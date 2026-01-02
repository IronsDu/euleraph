#include "algo/algo_Implementation.hpp"

#include "interface/algo/algo.hpp"
#include "interface/types/types.hpp"
#include "k_hop_neighbors_count.hpp"

#include <vector>
#include <unordered_set>
#include <bits/stdc++.h>

#include <string>
#include <unordered_map>
#include <algorithm>
#include <functional>

uint64_t AlgoImpl::get_k_hop_neighbor_count(const KHopQueryParams&           params,
                                            std::shared_ptr<ReaderInterface> reader,
                                            WT_CONNECTION*                   conn)
{
    if (params.k <= 0 || params.vertex_id_list.empty())
    {
        return 0;
    }

    uint64_t total = 0;
    if (params.direction == 1)
    {
        total = KHopNeighborsCountAlgo::get_k_hop_neighbors_count(params.vertex_id_list,
                                                                  EdgeDirection::OUTGOING,
                                                                  params.k,
                                                                  params.relation_label_type_id_list,
                                                                  params.node_label_type_id_list,
                                                                  conn);
    }
    else if (params.direction == 2)
    {
        total = KHopNeighborsCountAlgo::get_k_hop_neighbors_count(params.vertex_id_list,
                                                                  EdgeDirection::INCOMING,
                                                                  params.k,
                                                                  params.relation_label_type_id_list,
                                                                  params.node_label_type_id_list,
                                                                  conn);
    }
    else if (params.direction == 3)
    {
        total = KHopNeighborsCountAlgo::get_k_hop_neighbors_count(params.vertex_id_list,
                                                                  EdgeDirection::UNDIRECTED,
                                                                  params.k,
                                                                  params.relation_label_type_id_list,
                                                                  params.node_label_type_id_list,
                                                                  conn);
    }
    return total;
}

int AlgoImpl::get_common_neighbor_count(const CommonNeighborQueryParams& params,
                                        std::shared_ptr<ReaderInterface> reader)
{
    if (params.vertex_id_list.size() <= 1)
        return 0;

    params.relation_label_type_id_list;
    // 为每个点获取邻居，并存成哈希集合
    std::vector<std::unordered_set<VertexId>> neighbor_sets;
    for (const auto& vid : params.vertex_id_list)
    {
        // 查找当前点的邻居点集合
        std::unordered_set<VertexId> neighbors;
        std::unordered_set<VertexId> outgoing_neighbors;
        if (params.relation_label_type_id_list.empty())
        {
            auto edges_outgoing = reader->get_neighbors_by_start_vertex(vid, 0, EdgeDirection::OUTGOING, std::nullopt);
            for (const auto& e_o : edges_outgoing)
            {
                outgoing_neighbors.insert(e_o.end_vertex_id);
            }
            auto edges_incoming = reader->get_neighbors_by_start_vertex(vid, 0, EdgeDirection::INCOMING, std::nullopt);
            for (const auto& e_i : edges_incoming)
            {
                if (outgoing_neighbors.count(e_i.end_vertex_id))
                {
                    neighbors.insert(e_i.end_vertex_id);
                }
            }
        }
        else
        {
            for (const auto& r : params.relation_label_type_id_list)
            {
                auto edges_outgoing = reader->get_neighbors_by_start_vertex(vid, 0, EdgeDirection::OUTGOING, r);
                for (const auto& e_o : edges_outgoing)
                {
                    outgoing_neighbors.insert(e_o.end_vertex_id);
                }
                auto edges_incoming = reader->get_neighbors_by_start_vertex(vid, 0, EdgeDirection::INCOMING, r);
                for (const auto& e_i : edges_incoming)
                {
                    if (outgoing_neighbors.count(e_i.end_vertex_id))
                    {
                        neighbors.insert(e_i.end_vertex_id);
                    }
                }
            }
        }

        neighbor_sets.push_back(std::move(neighbors));
    }

    // 找到邻居最少的索引
    size_t min_idx = 0;
    for (size_t i = 1; i < neighbor_sets.size(); ++i)
    {
        if (neighbor_sets[i].size() < neighbor_sets[min_idx].size())
        {
            min_idx = i;
        }
    }

    // 以 min_idx 的邻居为查询起点
    std::unordered_set<VertexId> candidates = std::move(neighbor_sets[min_idx]);

    // 用其他点的邻居集合来过滤
    for (size_t i = 0; i < neighbor_sets.size(); ++i)
    {
        if (i == min_idx)
            continue; // 跳过自己
        if (candidates.empty())
            break; // 提前退出

        std::unordered_set<VertexId> new_candidates;
        for (const auto& candidate : candidates)
        {
            // 如果这个点也在第 i 个点的邻居中则保留
            if (neighbor_sets[i].count(candidate))
            {
                new_candidates.insert(candidate);
            }
        }
        candidates = std::move(new_candidates);
    }

    return candidates.size();
}

