#include "k_hop_neighbors_count.hpp"

#include <mutex>
#include <semaphore>
#include <unordered_set>

#include "storage/one_trx_reader_wiredtiger.hpp"

#include "utils/gdm_array.h"
#include "utils/wait_group.hpp"
#include "utils/thread_pool.hpp"

using t_uint64 = uint64_t;

struct QueryArg
{
    VertexId                    start_vertex_id;
    EdgeDirection               direction;
    int                         k;
    std::vector<RelationTypeId> relation_type_id_list;
    std::vector<LabelTypeId>    end_label_type_id_list;
    WT_CONNECTION*              conn;
};

// 邻接count查询过程中产生的顶点信息，它作为每个一度邻接查询任务的输入和输出的元素类型
struct t_adj_count_query_vertex_info
{
    VertexId           vid;            // 顶点id
    std::vector<Edge>* path = nullptr; // 边路径，存在路径上的所有边信息
};
typedef t_adj_count_query_vertex_info t_adj_count_query_vertex_info;

static t_adj_count_query_vertex_info* create_adj_count_query_vertex_info(size_t path_cap)
{
    t_adj_count_query_vertex_info* info = new t_adj_count_query_vertex_info();
    info->vid                           = 0;
    info->path                          = new std::vector<Edge>();
    info->path->reserve(path_cap);
    return info;
}

static t_adj_count_query_vertex_info* create_adj_count_query_vertex_info_with_empty_path()
{
    t_adj_count_query_vertex_info* info = new t_adj_count_query_vertex_info();
    info->vid                           = 0;
    info->path                          = nullptr;
    return info;
}

static void free_adj_count_query_vertex_info(t_adj_count_query_vertex_info* info)
{
    if (info != NULL)
    {
        if (info->path != NULL)
        {
            delete info->path;
        }
        delete info;
    }
}

// 拷贝顶点路径信息
static void copy_adj_count_query_vertex_info_path(t_adj_count_query_vertex_info*       target,
                                                  const t_adj_count_query_vertex_info* src)
{
    auto         target_path = target->path;
    const auto   src_path    = src->path;
    const size_t num         = src_path->size();
    size_t       i           = 0;
    for (; i < num; i++)
    {
        target_path->push_back((*src_path)[i]);
    }
}

static bool edge_is_equal(const Edge& e1, const Edge& e2)
{
    return (e1.start_vertex_id == e2.start_vertex_id) && (e1.end_vertex_id == e2.end_vertex_id) &&
           (e1.direction == e2.direction) && (e1.relation_type_id == e2.relation_type_id);
}

// 查询边是否已经存在于路径之中
static bool edge_is_in_path(const t_adj_count_query_vertex_info* info, const Edge& find_eid)
{
    const auto   path = info->path;
    const size_t num  = path->size();
    size_t       i    = 0;
    for (; i < num; i++)
    {
        const auto& eid = (*path)[i];
        if (edge_is_equal(eid, find_eid))
        {
            return true;
        }
    }

    return false;
}

// 每一层的数据状态结构
struct parallel_adj_count_query_level_data;
typedef struct parallel_adj_count_query_level_data t_parallel_adj_count_query_level_data;

// 用于每一层收集到的最终结果顶点的类型
typedef struct adj_count_vid_info
{
    VertexId vid; // 顶点id
} t_adj_count_vid_info;

// 邻接count查询中每一层的子任务里用于去重顶点的hash表的最大个数，用于将去重的顶点分散到多个hash表中，
// 就可以使用多个互斥锁，每一个互斥锁对其对应的一个hash表进行加锁，从而降低锁竞争，提高性能
#define MAX_ADJ_COUNT_DISTINCT_HASH_SIZE 3

