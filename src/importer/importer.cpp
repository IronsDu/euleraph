#include "importer/importer.hpp"

#include <xlnt/xlnt.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>
#include <unordered_set>
#include <spdlog/spdlog.h>

#include "interface/storage/reader.hpp"
#include "interface/storage/writer.hpp"

#include "utils/defer.hpp"
#include "utils/wait_group.hpp"

using namespace std;

static constexpr std::size_t BATCH_SIZE = 1000;

struct EdgeRowData
{
    string         start_pk;
    string         start_label_type;
    LabelTypeId    start_label_type_id;
    string         relation_type;
    RelationTypeId relation_type_id;
    string         end_pk;
    string         end_label_type;
    LabelTypeId    end_label_type_id;
};

static void import_edges_first_step(std::vector<EdgeRowData>&                  excel_edges,
                                    WriterInterfaceFactory                     writer_interface_generator,
                                    ReaderInterfaceFactory                     reader_interface_factory,
                                    std::shared_ptr<std::counting_semaphore<>> control_write_first_step,
                                    std::shared_ptr<std::counting_semaphore<>> control_real_write_edges,
                                    ThreadPool::Ptr                            real_write_edges_thread_pool,
                                    WaitGroup::Ptr                             async_task_wg,
                                    std::shared_ptr<std::atomic_uint64_t>      writed_edges_num)
{
    const auto t_begin = std::chrono::steady_clock::now();
    // spdlog::info("start once write edges resolve vertex, size:{}", excel_edges.size());
    auto writer = writer_interface_generator();
    auto reader = reader_interface_factory();

    // 去重之后的所有的点的标识
    std::vector<VertexPk>    pks;
    std::vector<LabelTypeId> pk_label_type_ids;
    {
        std::unordered_set<VertexPk> pk_map;
        for (auto& edge_source_data : excel_edges)
        {
            if (!pk_map.contains(edge_source_data.start_pk))
            {
                pk_map.insert(edge_source_data.start_pk);
                pks.push_back(edge_source_data.start_pk);
                pk_label_type_ids.push_back(edge_source_data.start_label_type_id);
            }
            if (!pk_map.contains(edge_source_data.end_pk))
            {
                pk_map.insert(edge_source_data.end_pk);
                pks.push_back(edge_source_data.end_pk);
                pk_label_type_ids.push_back(edge_source_data.end_label_type_id);
            }
        }
    }

    std::unordered_map<VertexPk, VertexId> pk_to_ids;

    std::vector<WriterInterface::Vertex> unresolve_pks;
    unresolve_pks.reserve(pks.size());
    // 查询所有点对应的顶点id
    auto pk_ids = reader->get_vertex_ids(pks);
    for (int i = 0; i < pks.size(); i++)
    {
        // 如果某个顶点标识还没有顶点id，就把它放入待处理列表中
        if (!pk_ids[i])
        {
            unresolve_pks.push_back({pk_label_type_ids[i], pks[i]});
        }
        else
        {
            pk_to_ids[pks[i]] = pk_ids[i].value();
        }
    }

    // 写入待处理列表
    auto fuck_ids = writer->write_vertices(unresolve_pks);
    if (fuck_ids.size() != unresolve_pks.size())
    {
        spdlog::error("size not equal: {} != {}", fuck_ids.size(), unresolve_pks.size());
    }
    control_write_first_step->release();

    const auto unresolve_pks_size = unresolve_pks.size();
    for (int i = 0; i < unresolve_pks_size; i++)
    {
        pk_to_ids[unresolve_pks[i].vertex_pk] = fuck_ids[i];
    }

    std::vector<WriterInterface::Edge> edges;
    edges.reserve(excel_edges.size());
    for (auto& edge_source_data : excel_edges)
    {
        const auto start_vertex_id = pk_to_ids[edge_source_data.start_pk];
        const auto end_vertex_id   = pk_to_ids[edge_source_data.end_pk];

        edges.emplace_back(WriterInterface::Edge{edge_source_data.relation_type_id,
                                                 edge_source_data.start_label_type_id,
                                                 start_vertex_id,
                                                 EdgeDirection::OUTGOING,
                                                 edge_source_data.end_label_type_id,
                                                 end_vertex_id});

        edges.emplace_back(WriterInterface::Edge{edge_source_data.relation_type_id,
                                                 edge_source_data.end_label_type_id,
                                                 end_vertex_id,
                                                 EdgeDirection::INCOMING,
                                                 edge_source_data.start_label_type_id,
                                                 start_vertex_id});
    }

    auto t_end        = std::chrono::steady_clock::now();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
    spdlog::info("first step cost {} ms", milliseconds);

    control_real_write_edges->acquire();
    async_task_wg->add();
    real_write_edges_thread_pool->enqueue([=, edges = std::move(edges)]() {
        DEFER(async_task_wg->done(); control_real_write_edges->release());

        const auto t_begin = std::chrono::steady_clock::now();
        auto       writer  = writer_interface_generator();
        writer->write_edges(edges);

        auto t_end        = std::chrono::steady_clock::now();
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
        spdlog::info("real write edges step cost {} ms", milliseconds);

        writed_edges_num->fetch_add(edges.size());
    });

    // spdlog::info("end once write edges resole vertex, size:{}", excel_edges.size());
};

