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

    args::ArgumentParser parser("This is a euleraph program.", "This goes after the options.");

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

    if (true)
    {
        try
        {
            Importer importer;
            importer.import_data("data/large_test_data.xlsx",
                                 std::thread::hardware_concurrency() * 2,
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
    app().registerHandler("/k_hop_neighbor_query",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<ReaderWiredTiger>(conn);
                              EuleraphHttpHandle::k_hop_neighbor_query(req, std::move(callback), reader);
                          },
                          {Post});
    app().registerHandler("/common_neighbor_query",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              auto reader = std::make_shared<ReaderWiredTiger>(conn);
                              EuleraphHttpHandle::common_neighbor_query(req, std::move(callback), reader);
                          },
                          {Post});

    app().run();

    return 0;
}