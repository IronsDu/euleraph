#include <iostream>
#include <thread>

#include <drogon/drogon.h>
#include <args.hxx>

#include "importer/importer.hpp"
#include "service/http/euleraph_http_handle.hpp"
#include "storage/reader_wiredtiger.hpp"
#include "storage/writer_wiredtiger.hpp"
#include "storage/wiredtiger_common.hpp"
#include "utils/thread_pool.hpp"
#include "utils/gaudy.hpp"
#include "log/log.hpp"

using namespace drogon;

struct cli_args
{
    std::string path;
    int         batch_size;
    int         concurrency;
    int         port;
};

constexpr int DEFAULT_BATCH_SIZE = 100000;
constexpr int PORT               = 8200;

static int DefaultConcurrency()
{
    unsigned n = std::thread::hardware_concurrency() * 2;
    return (n == 0u) ? 1 : static_cast<int>(n);
}

bool parse_cli_args(int argc, char** argv, cli_args& out_args) {
    // 附默认值
    out_args.batch_size   = DEFAULT_BATCH_SIZE;
    out_args.concurrency  = DefaultConcurrency();
    out_args.port         = PORT;

    args::ArgumentParser parser("This is a euleraph program.", "This goes after the options.");

    args::HelpFlag help(parser, "help", "Show this help menu", {'h', "help"});

    args::ValueFlag<std::string> path(parser, "data_path", "input data path (required unless -h)",
                                      {'d', "data_path"});

    args::ValueFlag<int> batch_size(parser, "batch_size", "batch size (optional)",
                                    {'b', "batch_size"});
    args::ValueFlag<int> concurrency(parser, "concurrency", "Concurrency (optional)",
                                     {'c', "concurrency"});
    args::ValueFlag<int> port(parser, "port", "Port (optional)",
                              {'p', "port"});

    try {
        parser.ParseCLI(argc, argv);

        if (help) {
            std::cout << parser << std::endl;
            return false;
        }

        if (!path) {
            std::cerr << "Error: 必须指定路径 (-d/--data_path) 或者使用-h查看帮助\n\n";
            // std::cerr << parser << std::endl;
            return false;
        }

        // 必填赋值
        out_args.path = args::get(path);

        // 可选覆盖默认值
        if (batch_size)  out_args.batch_size  = args::get(batch_size);
        if (concurrency) out_args.concurrency = args::get(concurrency);
        if (port)        out_args.port        = args::get(port);

        return true;
    }
    catch (const args::Help&) {
        std::cout << parser << std::endl;
        return false;
    }
    catch (const args::ParseError& e) {
        std::cerr << "ParseError: " << e.what() << "\n\n";
        // std::cerr << parser << std::endl;
        return false;
    }
    catch (const args::ValidationError& e) {
        std::cerr << "ValidationError: " << e.what() << "\n\n";
        // std::cerr << parser << std::endl;
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
static ReaderInterface::Ptr make_reader()
{
    if (!reader)
    {
        reader = std::make_shared<ReaderWiredTiger>(conn);
    }
    return reader;
}

int main(int argc, char** argv)
{
    initialize_log();

    const std::string logo = R"(
___________     .__                             .__     
\_   _____/__ __|  |   ________________  ______ |  |__  
 |    __)_|  |  \  | _/ __ \_  __ \__  \ \____ \|  |  \ 
 |        \  |  /  |_\  ___/|  | \// __ \|  |_> >   Y  \
/_______  /____/|____/\___  >__|  (____  /   __/|___|  /
        \/                \/           \/|__|        \/ 
)";

    play_neon_banner(logo);

    wiredtiger_initialize_databse_schema("graph_database");

    if (wiredtiger_open("graph_database", nullptr, "create", &conn) != 0)
    {
        std::cerr << "open failed" << std::endl;
    }

    auto thread_pool = std::make_shared<ThreadPool>(std::thread::hardware_concurrency() * 2);

    // 解析用户参数
    cli_args param;

    if (!parse_cli_args(argc, argv, param))
    {
        return 0;
    }

    if (true)
    {
        try
        {
            Importer importer;
            importer.import_data(param.path,
                                 param.concurrency,
                                 param.batch_size,
                                 make_writer,
                                 make_reader);

            // TODO::导入后测试一下
            if (true)
            {
                auto reader          = std::make_shared<ReaderWiredTiger>(conn);
                auto label_type_id   = reader->get_label_type_id("User");
                auto start_vertex_id = reader->get_vertex_id(label_type_id.value(), "node_0607477");
                auto neighbors_edges = reader->get_neighbors_by_start_vertex(start_vertex_id.value(),
                                                                             label_type_id.value(),
                                                                             EdgeDirection::OUTGOING,
                                                                             std::nullopt);
                std::cout << "neighbors_edges size:" << neighbors_edges.size() << std::endl;
                for (const auto& edge : neighbors_edges)
                {
                    spdlog::info("======================");
                    auto start_label_type = reader->get_label_type_by_id(edge.start_label_type_id);
                    auto end_label_type   = reader->get_label_type_by_id(edge.end_label_type_id);

                    auto relation_type = reader->get_relation_type_by_id(edge.relation_type_id);

                    auto start_vertex_pk = reader->get_vertex_pk_by_id(edge.start_vertex_id);
                    auto end_vertex_pk   = reader->get_vertex_pk_by_id(edge.end_vertex_id);

                    spdlog::info("{}:{} -> [{}] -> {}:{}",
                                 start_vertex_pk.value(),
                                 start_label_type.value(),
                                 relation_type.value(),
                                 end_vertex_pk.value(),
                                 end_label_type.value());
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Data import failed: " << e.what() << std::endl;
        }
    }

    app().setLogPath("./").setLogLevel(trantor::Logger::kWarn).addListener("0.0.0.0", 10020).setThreadNum(16);
    app().registerHandler("/ping",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              EuleraphHttpHandle::ping(req, std::move(callback));
                          },
                          {Get});

    app().run();

    return 0;
}