// ============================================================
// SortedFilter：小集合过滤器（排序 + 去重 + binary_search）
// 设计目的：
//   - n_labels / r_labels 通常是小集合（几十以内），用 vector + 二分比 unordered_set 更省内存且更 cache-friendly
// 语义：
//   - keys_ 为空 => 不过滤（contains 恒 true）
// 复杂度：
//   - 构建：O(k log k)（k 为输入数量）
//   - 查询：O(log k)
// 注意：
//   - T 一般是整数型 ID（LabelTypeId/RelationTypeId），传值 contains(T x) 没问题
// ============================================================

template <typename T> class SortedFilter
{
public:
    SortedFilter() = default; // 默认：keys_ 为空，表示不过滤

    explicit SortedFilter(const std::vector<T>& input) // 输入可能重复、无序
        : keys_(input)                                 // 拷贝到内部 keys_
    {
        std::sort(keys_.begin(), keys_.end());                             // 排序，便于 unique 和二分
        keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end()); // 去重：保持 keys_ 内元素唯一
    }

    inline bool contains(T x) const // 判断 x 是否在 keys_ 中（空 keys_ => 恒 true）
    {
        if (keys_.empty())                                        // 空列表：不过滤
            return true;                                          // 赛题接口约定：空数组表示不过滤
        return std::binary_search(keys_.begin(), keys_.end(), x); // 二分判断存在性
    }

    inline size_t size() const // 获取过滤器中 key 数量
    {
        return keys_.size();
    }

    inline const std::vector<T>& keys() const // 只读访问 keys_（比如 size==1 时取出唯一值用于下推）
    {
        return keys_;
    }

private:
    std::vector<T> keys_; // 排序去重后的 key 集合
};

// ============================================================
// PagedArray：分页数组（按块分配）
// 设计目的：
//   - 不需要提前知道顶点总数 v_cnt
//   - 允许“按需”扩展到某个 idx
//   - 块内连续内存，访问开销接近普通数组
// 关键实现：
//   - 块号 b = idx >> SHIFT
//   - 块内偏移 o = idx & (2^SHIFT - 1)
// 注意（非常关键）：
//   - ensure(idx) 会把 blocks_ 从 0..b 全部补齐（vector 顺序块）
//   - 若 VertexId 极端稀疏且跨度巨大（比如直接出现一个很大的 idx），可能导致大量空块分配 => 内存灾难
//   - 适合 VertexId 基本连续或跨度不离谱的场景（如 0..N-1）
// ============================================================

template <typename T, uint32_t SHIFT = 20> // SHIFT=20 => 每块 1,048,576 元素；int32 块约 4MB
class PagedArray
{
public:
    static constexpr uint64_t BLOCK_ELEMS = (1ULL << SHIFT); // 每块元素数量
    static constexpr uint64_t MASK        = BLOCK_ELEMS - 1; // 块内 offset 掩码

    explicit PagedArray(T default_value) : default_(default_value) // default_ 用于初始化新块
    {
    }

    inline void ensure(uint64_t idx) // 确保 idx 所属块存在：会把 blocks_ 补齐到 b
    {
        const uint64_t b = (idx >> SHIFT); // idx 所在块号
        while (blocks_.size() <= b)        // 不够则持续追加块
        {
            auto blk = std::make_unique<T[]>(BLOCK_ELEMS);           // 分配新块（数组）
            std::fill(blk.get(), blk.get() + BLOCK_ELEMS, default_); // 块内填充默认值（例如 DSU 的 kInactive）
            blocks_.push_back(std::move(blk));                       // 存入 blocks_
        }
    }

    inline bool in_range(uint64_t idx) const // idx 的块是否已分配（不触发分配）
    {
        const uint64_t b = (idx >> SHIFT); // 块号
        return b < blocks_.size();         // 块号在 blocks_ 范围内即可
    }

    inline T& at(uint64_t idx) // 返回可写引用（调用方需保证 ensure 过）
    {
        const uint64_t b = (idx >> SHIFT); // 块号
        const uint64_t o = (idx & MASK);   // 块内偏移
        return blocks_[b][o];              // 直接访问
    }