typedef struct parallel_adj_count_query_level_data
{
    const QueryArg* query_arg;

    std::shared_ptr<ThreadPool> thread_pool;

    std::counting_semaphore<>* semaphore; // 控制并发邻接任务线程总数的信号量，避免同时任务太多，导致OOM
    EdgeDirection              direction;

    // 上层开启的每个子任务结束后，（如果当前不是最后一层的话）会将结果放入下层进行处理，在其中就可能需要持有互斥锁
    // 这个互斥锁可以用于保护整个对象，但也不是必须在任何并发场景下都使用它。
    std::mutex           mu;
    std::atomic_uint64_t vertext_counter; // 当前层收集到的顶点计数
    // 当前层实际的用于去重的顶点hash表（vid_hash_table）的有效个数，
    // 当不需要去重时，hash_table_num为1，但这也仅仅为了代码统一，此时也并不实际需要访问hash table.
    int hash_table_num;
    // 去重的顶点hash表，其元素类型为VertexId
    std::unordered_set<VertexId>* vid_hash_table[MAX_ADJ_COUNT_DISTINCT_HASH_SIZE];
    // hash_table_mu
    // 用于保护对应的vid_hash_table。vid_hash_table在当前实现中，为了提升性能，其锁层次比mu更高。通常是先对vid_hash_table加锁，再对mu加锁。
    std::mutex hash_table_mu[MAX_ADJ_COUNT_DISTINCT_HASH_SIZE];
    // 等待被处理的顶点数组（为了batch，所以等待同一层的子任务完成后凑够固定N个顶点就开启下一层的邻接查询）
    // 主线程在每一层所有任务结束后，也会查看下层的此待处理队列是否为空，若不为空，则会为下层开启一个子查询任务
    // 这个顶点数组是用于分配到下层任务的顶点
    t_adj_count_query_vertex_info** pending_vid_array;
    // pending_vid_array的容量
    uint64_t pending_vid_array_cap;
    // 等待被处理的顶点的数量，即pending_vid_array里的有效顶点数量
    uint64_t pending_vid_num;

    WaitGroup* task_wg; // 用于当前层一批子任务完成的wg（注意，但不包含子任务的子任务）

    t_parallel_adj_count_query_level_data* parent_level_data; // 父级层级数据结构
    t_parallel_adj_count_query_level_data* next_level_data;   // 下一层数据结构

    int total_level_num;
} t_parallel_adj_count_query_level_data;

// 并行邻接count查询子任务线程的参数对象
typedef struct parallel_adj_count_task_arg
{
    t_parallel_adj_count_query_level_data* level_data;
    // 顶点数组，任务完成后需要释放
    t_adj_count_query_vertex_info** vid_array;
    uint64_t                        vid_num;
} t_parallel_adj_count_task_arg;

// 并行邻接count查询的子任务线程
static void parallel_adj_count_sub_task_thread(t_parallel_adj_count_task_arg*);

// 为指定层级的为处理队列开启一个下级子任务，只有成功开启了下级子任务，此函数才返回。
static void start_one_parallel_adj_count_sub_task_thread(t_parallel_adj_count_query_level_data* level_data)
{
    t_parallel_adj_count_task_arg*         arg             = new t_parallel_adj_count_task_arg;
    t_parallel_adj_count_query_level_data* next_level_data = level_data->next_level_data;
    // 此任务所属层级是当前层级的下一层级
    arg->level_data = next_level_data;
    // 将当前层待处理队列放入任务参数中
    arg->vid_array = level_data->pending_vid_array;
    arg->vid_num   = level_data->pending_vid_num;

    // 重新分配未处理队列
    level_data->pending_vid_array = (t_adj_count_query_vertex_info**)malloc(sizeof(level_data->pending_vid_array[0]) *
                                                                            level_data->pending_vid_array_cap);
    level_data->pending_vid_num   = 0;

    // 获取下一层的执行资源，然后才能开启线程
    next_level_data->semaphore->acquire();
    next_level_data->task_wg->add();

    next_level_data->thread_pool->enqueue([arg]() { parallel_adj_count_sub_task_thread(arg); });
}

