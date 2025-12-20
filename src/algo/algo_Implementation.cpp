#include "algo/algo_Implementation.hpp"

#include "interface/algo/algo.hpp"
#include "interface/types/types.hpp"
#include "k_hop_neighbors_count.hpp"

#include <vector>
#include <unordered_set>

uint64_t AlgoImpl::get_k_hop_neighbor_count(const KHopQueryParams&           params,
                                            std::shared_ptr<ReaderInterface> reader,
                                            WT_CONNECTION*                   conn)
{
    if (params.k <= 0 || params.vertex_id_list.empty())
    {
        return 0;
    }

    uint64_t total = 0;
    for (const auto& start_vertex_id : params.vertex_id_list)
    {
        if (params.direction == 1)
        {
            total += KHopNeighborsCountAlgo::get_k_hop_neighbors_count(start_vertex_id,
                                                                       EdgeDirection::OUTGOING,
                                                                       params.k,
                                                                       params.relation_label_type_id_list,
                                                                       params.node_label_type_id_list,
                                                                       conn);
        }
        else if (params.direction == 2)
        {
            total += KHopNeighborsCountAlgo::get_k_hop_neighbors_count(start_vertex_id,
                                                                       EdgeDirection::INCOMING,
                                                                       params.k,
                                                                       params.relation_label_type_id_list,
                                                                       params.node_label_type_id_list,
                                                                       conn);
        }
        else if (params.direction == 3)
        {

            total += KHopNeighborsCountAlgo::get_k_hop_neighbors_count(start_vertex_id,
                                                                       EdgeDirection::UNDIRECTED,
                                                                       params.k,
                                                                       params.relation_label_type_id_list,
                                                                       params.node_label_type_id_list,
                                                                       conn);
        }
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

int AlgoImpl::get_wcc_count(const WCCParams& params, std::shared_ptr<ReaderInterface> reader)
{
    return 0;
}

int AlgoImpl::get_subgraph_matching_count(const SubgraphMatchingParams& params, std::shared_ptr<ReaderInterface> reader)
{
    return 0;
}

std::shared_ptr<AlgoInterface> create_algo()
{
    return std::make_shared<AlgoImpl>();
}