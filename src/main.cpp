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

using namespace drogon;

// 全局链接
WT_CONNECTION* conn = nullptr;
// 每一个线程一个writer对象
thread_local std::shared_ptr<WriterInterface> writer = nullptr;
static std::shared_ptr<WriterInterface>       make_writer()
{
    if (!writer)
    {
        writer = std::make_shared<WriterWiredTiger>(conn);
    }
    return writer;
}

int main(int argc, char** argv)
{
    args::ArgumentParser parser("This is a euleraph program.", "This goes after the options.");

    const std::string log = R"(
             .__                             .__     
  ____  __ __|  |   ________________  ______ |  |__  
_/ __ \|  |  \  | _/ __ \_  __ \__  \ \____ \|  |  \ 
\  ___/|  |  /  |_\  ___/|  | \// __ \|  |_> >   Y  \
 \___  >____/|____/\___  >__|  (____  /   __/|___|  /
     \/                \/           \/|__|        \/ 
)";
    std::cout << log << std::endl;

    wiredtiger_initialize_databse_schema("graph_database");

    if (wiredtiger_open("graph_database", nullptr, "create", &conn) != 0)
    {
        std::cerr << "open failed" << std::endl;
    }

    auto thread_pool = std::make_shared<ThreadPool>(std::thread::hardware_concurrency() * 2);

    if (true)
    {
        try
        {
            Importer importer;
            importer.import_data("data/sample_data.xlsx",
                                 thread_pool,
                                 std::thread::hardware_concurrency() * 2,
                                 make_writer);

            // TODO::导入后测试一下
            if (true)
            {
                auto reader          = std::make_shared<ReaderWiredTiger>(conn);
                auto label_type_id   = reader->get_label_type_id("Company");
                auto start_vertex_id = reader->get_vertex_id(label_type_id.value(), "node_0000029");
                auto neighbors_edges = reader->get_neighbors_by_start_vertex(start_vertex_id.value(),
                                                                             label_type_id.value(),
                                                                             EdgeDirection::OUTGOING,
                                                                             std::nullopt);
                std::cout << "neighbors_edges size:" << neighbors_edges.size() << std::endl;
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