// 需要去重顶点的子任务完成函数。vid_array为其输出的顶点结果，vid_array_num 为顶点个数。hash_table_index
// 为这一批结果对应的hash table在vid_hash_table数组中的索引。
// vid_array
// 数组中的顶点对象转移到此函数内，因此，此函数调用者不需要释放vid_array数组内的顶点对象，只需要释放vid_array数组即可
static void parallel_adj_count_sub_task_complete_with_distinct(t_parallel_adj_count_query_level_data* level_data,
                                                               t_adj_count_query_vertex_info* const*  vid_array,
                                                               const t_uint64                         vid_array_num,
                                                               int                                    hash_table_index)
{
    std::unordered_set<VertexId>* vid_hash_table = level_data->vid_hash_table[hash_table_index];
    // 是否邻接count查询的最后一层
    const bool     is_last_level         = level_data->next_level_data == NULL;
    const t_uint64 pending_vid_array_cap = level_data->pending_vid_array_cap;
    t_uint64       i                     = 0;
    // pending_free数组用于记录临界区结束后需要释放的顶点结果，用于减少临界区大小
    t_array* pending_free = array_new(sizeof(t_adj_count_query_vertex_info*), vid_array_num);

    // 此完成函数会在多个子任务线程执行结束后调用，因此需要加锁
    // 这里对这一批数据对应的hash表对应的互斥锁进行加锁
    level_data->hash_table_mu[hash_table_index].lock();

    // 将这一批结果去重，并放入当前等待队列，队列满了就发起下一层任务
    for (; i < vid_array_num; i++, vid_array++)
    {
        t_adj_count_query_vertex_info* info_ptr = *vid_array;
        const t_uint64                 vid      = info_ptr->vid;

        const auto it = vid_hash_table->find(vid);
        // 查找当前层是否已经存在此顶点
        if (it != vid_hash_table->end())
        {
            array_push_back(pending_free, &info_ptr);
            continue;
        }

        vid_hash_table->insert(vid);

        if (info_ptr->path != NULL)
        {
            // 如果要去重，那么需要在这一层清除每一个结果的已有path，即以一个空的path重新开始执行后续的邻接查询
            info_ptr->path->clear();
        }

        level_data->vertext_counter.fetch_add(1);

        // 如果已经是最底层，则无需投递任务
        if (is_last_level)
        {
            array_push_back(pending_free, &info_ptr);
            continue;
        }

        level_data->mu.lock();
        level_data->pending_vid_array[level_data->pending_vid_num] = info_ptr;
        if (pending_vid_array_cap == ++level_data->pending_vid_num)
        {
            // 如果满了则发起下一层任务
            start_one_parallel_adj_count_sub_task_thread(level_data);
        }
        level_data->mu.unlock();
    }

    level_data->hash_table_mu[hash_table_index].unlock();

    {
        size_t pending_free_num = array_num(pending_free);
        i                       = 0;
        for (; i < pending_free_num; i++)
        {
            t_adj_count_query_vertex_info* info_ptr = *(t_adj_count_query_vertex_info* const*)array_at(pending_free, i);
            // 释放未添加到下一层任务的顶点结果
            free_adj_count_query_vertex_info(info_ptr);
        }
        array_free(pending_free);
        pending_free = NULL;
    }
}

// 每一个线程一个reader对象
thread_local ReaderInterface::Ptr adj_query_reader = nullptr;
static ReaderInterface::Ptr       make_adj_query_reader(WT_CONNECTION* conn)
{
    if (!adj_query_reader)
    {
        adj_query_reader = std::make_shared<OneTrxReaderWiredTiger>(conn);
    }
    return adj_query_reader;
}

static void release_adj_query_reader()
{
    adj_query_reader = nullptr;
}