    inline T at(uint64_t idx) const // const 版本，按值返回
    {
        const uint64_t b = (idx >> SHIFT); // 块号
        const uint64_t o = (idx & MASK);   // 块内偏移
        return blocks_[b][o];              // 返回值
    }

private:
    T                                 default_; // 新块填充值
    std::vector<std::unique_ptr<T[]>> blocks_;  // 块指针数组：blocks_[b] 指向第 b 块
};

// ============================================================
// DSU（并查集）
// 存储结构：
//   - parent_[x] == kInactive：节点 x 未激活（不参与 DSU）
//   - parent_[x] < 0：x 是根节点，且 -parent_[x] 表示集合大小
//   - parent_[x] >= 0：x 的父节点为 parent_[x]（int32 存储）
// 优化策略：
//   - union-by-size：把小集合挂到大集合上（减少树高）
//   - path compression：find 过程中压缩路径
// 注意：
//   - parent 使用 int32 存父指针，因此 VertexId 需 < INT32_MAX
//   - 当前实现不是线程安全的（scan 并行时需外部保证互斥/分片）
// ============================================================

class DSU
{
public:
    static constexpr int32_t kInactive = INT32_MIN; // 未激活标记

    DSU() : parent_(kInactive) // 初始化 PagedArray：默认值为 kInactive
    {
    }

    inline bool is_active(VertexId x) const // 判断 x 是否已激活并可访问
    {
        // int32 父指针限制：若 VertexId 超过 int32 可表示范围，会溢出/越界
        if (x >= static_cast<VertexId>(INT32_MAX))
            return false;

        // 如果 idx 所属块未分配，说明从未触达该 x => 未激活
        if (!parent_.in_range(x))
            return false;

        // 块已存在：只要 parent[x] != kInactive 就视为激活
        return parent_.at(x) != kInactive;
    }

    inline void activate(VertexId x) // 将节点 x 加入 DSU，初始化为一个新集合（size=1）
    {
        if (x >= static_cast<VertexId>(INT32_MAX)) // 同样的 int32 安全检查
            return;

        parent_.ensure(x);  // 分配到能容纳 idx 的块
        parent_.at(x) = -1; // 根节点，集合大小 1
    }

    inline VertexId find(VertexId x) // 查找根节点 + 路径压缩
    {
        VertexId r = x;            // r 从 x 开始向上爬
        while (parent_.at(r) >= 0) // 只要 parent[r] 是父指针（>=0），就继续向上
        {
            r = static_cast<VertexId>(parent_.at(r)); // r 指向父节点
        }

        // 第二段：把 x 到根 r 的路径压缩（使后续 find 更快）
        while (x != r)
        {
            VertexId p    = static_cast<VertexId>(parent_.at(x)); // 保存 x 的父节点
            parent_.at(x) = static_cast<int32_t>(r);              // 直接指向根
            x             = p;                                    // 继续处理原来的父节点
        }
        return r; // 返回根
    }

    // 返回 true 表示发生合并（用于 components--）
    inline bool unite(VertexId a, VertexId b) // 合并 a 与 b 所在集合
    {
        // 只对已激活节点做合并：未激活节点不参与统计
        if (!is_active(a) || !is_active(b))
            return false;

        VertexId ra = find(a); // a 的根
        VertexId rb = find(b); // b 的根
        if (ra == rb)          // 已在同一集合
            return false;

        int32_t sa = parent_.at(ra); // 根节点处存负 size
        int32_t sb = parent_.at(rb); // 根节点处存负 size

        // union-by-size：size 更大的集合，其负数值更小（更“负”）
        // 例：-10 表示大小 10，-3 表示大小 3；-3 > -10，因此 sa > sb 表示 ra 更小
        if (sa > sb)
        {
            std::swap(ra, rb); // 确保 ra 是更大的集合根
            std::swap(sa, sb);
        }

        parent_.at(ra) = sa + sb;                  // 更新 ra 的 size（负数相加）
        parent_.at(rb) = static_cast<int32_t>(ra); // rb 挂到 ra 下
        return true;                               // 合并成功
    }

private:
    PagedArray<int32_t> parent_; // parent 数组：分页存储，节省未知规模时的初始化成本
};

