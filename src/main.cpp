#include <iostream>
#include <thread>

#include <drogon/drogon.h>
#include <args.hxx>

#include "importer/importer.hpp"
#include "service/http/euleraph_http_handle.hpp"
#include "storage/reader_wiredtiger.hpp"
#include "storage/one_trx_reader_wiredtiger.hpp"
#include "storage/writer_wiredtiger.hpp"
#include "storage/wiredtiger_common.hpp"
#include "utils/thread_pool.hpp"
#include "utils/gaudy.hpp"
#include "log/log.hpp"
#include "utils/wait_group.hpp"
#include "utils/defer.hpp"

using namespace drogon;

constexpr int     DEFAULT_BATCH_SIZE = 1000;
constexpr int     PORT               = 8200;
constexpr int     CacheSize          = 1024 * 5; // MB
constexpr bool    DefaultNeedImport  = false;
const std::string DefaultLogLevel    = std::string("info");

static int DefaultConcurrency()
{
    return 1;
}

struct cli_args
{
    // 数据库目录
    std::string database_dir;
    // excel 文件路径
    std::string data_path;
    int         batch_size  = DEFAULT_BATCH_SIZE;
    int         concurrency = DefaultConcurrency();
    int         port        = PORT;
    bool        need_import = DefaultNeedImport;
    int         cache_size  = CacheSize; // MB
    std::string log_level   = DefaultLogLevel;
    // wiredtiger的最大驱逐线程数
    // 默认值为CPU核心数的一半，至少为1
    int evict_threads_max = std::max<int>(std::thread::hardware_concurrency() / 2, 1);

    std::optional<int64_t> csv_row_num;
};

static bool parse_cli_args(int argc, char** argv, cli_args& out_args)
{
    args::ArgumentParser parser("This is a euleraph program.", "This goes after the options.");

    args::HelpFlag help(parser, "help", "Show this help menu", {'h', "help"});

    args::ValueFlag<std::string> database_dir(parser,
                                              "database_dir",
                                              "input excel data path (required unless -h)",
                                              {"database_dir"},
                                              args::Options::Required);

    args::ValueFlag<std::string> data_path(parser,
                                           "data_path",
                                           "input excel data path (required when need_import is true)",
                                           {"data_path"});
    args::Flag                   need_import(parser,
                           "need_import",
                           fmt::format("whether need import data (optional, default:{})", out_args.need_import),
                                             {"need_import"},
                           out_args.need_import);

    args::ValueFlag<int> batch_size(parser,
                                    "batch_size",
                                    fmt::format("batch size (optional, default:{})", out_args.cache_size),
                                    {"batch_size"},
                                    out_args.batch_size);
    args::ValueFlag<int> concurrency(parser,
                                     "concurrency",
                                     fmt::format("Concurrency (optional, default:{})", out_args.concurrency),
                                     {"concurrency"},
                                     out_args.concurrency);
    args::ValueFlag<int> port(parser, "port", "Port (optional)", {"port"}, out_args.port);
    args::ValueFlag<int> cache_size(
        parser,
        "cache_size",
        fmt::format("WiredTiger Cache size in MB (optional, default:{})", out_args.cache_size),
        {"cache_size"},
        out_args.cache_size);
    args::ValueFlag<std::string> log_level(parser,
                                           "log_level",
                                           fmt::format("Log level (optional, default:{})", out_args.log_level),
                                           {"log_level"},
                                           out_args.log_level);
    args::ValueFlag<int64_t>     csv_row_num(parser,
                                         "csv_row_num",
                                         "number of rows in csv file (optional, default: all rows)",
                                             {"csv_row_num"});
    args::ValueFlag<int>         evict_threads_max(
        parser,
        "evict_threads_max",
        fmt::format("WiredTiger max evict threads (optional, default:{})", out_args.evict_threads_max),
        {"evict_threads_max"},
        out_args.evict_threads_max);

    try
    {
        parser.ParseCLI(argc, argv);

        // 必填赋值
        out_args.database_dir = args::get(database_dir);

        // 可选覆盖默认值
        if (batch_size)
            out_args.batch_size = args::get(batch_size);
        if (concurrency)
            out_args.concurrency = args::get(concurrency);
        if (port)
            out_args.port = args::get(port);
        if (need_import)
        {
            out_args.need_import = true;
            out_args.data_path   = args::get(data_path);
            if (out_args.data_path.empty())
            {
                throw args::ParseError("data_path must be provided when need_import is true.");
            }
        }
        if (cache_size)
        {
            out_args.cache_size = args::get(cache_size);
        }
        if (log_level)
        {
            out_args.log_level = args::get(log_level);
        }
        if (csv_row_num)
        {
            out_args.csv_row_num = args::get(csv_row_num);
            if (out_args.csv_row_num <= 0)
            {
                throw args::ParseError("csv_row_num must be a positive integer.");
            }
        }
        if (evict_threads_max)
        {
            out_args.evict_threads_max = args::get(evict_threads_max);
        }

        if (out_args.batch_size <= 0 || out_args.concurrency <= 0 || out_args.port <= 0 || out_args.cache_size <= 0)
        {
            throw args::ParseError("batch_size, concurrency, cache_size and port must be positive integers.");
        }

        return true;
    }
    catch (const args::Help&)
    {
        std::cout << parser << std::endl;
        return false;
    }
    catch (const args::ParseError& e)
    {
        std::cerr << "ParseError: " << e.what() << std::endl;
        std::cerr << parser << std::endl;
        return false;
    }
    catch (const args::ValidationError& e)
    {
        std::cerr << "ValidationError: " << e.what() << std::endl;
        std::cerr << parser << std::endl;
        return false;
    }
}