// 表示某个子任务完成。vid_array为其输出的顶点结果，vid_array_num 为顶点个数。hash_table_index
// 为这一批结果对应的hash table在vid_hash_table数组中的索引。
// vid_array
// 数组中的顶点对象转移到此函数内，因此，此函数调用者不需要释放vid_array数组内的顶点对象，只需要释放vid_array数组即可
static void parallel_adj_count_sub_task_complete(t_parallel_adj_count_query_level_data* level_data,
                                                 t_adj_count_query_vertex_info* const*  vid_array,
                                                 const t_uint64                         vid_array_num,
                                                 int                                    hash_table_index)
{
    return parallel_adj_count_sub_task_complete_with_distinct(level_data, vid_array, vid_array_num, hash_table_index);
}

using t_uint16 = uint16_t;
using t_bool   = bool;
using t_uint32 = uint32_t;

// 获取一批定点的1度邻接点
// t_parallel_adj_count_query_level_data* level_data为层级
// vid_array 为输入的顶点
static void execute_parallel_adj_count_sub_task(t_parallel_adj_count_query_level_data* level_data,
                                                t_adj_count_query_vertex_info**        vid_array,
                                                t_uint64                               vid_num)
{
    // 保存查询到的一度邻接点数组，每一个hash
    // table对应一个数组成员（成员是一个顶点数组），当不需要去重时，数组实际的成员个数为1.
    t_array*       adj_vid_array_array[MAX_ADJ_COUNT_DISTINCT_HASH_SIZE];
    Edge           edge;
    const size_t   total_level           = (size_t)level_data->total_level_num;
    const t_bool   is_last_level         = level_data->next_level_data == NULL;
    const t_uint64 pending_vid_array_cap = level_data->pending_vid_array_cap;
    t_uint64       i                     = 0;
    t_uint64       current_pending_num   = 0;
    const int      hash_table_num        = level_data->hash_table_num;

    // 获取实际需要遍历的方向数组
    const EdgeDirection* direction_arr = NULL;
    int                  direction_num = 0;
    switch (level_data->direction)
    {
    case EdgeDirection::INCOMING:
    {
        static const EdgeDirection DIRECTION_ARRAY[] = {EdgeDirection::INCOMING};
        direction_arr                                = DIRECTION_ARRAY;
        direction_num                                = sizeof(DIRECTION_ARRAY) / sizeof(DIRECTION_ARRAY[0]);
    }
    break;
    case EdgeDirection::OUTGOING:
    {
        static const EdgeDirection DIRECTION_ARRAY[] = {EdgeDirection::OUTGOING};
        direction_arr                                = DIRECTION_ARRAY;
        direction_num                                = sizeof(DIRECTION_ARRAY) / sizeof(DIRECTION_ARRAY[0]);
    }
    break;
    case EdgeDirection::UNDIRECTED:
    {
        // 若cypher-server指定的方向为OP_DIRECTION_BOTH，则我们需要遍历N和OUT。
        static const EdgeDirection DIRECTION_ARRAY[] = {EdgeDirection::INCOMING, EdgeDirection::OUTGOING};
        direction_arr                                = DIRECTION_ARRAY;
        direction_num                                = sizeof(DIRECTION_ARRAY) / sizeof(DIRECTION_ARRAY[0]);
    }
    break;
    default:
        break;
        // do nothing
    }

    {
        int i = 0;
        for (; i < level_data->hash_table_num; i++)
        {
            adj_vid_array_array[i] = array_new(sizeof(t_adj_count_query_vertex_info*), vid_num);
        }
    }

    auto reader = make_adj_query_reader(level_data->query_arg->conn);

    const auto& relation_type_id_list = level_data->query_arg->relation_type_id_list;
    const auto& label_type_id_list    = level_data->query_arg->end_label_type_id_list;

    for (; i < vid_num; i++, vid_array++)
    {
        t_adj_count_query_vertex_info* vid_info = *vid_array;
        const VertexId                 vid      = vid_info->vid;

        // 是否发现了自己指向自己
        t_bool already_find_self       = false;
        auto   process_neighbors_edges = [&](const std::vector<Edge>& edges) {
            for (const auto& edge : edges)
            { // 检查是否存在环路，即此边的rowid是否已经存在于输入的顶点的路径之中
                if (edge_is_in_path(vid_info, edge))
                {
                    continue;
                }

                // haslabel 过滤
                if (!label_type_id_list.empty())
                {
                    bool found = false;
                    for (const auto& label_type_id : label_type_id_list)
                    {
                        if (label_type_id == edge.end_label_type_id)
                        {
                            found = true;
                        }
                    }
                    if (!found)
                    {
                        continue;
                    }
                }

                auto adj_vid = edge.end_vertex_id;
                if (adj_vid == vid)
                {
                    if (already_find_self)
                    {
                        // 无论用户请求的是否both方向，总之自己指向自己时，只能添加一次。
                        continue;
                    }
                    already_find_self = true;
                }

                // 创建一个邻接点结果对象并将其加入到此次子任务的结果数组中
                t_adj_count_query_vertex_info* info;
                if (is_last_level)
                {
                    // 如果当前是最后一层的任务，那么它所获取的邻接点其实就不需要产生路径了，因此只需要创建一个空的路径path的顶点即可
                    info = create_adj_count_query_vertex_info_with_empty_path();
                }
                else
                {
                    info = create_adj_count_query_vertex_info(total_level);
                    copy_adj_count_query_vertex_info_path(info, vid_info);
                    info->path->push_back(edge);
                }
                info->vid = adj_vid;

                array_push_back(adj_vid_array_array[adj_vid % hash_table_num], &info);
                current_pending_num++;
            }
        };

        for (int direction_index = 0; direction_index < direction_num; direction_index++)
        {
            const EdgeDirection direction = direction_arr[direction_index];
            t_uint64            row_id;
            t_uint64            adj_vid = 0;

            LabelTypeId dummy = 1;

            if (relation_type_id_list.empty())
            {
                auto neighbors_edges = reader->get_neighbors_by_start_vertex(vid, dummy, direction, std::nullopt);
                process_neighbors_edges(neighbors_edges);
            }
            else
            {
                for (const auto& relation_type_id : relation_type_id_list)
                {
                    auto neighbors_edges =
                        reader->get_neighbors_by_start_vertex(vid, dummy, direction, relation_type_id);
                    process_neighbors_edges(neighbors_edges);
                }
            }
        }

        // 立即释放这个起始顶点（减少内存占用），释放后需要修改*vid_array为NULL，调用者就知道不用释放它
        free_adj_count_query_vertex_info(vid_info);
        *vid_array = NULL;
        vid_info   = NULL;

        // 立即判断当前这一次一度邻接任务所获得的邻居数是否达到阈值，若是，则立即调用一次完成处理
        if (current_pending_num > pending_vid_array_cap)
        {
            for (int i = 0; i < hash_table_num; i++)
            {
                t_array* adj_vid_array = adj_vid_array_array[i];
                if (array_num(adj_vid_array) == 0)
                {
                    continue;
                }

                // 这一批获取完了之后，调用完成
                parallel_adj_count_sub_task_complete(level_data,
                                                     (t_adj_count_query_vertex_info* const*)array_at(adj_vid_array, 0),
                                                     array_num(adj_vid_array),
                                                     i);

                array_reset(adj_vid_array);
            }

            current_pending_num = 0;
        }
    }

    level_data->semaphore->release();

    for (int i = 0; i < hash_table_num; i++)
    {
        t_array* adj_vid_array = adj_vid_array_array[i];
        if (array_num(adj_vid_array) == 0)
        {
            continue;
        }
        // 这一批获取完了之后，调用完成
        parallel_adj_count_sub_task_complete(level_data,
                                             (t_adj_count_query_vertex_info* const*)array_at(adj_vid_array, 0),
                                             array_num(adj_vid_array),
                                             i);
        array_reset(adj_vid_array);
    }

    // 这里不用释放adj_vid_array_array数组里的每一个成员，因为其已经转移到了下一层
    {
        int i = 0;
        for (; i < hash_table_num; i++)
        {
            array_free(adj_vid_array_array[i]);
        }
    }
}