// ============================================================
// get_wcc_count：弱连通分量数（WCC）
// 口径说明：
//   - 输入可选 n_labels / r_labels（为空则不过滤）
//   - “诱导子图”：仅统计标签满足过滤条件的节点集合及其内部边
//   - “弱连通”：忽略方向，遇到有向边 u->v 就 union(u,v)
// 优化点：
//   - components 在线维护：激活新点 +1；合并成功 -1；避免最后遍历所有点统计根
//   - r_labels 只有一个值时下推给 reader：减少返回边量（I/O 与 CPU）
// 性能关键：
//   - 若 reader->get_neighbors_by_start_vertex 每次返回 vector 且发生频繁分配，极大规模下会成为主要瓶颈
// ============================================================

int AlgoImpl::get_wcc_count(const WCCParams& params, std::shared_ptr<ReaderInterface> reader)
{
    /// 1) 构建过滤器（空列表 => 不过滤）
    const SortedFilter<LabelTypeId>    node_filter(params.label_type_id_list);         // 节点标签过滤器
    const SortedFilter<RelationTypeId> rel_filter(params.relation_label_type_id_list); // 边标签过滤器

    // 2) r_labels 单值时尝试下推给 reader，减少返回边量
    std::optional<RelationTypeId> relation_pushdown = std::nullopt; // 默认：不下推
    if (rel_filter.size() == 1)                                     // 若只有一个边类型
    {
        relation_pushdown = rel_filter.keys()[0]; // 下推该唯一边类型 ID
    }

    DSU      dsu;            // 并查集：维护连通关系
    uint64_t components = 0; // 分量计数器（在线维护）

    // 3) 单次扫描：在线激活顶点 + 在线 union + 在线维护 components
    reader->scan_vertex_id([&](VertexId u, LabelTypeId u_lab) { // 扫描每个顶点 (u, u_lab)
        // u 不在允许节点集合：不参与统计，也不处理其边（诱导子图口径）
        if (!node_filter.contains(u_lab))
            return; // 跳过：既不计入孤点，也不 union 边

        // 激活 u（确保孤点也会计入一个分量）
        if (!dsu.is_active(u)) // u 第一次出现
        {
            dsu.activate(u); // 让 u 成为一个单点集合
            ++components;    // 新增一个分量（之后可能因 union 被合并）
        }

        // WCC 忽略方向：只需要处理每条有向边一次，因此取 OUTGOING 即可
        // 重要前提：reader 的 OUTGOING 能覆盖所有边（每条边至少被某个起点扫描到一次）
        auto edges = reader->get_neighbors_by_start_vertex( // 获取 u 的出边集合
            u,                                              // 起点
            u_lab,                                          // 起点标签（可能参与存储索引）
            EdgeDirection::OUTGOING,                        // 只扫描出边
            relation_pushdown);                             // 可选：下推单一关系类型

        for (const Edge& e : edges) // 遍历每条边
        {
            // 边类型过滤（若 pushdown 生效，这里几乎总是 true，但保留防御）
            if (!rel_filter.contains(e.relation_type_id))
                continue; // 跳过不允许的边类型

            // 诱导子图：终点也必须属于允许节点集合（用边上 end_label_type_id 直接判断）
            if (!node_filter.contains(e.end_label_type_id))
                continue; // 终点不允许 => 此边不参与，且终点不激活

            const VertexId v = e.end_vertex_id; // 终点 ID

            // 自环无意义，跳过
            if (v == u)
                continue; // u 与 u union 无意义

            // 按需激活 v（即使 v 还没 scan 到也没关系）
            if (!dsu.is_active(v)) // v 可能没在 scan_vertex_id 中出现过
            {
                dsu.activate(v); // 激活 v
                ++components;    // 新增一个分量（稍后若 union 成功会 -1）
            }

            // 合并成功才减少分量数
            if (dsu.unite(u, v)) // 若 u 与 v 原本不连通
            {
                --components; // 合并两个分量 => 分量数 -1
            }
        }
    });

    return components; // 返回最终弱连通分量数
}

