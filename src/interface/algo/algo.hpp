#pragma once

#include <vector>
#include "interface/types/types.hpp"

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

class AlgoInterface
{
public:
    virtual ~AlgoInterface() = default;

    // @brief K度邻居算法，根据输入的vertex_id_list，获取K度共同邻居的总数
    // @param 见KHopQueryParams
    // @return k度共同邻居总数
    virtual int get_k_hop_neighbor_count(const KHopQueryParams& params) = 0;

    // @brief 共同邻居算法，根据输入的vertex_id_list，获取共同邻居的总数
    // @param 见CommonNeighborQueryParams
    // @return 共同邻居总数
    virtual int get_common_neighbor_count(const KHopQueryParams& params) = 0;
};