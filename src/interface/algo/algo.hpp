#pragma once

#include <vector>
#include "interface/types/types.hpp"
#include "interface/storage/reader.hpp"
#include <memory>
#include <unordered_map>

struct KHopQueryParams {
    int k = 0; // 跳数（K ≥ 1）
    int direction = 0; // 遍历方向：1:出向；2:入向；3:双向/无向
    std::vector<VertexId> vertex_id_list; // 起始节点 ID 列表
    std::vector<LabelTypeId> node_label_type_id_list; // 节点标签（可选）
    std::vector<RelationTypeId> relation_label_type_id_list; // 边标签（可选）
};

struct CommonNeighborQueryParams {
    std::vector<VertexId> vertex_id_list; // 起始节点 ID 列表
    std::vector<RelationTypeId> relation_label_type_id_list; // 边标签（可选）
};

struct WCCParams {
    std::vector<LabelTypeId> label_type_id_list; // 节点标签（可选）
    std::vector<RelationTypeId> relation_label_type_id_list; // 边标签（可选）
};

struct PatternEdge
{
    std::string source_node;
    std::string target_node;
    std::vector<RelationTypeId> relation_type_id_list; // 关系类型ID数组
    EdgeDirection direction; // 边的方向
};
struct SubgraphMatchingParams {
    std::unordered_map<std::string, std::vector<LabelTypeId>> nodes_pattern_map;
    std::vector<PatternEdge> edges_pattern_list;
};

class AlgoInterface
{
public:
    virtual ~AlgoInterface() = default;

    // @brief K度邻居算法，根据输入的vertex_id_list，获取K度邻居的总数
    // @param 见KHopQueryParams
    // @return k度邻居总数
    virtual int get_k_hop_neighbor_count(const KHopQueryParams& params, std::shared_ptr<ReaderInterface> reader) = 0;

    // @brief 共同邻居算法，根据输入的vertex_id_list，获取共同邻居的总数
    // @param 见CommonNeighborQueryParams
    // @return 共同邻居总数
    virtual int get_common_neighbor_count(const CommonNeighborQueryParams& params, std::shared_ptr<ReaderInterface> reader) = 0;

    // @brief 连通分量算法，计算连通分量，根据输入的label_type和relation_type过滤
    // @param 见WCCParams
    // @return 连通分量总数
    virtual int get_wcc_count(const WCCParams& params, std::shared_ptr<ReaderInterface> reader) = 0;

    // @brief 子图匹配(子图同态)算法，根据输入的一组节点列表，返回匹配的子图实例总数
    // @param 见SubgraphMatchingParams
    // @return 匹配的子图实例总数
    virtual int get_subgraph_matching_count(const SubgraphMatchingParams& params, std::shared_ptr<ReaderInterface> reader) = 0;
};

std::shared_ptr<AlgoInterface> create_algo();