static void parallel_adj_count_sub_task_thread(t_parallel_adj_count_task_arg* task_arg)
{
    execute_parallel_adj_count_sub_task(task_arg->level_data, task_arg->vid_array, task_arg->vid_num);
    // 此次子任务完成后减少计数
    task_arg->level_data->task_wg->done();
    // 单次任务结束后释放此次子任务的输入顶点
    for (size_t i = 0; i < task_arg->vid_num; i++)
    {
        free_adj_count_query_vertex_info(task_arg->vid_array[i]);
    }
    delete (task_arg->vid_array);
    delete task_arg;
}

// 当子任务获取的一度邻接数量达到阈值时就要开启下一级的子任务
// 每个子任务最少累计的顶点数量
const int MIN_PENDING_VID_ARRAY_CAP = 5000;
// 每个子任务最多累计的顶点数量
const int MAX_PENDING_VID_ARRAY_CAP = 50000;

static t_parallel_adj_count_query_level_data* create_parallel_adj_count_query_level_data(QueryArg* query_arg,
                                                                                         int      pending_vid_array_cap,
                                                                                         t_bool   distinct,
                                                                                         t_uint32 parallel_num)
{
    t_parallel_adj_count_query_level_data* level_data = new t_parallel_adj_count_query_level_data;

    level_data->query_arg = query_arg;
    level_data->semaphore = new std::counting_semaphore<>(parallel_num);

    {
        int i                      = 0;
        level_data->hash_table_num = MAX_ADJ_COUNT_DISTINCT_HASH_SIZE;
        for (; i < level_data->hash_table_num; i++)
        {
            level_data->vid_hash_table[i] = new std::unordered_set<VertexId>();
        }
    }
    level_data->pending_vid_array_cap = pending_vid_array_cap;
    level_data->pending_vid_array = (t_adj_count_query_vertex_info**)malloc(sizeof(level_data->pending_vid_array[0]) *
                                                                            level_data->pending_vid_array_cap);
    level_data->pending_vid_num   = 0;
    level_data->task_wg           = new WaitGroup();
    level_data->parent_level_data = NULL;
    level_data->next_level_data   = NULL;

    level_data->vertext_counter = 0;
    level_data->total_level_num = query_arg->k;

    level_data->thread_pool = std::make_shared<ThreadPool>(parallel_num, []() { release_adj_query_reader(); });

    return level_data;
}

