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
    // 目前单并发写入边,因为多个并发时会有冲突问题
    return 1;
}

struct cli_args
{
    cli_args()
    {
        batch_size  = DEFAULT_BATCH_SIZE;
        concurrency = DefaultConcurrency();
        port        = PORT;
    }

    // 数据库目录
    std::string database_dir;
    // excel 文件路径
    std::string            data_path;
    int                    batch_size;
    int                    concurrency;
    int                    port;
    bool                   need_import = DefaultNeedImport;
    int                    cache_size  = CacheSize; // MB
    std::string            log_level   = DefaultLogLevel;
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
                           fmt::format("whether need import data (optional, default:{})", DefaultNeedImport),
                                             {"need_import"},
                           DefaultNeedImport);

    args::ValueFlag<int>         batch_size(parser,
                                    "batch_size",
                                    fmt::format("batch size (optional, default:{})", DEFAULT_BATCH_SIZE),
                                            {"batch_size"},
                                    DEFAULT_BATCH_SIZE);
    args::ValueFlag<int>         concurrency(parser,
                                     "concurrency",
                                     fmt::format("Concurrency (optional, default:{})", DefaultConcurrency()),
                                             {"concurrency"},
                                     DefaultConcurrency());
    args::ValueFlag<int>         port(parser, "port", "Port (optional)", {"port"}, PORT);
    args::ValueFlag<int>         cache_size(parser,
                                    "cache_size",
                                    fmt::format("WiredTiger Cache size in MB (optional, default:{})", CacheSize),
                                            {"cache_size"},
                                    CacheSize);
    args::ValueFlag<std::string> log_level(parser,
                                           "log_level",
                                           fmt::format("Log level (optional, default:{})", DefaultLogLevel),
                                           {"log_level"},
                                           DefaultLogLevel);
    args::ValueFlag<int64_t>     csv_row_num(parser,
                                         "csv_row_num",
                                         "number of rows in csv file (optional, default: all rows)",
                                             {"csv_row_num"});

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
    const std::string logo = R"(
___________     .__                             .__     
\_   _____/__ __|  |   ________________  ______ |  |__  
 |    __)_|  |  \  | _/ __ \_  __ \__  \ \____ \|  |  \ 
 |        \  |  /  |_\  ___/|  | \// __ \|  |_> >   Y  \
/_______  /____/|____/\___  >__|  (____  /   __/|___|  /
        \/                \/           \/|__|        \/ 
)";
    play_neon_banner(logo);

    // 解析用户参数
    cli_args param;
    if (!parse_cli_args(argc, argv, param))
    {
        return 1;
    }

    spdlog::set_level(spdlog::level::from_str(param.log_level));
    initialize_log(spdlog::level::from_str(param.log_level));

    wiredtiger_initialize_databse_schema(param.database_dir);
    if (wiredtiger_open(param.database_dir.c_str(),
                        nullptr,
                        fmt::format("create,cache_size={}MB", param.cache_size).c_str(),
                        &conn) != 0)
    {
        spdlog::error("Failed to open WiredTiger database at {}", param.database_dir);
        return 1;
    }

    spdlog::info("WiredTiger database opened at {}, cache size:{}MB, wirte edge concurrency:{}, and batch size:{}",
                 param.database_dir,
                 param.cache_size,
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
    app().setUploadPath(web_dir);
    app().run();

    return 0;
}