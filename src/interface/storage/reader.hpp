#pragma once

#include <optional>
#include <vector>

#include "interface/types/types.hpp"

// 查询数据的抽象接口
class ReaderInterface
{
public:
    virtual ~ReaderInterface() = default;

    // @brief 根据标签类型获取标签类型ID
    // @param label_type 标签类型
    // @return 标签类型ID, 如果不存在则返回std::nullopt
    virtual std::optional<LabelTypeId> get_label_type_id(const LabelType& label_type) = 0;

    // @brief 根据关系类型获取关系类型ID
    // @param relation_type 关系类型
    // @return 关系类型ID, 如果不存在则返回std::nullopt
    virtual std::optional<RelationTypeId> get_relation_type_id(const RelationType& relation_type) = 0;

    // @brief 根据标签类型ID和顶点唯一标识获取顶点ID
    // @param label_type_id 标签类型ID
    // @param vertex_pk 顶点唯一标识
    // @return 顶点ID, 如果不存在则返回std::nullopt
    virtual std::optional<VertexId> get_vertex_id(const LabelTypeId& label_type_id, const VertexPk& vertex_pk) = 0;

    // @brief 根据起始顶点ID获取邻居边
    // @param start_vertex_id 起始顶点ID
    // @param direction 边的方向
    // @param relation_type_id 可选的关系类型ID, 如果提供则只返回该关系类型的邻居边
    virtual std::vector<Edge> get_neighbors_by_start_vertex(const VertexId&               start_vertex_id,
                                                            EdgeDirection                 direction,
                                                            std::optional<RelationTypeId> relation_type_id) = 0;
};