#pragma once

#include <unordered_set>
#include <wiredtiger.h>

#include "interface/storage/reader.hpp"
#include "interface/types/types.hpp"

// 实现k度邻居数量查询算法

class KHopNeighborsCountAlgo
{
public:
    // @brief 获取k度邻居数量
    // @param vertex_id_list 起点顶点ID列表
    // @param direction 边的方向，如果为无向边则表示同时查询入边和出边
    // @param k 度数
    // @param relation_type_id_list 关系类型ID列表, 如果为空则表示不过滤关系类型
    // @param end_label_type_id_list 终点标签类型ID列表, 如果为空则表示不过滤终点标签类型
    static int get_k_hop_neighbors_count(const std::vector<VertexId>&           vertex_id_list,
                                         EdgeDirection                          direction,
                                         int                                    k,
                                         const std::vector<RelationTypeId>&     relation_type_id_list,
                                         const std::unordered_set<LabelTypeId>& end_label_type_id_list,
                                         WT_CONNECTION*                         conn);

    static uint64_t get_adj_count(const std::vector<VertexId>&       vertex_id_list,
                                  EdgeDirection                      direction,
                                  int                                k,
                                  const std::vector<RelationTypeId>& relation_type_id_list,
                                  const std::vector<LabelTypeId>&    end_label_type_id_list,
                                  bool                               distinct,
                                  WT_CONNECTION*                     conn);
};