int AlgoImpl::get_subgraph_matching_count(const SubgraphMatchingParams& params, std::shared_ptr<ReaderInterface> reader)
{
    // 1. 提取变量名
    std::vector<std::string> var_names;
    for (const auto& kv : params.nodes_pattern_map)
    {
        var_names.push_back(kv.first);
    }

    std::unordered_map<std::string, std::vector<VertexId>> candidates;
    std::unordered_map<VertexId, LabelTypeId>              candidate_labels;

    // 2. 候选集过滤
    reader->scan_vertex_id([&](VertexId vid, LabelTypeId vlabel) {
        for (const auto& [var, required_labels] : params.nodes_pattern_map)
        {
            for (LabelTypeId req : required_labels)
            {
                if (vlabel == req)
                {
                    candidates[var].push_back(vid);
                    candidate_labels[vid] = vlabel;
                    break;
                }
            }
        }
    });

    // 3. MRV 排序：先匹配候选集小的节点以缩减搜索空间
    std::sort(var_names.begin(), var_names.end(), [&](const std::string& a, const std::string& b) {
        return candidates[a].size() < candidates[b].size();
    });

    std::unordered_map<std::string, int> var_to_idx;
    for (int i = 0; i < (int)var_names.size(); ++i)
    {
        var_to_idx[var_names[i]] = i;
    }

    // 4. 预处理：每个深度需要验证的边
    struct EdgeTask
    {
        std::string other_var;
        PatternEdge pattern;
        bool        is_src;
    };
    std::vector<std::vector<EdgeTask>> edges_at_depth(var_names.size());

    for (const auto& edge : params.edges_pattern_list)
    {
        int idx1    = var_to_idx[edge.source_node];
        int idx2    = var_to_idx[edge.target_node];
        int max_idx = std::max(idx1, idx2);

        // 标记当前节点在边中的角色，确保调用 get_neighbors 时 u 是起点，v 是终点
        if (idx1 == max_idx)
        {
            edges_at_depth[max_idx].push_back({edge.target_node, edge, true});
        }
        else
        {
            edges_at_depth[max_idx].push_back({edge.source_node, edge, false});
        }
    }

    // 5. 回溯搜索 (同态逻辑)
    long long                                 total = 0;
    std::unordered_map<std::string, VertexId> current_mapping;

    std::function<void(int)> backtrack = [&](int depth) {
        if (depth == static_cast<int>(var_names.size()))
        {
            total++;
            return;
        }

        const std::string& var = var_names[depth];
        for (VertexId vid : candidates[var])
        {
            // 【注意】这里删除了 used_vids.find 检查
            // 子图同态允许不同的 var 映射到同一个物理 vid

            bool consistent = true;
            for (const auto& task : edges_at_depth[depth])
            {
                // 确定物理边起点 u 和终点 v
                VertexId u = task.is_src ? vid : current_mapping[task.other_var];
                VertexId v = task.is_src ? current_mapping[task.other_var] : vid;

                bool edge_found = false;
                for (RelationTypeId rel_id : task.pattern.relation_type_id_list)
                {
                    // 仅支持 OUT 的 get_neighbors 调用逻辑
                    auto neighbors =
                        reader->get_neighbors_by_start_vertex(u, candidate_labels[u], task.pattern.direction, rel_id);

                    for (const auto& neighbor : neighbors)
                    {
                        if (neighbor.end_vertex_id == v)
                        {
                            edge_found = true;
                            break;
                        }
                    }
                    if (edge_found)
                        break;
                }

                if (!edge_found)
                {
                    consistent = false;
                    break;
                }
            }

            if (consistent)
            {
                current_mapping[var] = vid;
                backtrack(depth + 1);
            }
        }
    };

    backtrack(0);
    return static_cast<int>(total);
}

uint64_t
AlgoImpl::get_adj_count(const AdjCountQueryParams& params, std::shared_ptr<ReaderInterface> reader, WT_CONNECTION* conn)
{
    if (params.k <= 0 || params.vertex_id_list.empty())
    {
        return 0;
    }

    uint64_t total = 0;
    if (params.direction == 1)
    {
        total = KHopNeighborsCountAlgo::get_adj_count(params.vertex_id_list,
                                                      EdgeDirection::OUTGOING,
                                                      params.k,
                                                      params.relation_label_type_id_list,
                                                      params.node_label_type_id_list,
                                                      params.need_distinct,
                                                      conn);
    }
    else if (params.direction == 2)
    {
        total = KHopNeighborsCountAlgo::get_adj_count(params.vertex_id_list,
                                                      EdgeDirection::INCOMING,
                                                      params.k,
                                                      params.relation_label_type_id_list,
                                                      params.node_label_type_id_list,
                                                      params.need_distinct,
                                                      conn);
    }
    else if (params.direction == 3)
    {
        total = KHopNeighborsCountAlgo::get_adj_count(params.vertex_id_list,
                                                      EdgeDirection::UNDIRECTED,
                                                      params.k,
                                                      params.relation_label_type_id_list,
                                                      params.node_label_type_id_list,
                                                      params.need_distinct,
                                                      conn);
    }
    return total;
}

std::shared_ptr<AlgoInterface> create_algo()
{
    return std::make_shared<AlgoImpl>();
}