// 全局链接
WT_CONNECTION* conn = nullptr;
// 每一个线程一个writer对象
thread_local WriterInterface::Ptr writer = nullptr;
// 每一个线程一个reader对象
thread_local ReaderInterface::Ptr reader = nullptr;
static WriterInterface::Ptr       make_writer()
{
    if (!writer)
    {
        writer = std::make_shared<WriterWiredTiger>(conn);
    }
    return writer;
}
static void release_writer()
{
    writer = nullptr;
}

static ReaderInterface::Ptr make_reader()
{
    if (!reader)
    {
        reader = std::make_shared<ReaderWiredTiger>(conn);
    }
    return reader;
}

static void release_reader()
{
    reader = nullptr;
}

static void create_checkpoint()
{
    do
    {
        // 创建数据导入后的检查点，避免重启后数据丢失
        WT_SESSION* session;
        int         code = conn->open_session(conn, nullptr, nullptr, &session);
        if (code != 0)
        {
            spdlog::error("Failed to open_session, code is:{}", code);
            break;
        }

        DEFER(session->close(session, nullptr));
        code = session->checkpoint(session, nullptr);
        if (code != 0)
        {
            spdlog::error("Failed to create checkpoint, code is:{}", code);
            break;
        }

        spdlog::info("checkpoint created.");
    } while (0);
}

int main(int argc, char** argv)
{
    // 解析用户参数
    cli_args param;
    if (!parse_cli_args(argc, argv, param))
    {
        return 1;
    }

    const std::string logo = R"(
___________     .__                             .__     
\_   _____/__ __|  |   ________________  ______ |  |__  
 |    __)_|  |  \  | _/ __ \_  __ \__  \ \____ \|  |  \ 
 |        \  |  /  |_\  ___/|  | \// __ \|  |_> >   Y  \