// 构造分层数据对象，返回最顶层的层级对象
static t_parallel_adj_count_query_level_data* create_multi_level_list(QueryArg* query_arg,
                                                                      int       adj_count_query_parallel_num)
{
    // 最顶层level data，也是第0层
    t_parallel_adj_count_query_level_data* const root_level_data =
        create_parallel_adj_count_query_level_data(query_arg,
                                                   MIN_PENDING_VID_ARRAY_CAP,
                                                   false,
                                                   adj_count_query_parallel_num);
    // 当前层level data
    t_parallel_adj_count_query_level_data* last_level_data = root_level_data;
    int                                    i               = 0;

    // 创建下面几层的leve data
    for (; i < query_arg->k; i++)
    {
        t_parallel_adj_count_query_level_data* level_data = create_parallel_adj_count_query_level_data(
            query_arg,
            std::min((i + 1) * MIN_PENDING_VID_ARRAY_CAP, MAX_PENDING_VID_ARRAY_CAP),
            true,
            adj_count_query_parallel_num);

        if (last_level_data != NULL)
        {
            last_level_data->next_level_data = level_data;
        }
        level_data->parent_level_data = last_level_data;
        level_data->direction         = query_arg->direction;

        last_level_data = level_data;
    }

    return root_level_data;
}

// 释放层级对象中去重的hash table，并返回其中的数量
static void free_level_data_vid_hash_table(t_parallel_adj_count_query_level_data* level_data)
{
    int vid_hash_table_index = 0;
    for (; vid_hash_table_index < level_data->hash_table_num; vid_hash_table_index++)
    {
        auto                  vid_hash_table = level_data->vid_hash_table[vid_hash_table_index];
        t_adj_count_vid_info* vid_info       = NULL;
        t_uint32              hash_n_cell    = 0;
        t_uint32              i              = 0;

        if (vid_hash_table == NULL)
        {
            continue;
        }

        delete vid_hash_table;
        level_data->vid_hash_table[vid_hash_table_index] = NULL;
    }
}