void Importer::import_data(const std::string&     file_path,
                           int                    write_edge_thread_pool_concurrency_num,
                           WriterInterfaceFactory wirter_interface_generator,
                           ReaderInterfaceFactory reader_interface_factory)
{
    const auto t_begin = std::chrono::steady_clock::now();

    auto             writed_edges_num = std::make_shared<std::atomic_uint64_t>(0);
    std::atomic_bool writed_completed = false;

    // 打印当前写入进度
    WaitGroup wait_output_thread;
    wait_output_thread.add();
    std::thread([&]() {
        DEFER(wait_output_thread.done());
        while (!writed_completed.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            spdlog::warn("current write edges num: {}", writed_edges_num->load());
        }
    }).detach();

    // 写入的第一阶段任务的线程池，必须为一个工作线程
    // 它负责解决这一批边中的所有点
    auto write_first_step_thread_pool = std::make_shared<ThreadPool>(1);
    // 用于控制写入任务并发数的信号量(这不是实际写入边的任务控制), 现在必须为1,
    // 此任务里会写没有写入过的点，让分配写入边的任务。
    const auto control_write_first_step = std::make_shared<std::counting_semaphore<>>((1));

    // 实际写入最终的边的线程池
    const ThreadPool::Ptr real_write_edge_thread_pool =
        std::make_shared<ThreadPool>(write_edge_thread_pool_concurrency_num);
    // 控制实际写入最终的边的任务并发数
    const auto control_real_write_edge =
        std::make_shared<std::counting_semaphore<>>(write_edge_thread_pool_concurrency_num);

    // 用于等待所有任务完成的waitgroup
    const auto async_task_wg = std::make_shared<WaitGroup>();

    auto batch_edge_row = std::make_shared<std::vector<EdgeRowData>>();
    batch_edge_row->reserve(BATCH_SIZE);
    auto run_write_task = [=, &batch_edge_row]() {
        if (!batch_edge_row->empty())
        {
            control_write_first_step->acquire();
            async_task_wg->add();
            write_first_step_thread_pool->enqueue([=]() mutable {
                DEFER(async_task_wg->done(););
                import_edges_first_step(*batch_edge_row,
                                        wirter_interface_generator,
                                        reader_interface_factory,
                                        control_write_first_step,
                                        control_real_write_edge,
                                        real_write_edge_thread_pool,
                                        async_task_wg,
                                        writed_edges_num);
            });
            batch_edge_row = std::make_shared<std::vector<EdgeRowData>>();
            batch_edge_row->reserve(BATCH_SIZE);
        }
    };

    xlnt::workbook wb;
    wb.load(file_path);
    auto ws        = wb.active_sheet();
    auto writer    = wirter_interface_generator();
    bool first_row = true;
    int  i         = 0;

    auto cons_edge_begin = std::chrono::steady_clock::now();

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

        edgeRowData.relation_type_id    = writer->write_relation_type(edgeRowData.relation_type);
        edgeRowData.start_label_type_id = writer->write_label_type(edgeRowData.start_label_type);
        edgeRowData.end_label_type_id   = writer->write_label_type(edgeRowData.end_label_type);

        batch_edge_row->emplace_back(std::move(edgeRowData));

        if (batch_edge_row->size() >= BATCH_SIZE)
        {

            auto t_end        = std::chrono::steady_clock::now();
            auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - cons_edge_begin).count();
            spdlog::info("cons edges  cost {} ms", milliseconds);

            spdlog::info("current excel row num:{}", i);
            // TODO::先查询 这些点是否存在，对不存在的点进行分hash写入
            // TODO::然后再写边
            run_write_task();
            cons_edge_begin = std::chrono::steady_clock::now();
        }
        i++;
    }

    if (!batch_edge_row->empty())
    {
        run_write_task();
    }

    // 等待所有任务完成
    async_task_wg->wait();

    // 设置写入完成
    writed_completed.store(true);
    // 等待打印进度的线程结束
    wait_output_thread.wait();

    auto t_end   = std::chrono::steady_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(t_end - t_begin).count();

    std::cout << "完成启动，" << seconds << "s\n";
}