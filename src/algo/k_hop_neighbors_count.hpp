#pragma once

#include <wiredtiger.h>

#include "interface/storage/reader.hpp"
#include "interface/types/types.hpp"

// 实现k度邻居数量查询算法

class KHopNeighborsCountAlgo
{
public:
    // @brief 获取k度邻居数量
    // @param start_vertex_id 起始顶点ID
    // @param direction 边的方向，如果为无向边则表示同时查询入边和出边
    // @param k 度数
    // @param relation_type_id_list 关系类型ID列表, 如果为空则表示不过滤关系类型
    // @param end_label_type_id_list 终点标签类型ID列表, 如果为空则表示不过滤终点标签类型
    static int get_k_hop_neighbors_count(const VertexId&                    start_vertex_id,
                                         EdgeDirection                      direction,
                                         int                                k,
                                         const std::vector<RelationTypeId>& relation_type_id_list,
                                         const std::vector<LabelTypeId>&    end_label_type_id_list,
                                         WT_CONNECTION*                     conn);
};