static void free_multi_level_list(t_parallel_adj_count_query_level_data* current)
{
    while (current != NULL)
    {
        t_parallel_adj_count_query_level_data* tmp = current;
        current                                    = current->next_level_data;

        delete tmp->task_wg;

        free_level_data_vid_hash_table(tmp);
        {
            t_uint64 i = 0;
            for (; i < tmp->pending_vid_num; i++)
            {
                t_adj_count_query_vertex_info* info_ptr = tmp->pending_vid_array[i];
                free_adj_count_query_vertex_info(info_ptr);
            }
        }
        delete tmp->semaphore;
        free(tmp->pending_vid_array);
        delete tmp;
    }
}

// 并行多度邻接COUNT查询
static t_uint64 parallel_adj_count_query(QueryArg* query_arg, int adj_count_query_parallel_num)
{
    // 构造分层数据对象
    t_parallel_adj_count_query_level_data* root_level_data =
        create_multi_level_list(query_arg, adj_count_query_parallel_num);

    const t_uint64 vid = query_arg->start_vertex_id;

    // 直接调用第0层完成，因为我们认为第0层输出的结果（顶点集合）就是vid。
    t_adj_count_query_vertex_info* info_ptr = create_adj_count_query_vertex_info(1);
    info_ptr->vid                           = vid;

    parallel_adj_count_sub_task_complete(root_level_data, (t_adj_count_query_vertex_info* const*)&info_ptr, 1, 0);

    t_uint64 adj_total_count = 0;
    {
        // 依次等待每一层结束
        // 只有当上层完成了，才能去等待下一层，因为只有上层完成了，才能确定下一层的任务数量（即wait
        // group计数），此时等待才是正确的。

        t_parallel_adj_count_query_level_data* current_wait_level = root_level_data;
        while (current_wait_level != NULL)
        {
            // 等待这一层级的任务全部完成
            current_wait_level->task_wg->wait();

            current_wait_level->mu.lock();

            // 这一层任务结束了，查看是否队列里还有剩余未处理的顶点
            if (current_wait_level->next_level_data != NULL && current_wait_level->pending_vid_num > 0)
            {
                start_one_parallel_adj_count_sub_task_thread(current_wait_level);
            }
            current_wait_level->mu.unlock();

            // 这一层结束，可以释放其缓冲的vid hash table，并获取其vid的数量
            // 要返回总数，并去掉最顶层的初始顶点
            if (root_level_data != current_wait_level)
            {
                adj_total_count += current_wait_level->vertext_counter.load();
            }
            free_level_data_vid_hash_table(current_wait_level);
            current_wait_level = current_wait_level->next_level_data;
        }
    }

    free_multi_level_list(root_level_data);

    return adj_total_count;
}

int KHopNeighborsCountAlgo::get_k_hop_neighbors_count(const VertexId&                    start_vertex_id,
                                                      EdgeDirection                      direction,
                                                      int                                k,
                                                      const std::vector<RelationTypeId>& relation_type_id_list,
                                                      const std::vector<LabelTypeId>&    end_label_type_id_list,
                                                      WT_CONNECTION*                     conn)
{
    QueryArg query_arg;
    query_arg.start_vertex_id        = start_vertex_id;
    query_arg.direction              = direction;
    query_arg.k                      = k;
    query_arg.relation_type_id_list  = relation_type_id_list;
    query_arg.end_label_type_id_list = end_label_type_id_list;
    query_arg.conn                   = conn;

    // 获取CPU核数
    int cpu_core_num = std::thread::hardware_concurrency();
    return parallel_adj_count_query(&query_arg, cpu_core_num);
}
