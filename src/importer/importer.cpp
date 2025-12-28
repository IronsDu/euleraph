#include "importer/importer.hpp"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <xlnt/xlnt.hpp>

#include "interface/storage/reader.hpp"
#include "interface/storage/writer.hpp"

#include "utils/defer.hpp"
#include "utils/wait_group.hpp"
#include "utils/csv.h"
#include "utils/gaudy.hpp"

using namespace std;

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

static void import_edges_first_step(std::vector<EdgeRowData>&                  file_edges,
                                    WriterInterfaceFactory                     writer_interface_generator,
                                    ReaderInterfaceFactory                     reader_interface_factory,
                                    std::shared_ptr<std::counting_semaphore<>> control_write_first_step,
                                    std::shared_ptr<std::counting_semaphore<>> control_real_write_edges,
                                    ThreadPool::Ptr                            real_write_edges_thread_pool,
                                    WaitGroup::Ptr                             async_task_wg,
                                    std::shared_ptr<std::atomic_uint64_t>      writed_edges_num)
{
    const auto t_begin = std::chrono::steady_clock::now();
    // spdlog::info("start once write edges resolve vertex, size:{}", file_edges.size());
    auto writer = writer_interface_generator();
    auto reader = reader_interface_factory();

    // 去重之后的所有的点的标识
    std::vector<VertexPk>    pks;
    std::vector<LabelTypeId> pk_label_type_ids;
    {
        std::unordered_set<VertexPk> pk_map;
        for (auto& edge_source_data : file_edges)
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
    std::vector<VertexId> fuck_ids;
    bool                  have_retry = false;
    while (true)
    {
        try
        {
            fuck_ids = writer->write_vertices(unresolve_pks);
        }
        catch (const std::runtime_error& e)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            spdlog::debug("batch write_vertices catch exception:{}, will retry", e.what());
            have_retry = true;
            continue;
        }
        if (have_retry)
        {
            spdlog::debug("retry write_vertices success");
        }
        break;
    }
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
    edges.reserve(file_edges.size() * 2);
    for (auto& edge_source_data : file_edges)
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
    spdlog::debug("first step cost {} ms", milliseconds);

    const auto file_edge_num = file_edges.size();

    control_real_write_edges->acquire();
    async_task_wg->add();
    real_write_edges_thread_pool->enqueue([=, edges = std::move(edges)]() {
        DEFER(async_task_wg->done(); control_real_write_edges->release());

        const auto t_begin = std::chrono::steady_clock::now();
        auto       writer  = writer_interface_generator();

        bool have_retry = false;
        while (true)
        {
            try
            {
                writer->write_edges(edges);
            }
            catch (const std::runtime_error& e)
            {
                spdlog::debug("batch write_edges catch exception:{}, will retry", e.what());
                have_retry = true;
                continue;
            }
            if (have_retry)
            {
                spdlog::debug("retry write_edges success");
            }
            break;
        }

        auto t_end        = std::chrono::steady_clock::now();
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
        spdlog::debug("real write edges step cost {} ms", milliseconds);

        writed_edges_num->fetch_add(file_edge_num);
    });

    // spdlog::info("end once write edges resole vertex, size:{}", file_edges.size());
};

namespace fs = std::filesystem;
enum class FileType
{
    Excel,
    CSV,
    Unknown
};

static FileType get_file_type(const std::string& filename)
{
    fs::path filePath(filename);

    std::string ext = filePath.extension().string();

    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == ".xlsx" || ext == ".xls")
    {
        return FileType::Excel;
    }
    else if (ext == ".csv")
    {
        return FileType::CSV;
    }

    return FileType::Unknown;
}

