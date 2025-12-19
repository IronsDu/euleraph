#include "interface/algo/algo.hpp"
#include "algo/algo_Implementation.hpp"
#include "interface/types/types.hpp"
#include <vector>
#include <unordered_set>

int AlgoImpl::get_k_hop_neighbor_count(const KHopQueryParams& params, std::shared_ptr<ReaderInterface> reader)
{
    return 0;
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
        if (params.relation_label_type_id_list.empty())
        {
            auto edges = reader->get_neighbors_by_start_vertex(vid, 0, EdgeDirection::UNDIRECTED, std::nullopt);
            for (const auto& e : edges)
            {
                neighbors.insert(e.end_vertex_id);
            }
        }
        else
        {
            for (const auto& r : params.relation_label_type_id_list)
            {
                auto edges = reader->get_neighbors_by_start_vertex(vid, 0, EdgeDirection::UNDIRECTED, r);
                for (const auto& e : edges)
                {
                    neighbors.insert(e.end_vertex_id);
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

std::shared_ptr<AlgoInterface> create_algo() {
    return std::make_shared<AlgoImpl>();
}