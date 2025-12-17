#include "importer/importer.hpp"

#include <xlnt/xlnt.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

#include "interface/storage/writer.hpp"
#include "storage/writer_wiredtiger.hpp"

#include "utils/defer.hpp"
#include "utils/wait_group.hpp"

using namespace std;

static constexpr std::size_t BATCH_SIZE = 500;

struct EdgeRowData
{
    string start_pk;
    string start_label_type;
    string relation_type;
    string end_pk;
    string end_label_type;
};

static void import_edges(std::vector<EdgeRowData>& excel_edges, WriterInterfaceFactory wirter_interface_generator)
{
    auto writer = wirter_interface_generator();

    std::vector<WriterInterface::Edge> edges;
    edges.reserve(excel_edges.size());

    std::vector<WriterInterface::Vertex> pedding_vertexs;
    pedding_vertexs.reserve(excel_edges.size() * 2);

    for (auto& edge_source_data : excel_edges)
    {
        WriterInterface::Edge edge;
        edge.direction           = EdgeDirection::OUTGOING;
        edge.relation_type_id    = writer->write_relation_type(edge_source_data.relation_type);
        edge.start_label_type_id = writer->write_label_type(edge_source_data.start_label_type);
        edge.end_label_type_id   = writer->write_label_type(edge_source_data.end_label_type);

        pedding_vertexs.emplace_back(
            WriterInterface::Vertex{edge.start_label_type_id, std::move(edge_source_data.start_pk)});
        pedding_vertexs.emplace_back(
            WriterInterface::Vertex{edge.end_label_type_id, std::move(edge_source_data.end_pk)});

        edges.emplace_back(edge);
    }

    auto vertices_ids = writer->write_vertices(pedding_vertexs);
    int  i            = 0;
    for (auto& edge : edges)
    {
        edge.start_vertex_id = vertices_ids[i++];
        edge.end_vertex_id   = vertices_ids[i++];
    }

    writer->write_edges(edges);
};

void Importer::import_data(const std::string&     file_path,
                           ThreadPool::Ptr        thread_pool,
                           int                    max_worker_concurrency_num,
                           WriterInterfaceFactory wirter_interface_generator)
{
    const auto t_begin = std::chrono::steady_clock::now();

    // 用于等待所有写入任务完成的waitgroup
    WaitGroup wait_write_task;
    // 用于控制写入任务并发数的信号量
    std::counting_semaphore<100> sem((max_worker_concurrency_num));

    xlnt::workbook wb;
    wb.load(file_path);
    auto ws = wb.active_sheet();

    bool first_row = true;

    auto batch_edge_row = std::make_shared<std::vector<EdgeRowData>>();
    batch_edge_row->reserve(BATCH_SIZE);

    std::atomic_uint64_t writed_edges_num = 0;
    std::atomic_bool     writed_completed = false;

    // 打印当前写入进度
    WaitGroup wait_output_thread;
    wait_output_thread.add();
    std::thread([&]() {
        DEFER(wait_output_thread.done());
        while (!writed_completed.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "current write edges: " << writed_edges_num.load() << std::endl;
        }
    }).detach();

    auto run_write_task = [&]() {
        if (!batch_edge_row->empty())
        {
            sem.acquire();
            wait_write_task.add();
            thread_pool->enqueue([&, wirter_interface_generator, batch_edge_row = std::move(batch_edge_row)]() mutable {
                DEFER(sem.release(); wait_write_task.done(););
                import_edges(*batch_edge_row, wirter_interface_generator);
                writed_edges_num.fetch_add(batch_edge_row->size());
            });
            batch_edge_row = std::make_shared<std::vector<EdgeRowData>>();
            batch_edge_row->reserve(BATCH_SIZE);
        }
    };
    for (const auto& row : ws.rows(false))
    {
        if (first_row)
        {
            first_row = false;
            continue;
        }
        if (!row[0].has_value())
        {
            break;
        }

        EdgeRowData edgeRowData;
        edgeRowData.start_pk         = row[0].to_string();
        edgeRowData.start_label_type = row[1].to_string();
        edgeRowData.relation_type    = row[2].to_string();
        edgeRowData.end_pk           = row[3].to_string();
        edgeRowData.end_label_type   = row[4].to_string();

        batch_edge_row->emplace_back(std::move(edgeRowData));

        if (batch_edge_row->size() >= BATCH_SIZE)
        {
            run_write_task();
        }
    }

    if (!batch_edge_row->empty())
    {
        run_write_task();
    }

    // 等待所有写入的任务完成
    wait_write_task.wait();

    // 设置写入完成
    writed_completed.store(true);
    // 等待打印进度的线程结束
    wait_output_thread.wait();

    auto t_end   = std::chrono::steady_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(t_end - t_begin).count();

    std::cout << "完成启动，" << seconds << "s\n";
}