void Importer::import_data(const std::string&     file_path,
                           int                    write_edge_thread_pool_concurrency_num,
                           int                    batch_size,
                           WriterInterfaceFactory wirter_interface_generator,
                           ReaderInterfaceFactory reader_interface_factory,
                           std::function<void()>  release_writer,
                           std::function<void()>  release_reader,
                           std::optional<int64_t> csv_row_num)
{
    const auto t_begin = std::chrono::steady_clock::now();

    auto             writed_edges_num = std::make_shared<std::atomic_uint64_t>(0);
    std::atomic_bool writed_completed = false;

    // 写入的第一阶段任务的线程池，必须为一个工作线程
    // 它负责解决这一批边中的所有点
    auto write_first_step_thread_pool = std::make_shared<ThreadPool>(1, [=]() {
        release_writer();
        release_reader();
    });
    // 用于控制写入任务并发数的信号量(这不是实际写入边的任务控制), 现在必须为1,
    // 此任务里会写没有写入过的点，让分配写入边的任务。
    const auto control_write_first_step = std::make_shared<std::counting_semaphore<>>((1));

    // 实际写入最终的边的线程池
    const ThreadPool::Ptr real_write_edge_thread_pool =
        std::make_shared<ThreadPool>(write_edge_thread_pool_concurrency_num, [=]() { release_writer(); });
    // 控制实际写入最终的边的任务并发数
    const auto control_real_write_edge =
        std::make_shared<std::counting_semaphore<>>(write_edge_thread_pool_concurrency_num);

    // 用于等待所有任务完成的waitgroup
    const auto async_task_wg = std::make_shared<WaitGroup>();

    auto batch_edge_row = std::make_shared<std::vector<EdgeRowData>>();
    batch_edge_row->reserve(batch_size);
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
            batch_edge_row->reserve(batch_size);
        }
    };

    spdlog::info("start import edges from file: {}", file_path);

    auto writer = wirter_interface_generator();

    auto cons_edge_begin = std::chrono::steady_clock::now();

    // 打印当前写入进度
    WaitGroup wait_output_thread;

    const auto file_type = get_file_type(file_path);
    if (file_type == FileType::Excel)
    {
        bool                            first_row = true;
        xlnt::streaming_workbook_reader wb_streaming_reader;
        wb_streaming_reader.open(file_path);

        for (auto sheet_name : wb_streaming_reader.sheet_titles())
        {
            wb_streaming_reader.begin_worksheet(sheet_name);
            DEFER(wb_streaming_reader.end_worksheet(););

            while (true)
            {
                EdgeRowData edgeRowData;
                if (!wb_streaming_reader.has_cell())
                {
                    break;
                }
                edgeRowData.start_pk = wb_streaming_reader.read_cell().to_string();

                if (!wb_streaming_reader.has_cell())
                {
                    break;
                }
                edgeRowData.start_label_type = wb_streaming_reader.read_cell().to_string();

                if (!wb_streaming_reader.has_cell())
                {
                    break;
                }
                edgeRowData.relation_type = wb_streaming_reader.read_cell().to_string();

                if (!wb_streaming_reader.has_cell())
                {
                    break;
                }
                edgeRowData.end_pk = wb_streaming_reader.read_cell().to_string();

                if (!wb_streaming_reader.has_cell())
                {
                    break;
                }
                edgeRowData.end_label_type = wb_streaming_reader.read_cell().to_string();

                if (first_row)
                {
                    first_row = false;
                    continue;
                }

                edgeRowData.relation_type_id    = writer->write_relation_type(edgeRowData.relation_type);
                edgeRowData.start_label_type_id = writer->write_label_type(edgeRowData.start_label_type);
                edgeRowData.end_label_type_id   = writer->write_label_type(edgeRowData.end_label_type);

                batch_edge_row->emplace_back(std::move(edgeRowData));

                if (batch_edge_row->size() >= batch_size)
                {
                    auto t_end = std::chrono::steady_clock::now();
                    auto milliseconds =
                        std::chrono::duration_cast<std::chrono::milliseconds>(t_end - cons_edge_begin).count();
                    spdlog::debug("cons edges  cost {} ms", milliseconds);

                    run_write_task();
                    cons_edge_begin = std::chrono::steady_clock::now();
                }
            }
        }
    }
    else if (file_type == FileType::CSV)
    {
        size_t line_count = 0;
        if (csv_row_num)
        {
            line_count = csv_row_num.value();
        }
        else
        {
            io::LineReader lr(file_path);
            lr.next_line(); // skip header
            while (lr.next_line())
            {
                line_count++;
            }
        }

        spdlog::info("total csv file line num:{}", line_count);

        wait_output_thread.add();
        std::thread([&]() {
            DEFER(wait_output_thread.done());

            NeonArrowBar bar("IMPORT", line_count, t_begin, 70);

            // 100ms前的已完成值
            auto last_100ms_value = writed_edges_num->load();
            // 上一秒的已完成值和时间点
            auto last_second_value = writed_edges_num->load();
            auto last_second_time  = std::chrono::steady_clock::now();
            // 上一秒计算的实时速度
            auto last_avg = 0;

            while (!writed_completed.load())
            {
                // 每100ms检查一下已完成数量是否存在变化
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const auto new_value = writed_edges_num->load();
                if (new_value > last_100ms_value)
                {
                    const auto current_time = std::chrono::steady_clock::now();

                    // 上次(以秒为单位)变化到现在耗时多少毫秒
                    // 超过1秒就更新一次实时速度
                    const auto last_diff_mill =
                        std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_second_time).count();
                    if (last_diff_mill >= 1000)
                    {
                        last_avg = (static_cast<uint64_t>((new_value - last_second_value) * 1000)) / last_diff_mill;
                        last_second_time  = current_time;
                        last_second_value = new_value;
                    }

                    // 目前总共耗时秒数
                    const auto total_seconds =
                        std::chrono::duration_cast<std::chrono::seconds>(current_time - t_begin).count();
                    bar.update(new_value,
                               fmt::format("Loading... {}s, progress:{}/{}, avg:{}/s, rt speed:{}/s",
                                           total_seconds,
                                           new_value,
                                           line_count,
                                           new_value / (std::max<int>(total_seconds, 1)),
                                           last_avg));
                    last_100ms_value = new_value;
                }
            }
        }).detach();

        io::CSVReader<5> csv_reader(file_path);

        // 读取标题行，忽略多余的列
        csv_reader.read_header(io::ignore_extra_column, "startId", "startLabel", "edgeLabel", "endId", "endLabel");

        std::string startId;
        std::string startLabel;
        std::string edgeLabel;
        std::string endId;
        std::string endLabel;
        // 流式读取每一行
        while (csv_reader.read_row(startId, startLabel, edgeLabel, endId, endLabel))
        {
            EdgeRowData edgeRowData;

            edgeRowData.start_pk         = startId;
            edgeRowData.start_label_type = startLabel;
            edgeRowData.relation_type    = edgeLabel;
            edgeRowData.end_pk           = endId;
            edgeRowData.end_label_type   = endLabel;

            edgeRowData.relation_type_id    = writer->write_relation_type(edgeRowData.relation_type);
            edgeRowData.start_label_type_id = writer->write_label_type(edgeRowData.start_label_type);
            edgeRowData.end_label_type_id   = writer->write_label_type(edgeRowData.end_label_type);

            batch_edge_row->emplace_back(std::move(edgeRowData));

            if (batch_edge_row->size() >= batch_size)
            {
                auto t_end = std::chrono::steady_clock::now();
                auto milliseconds =
                    std::chrono::duration_cast<std::chrono::milliseconds>(t_end - cons_edge_begin).count();
                spdlog::debug("cons edges  cost {} ms", milliseconds);

                run_write_task();
                cons_edge_begin = std::chrono::steady_clock::now();
            }
        }
    }
    else
    {
        spdlog::error("only support excel or csv file now");
        throw std::runtime_error("only support excel or csv file now");
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

    spdlog::info("complete import, cost:{} seconds", seconds);
}