/_______  /____/|____/\___  >__|  (____  /   __/|___|  /
        \/                \/           \/|__|        \/ 
)";
    play_neon_banner(logo);

    initialize_log(spdlog::level::from_str(param.log_level));

    // 每秒钟刷新日志, 避免文件日志由于缓存到OS而许久没有刷新到磁盘
    spdlog::flush_every(std::chrono::seconds(1));

    wiredtiger_initialize_databse_schema(param.database_dir);
    if (wiredtiger_open(
            param.database_dir.c_str(),
            nullptr,
            fmt::format("create,cache_size={}MB,eviction=(threads_max={})", param.cache_size, param.evict_threads_max)
                .c_str(),
            &conn) != 0)
    {
        spdlog::error("Failed to open euleraph database at {}", param.database_dir);
        return 1;
    }

    spdlog::info("Euleraph database opened at {}, cache size:{}MB,  evict_threads_max:{}, wirte edge concurrency:{}, "
                 "and batch size:{}",
                 param.database_dir,
                 param.cache_size,
                 param.evict_threads_max,
                 param.concurrency,
                 param.batch_size);

    if (param.need_import)
    {
        try
        {
            std::atomic_bool import_completed = false;
            WaitGroup        wg;
            wg.add();

            // 定时创建检查点线程
            std::thread([&]() {
                DEFER(wg.done(););

                int counter = 0;
                while (!import_completed.load())
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    counter++;
                    if (counter >= 60)
                    {
                        create_checkpoint();
                        counter = 0;
                    }
                }
            }).detach();
            Importer importer;
            importer.import_data(param.data_path,
                                 param.concurrency,
                                 param.batch_size,
                                 make_writer,
                                 make_reader,
                                 release_writer,
                                 release_reader,
                                 param.csv_row_num);
            import_completed.store(true);
            wg.wait();

            // 创建数据导入后的检查点，避免重启后数据丢失
            create_checkpoint();
        }
        catch (const std::exception& e)
        {
            spdlog::error("Data import failed: {}", e.what());
            return 1;
        }
    }

    spdlog::info("Starting Euleraph server on port {}", param.port);

    const std::string web_dir = "./web";
    std::filesystem::create_directories(web_dir);

    app()
        .setLogPath(web_dir)
        .setLogLevel(trantor::Logger::kWarn)
        .addListener("0.0.0.0", param.port)
        .setThreadNum(std::thread::hardware_concurrency());
    app().registerHandler("/ping",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              EuleraphHttpHandle::ping(req, std::move(callback));
                          },
                          {Get});
    app().registerHandler("/api/v1/algorithms/k-hop-neighbors",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<ReaderWiredTiger>(conn);
                              EuleraphHttpHandle::k_hop_neighbor_query(req, std::move(callback), reader, conn);
                          },
                          {Post});
    app().registerHandler("/api/v1/algorithms/common-neighbors",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<ReaderWiredTiger>(conn);
                              EuleraphHttpHandle::common_neighbor_query(req, std::move(callback), reader);
                          },
                          {Post});

    // 获取指定顶点以及方向的一度邻居详细信息
    app().registerHandler("/get_one_hop_neighbors",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<OneTrxReaderWiredTiger>(conn);
                              EuleraphHttpHandle::get_one_hop_neighbors(req, std::move(callback), reader);
                          },
                          {Post});
    app().registerHandler("/api/v1/algorithms/wcc",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<OneTrxReaderWiredTiger>(conn);
                              EuleraphHttpHandle::wcc_query(req, std::move(callback), reader);
                          },
                          {Post});
    app().registerHandler("/api/v1/algorithms/subgraph-matching",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<OneTrxReaderWiredTiger>(conn);
                              EuleraphHttpHandle::subgraph_matching_query(req, std::move(callback), reader);
                          },
                          {Post});
    app().registerHandler("/api/v1/algorithms/adj-count",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<OneTrxReaderWiredTiger>(conn);
                              EuleraphHttpHandle::adj_count_query(req, std::move(callback), reader, conn);
                          },
                          {Post});

    app().setUploadPath(web_dir);
    app().run();

    return 0;
}