#pragma once

#include "interface/types/types.hpp"

// 写入数据的抽象接口
// 此接口较为低阶, 直接面向存储引擎, 不直接用于服务层
class WriterInterface
{
public:
    virtual ~WriterInterface() = default;

    // @brief 写入标签类型, 如果已经存在则返回已存在的标签类型ID
    // @param label_type 标签类型
    // @return 标签类型ID
    virtual LabelTypeId write_label_type(const LabelType& label_type) = 0;

    // @brief 写入关系类型, 如果已经存在则返回已存在的关系类型ID
    // @param relation_type 关系类型
    // @return 关系类型ID
    virtual RelationTypeId write_relation_type(const RelationType& relation_type) = 0;

    // @brief 写入顶点, 如果已经存在则返回已存在的顶点ID
    // @param label_type_id 标签类型ID
    // @param vertex_pk 顶点唯一标识
    // @return 顶点ID
    virtual VertexId write_vertex(const LabelTypeId& label_type_id, const VertexPk& vertex_pk) = 0;

    // @brief 写入边
    // @param relation_type_id 关系类型ID
    // @param start_label_type_id 起始标签类型ID
    // @param start_vertex_id 起始顶点ID
    // @param direction 边的方向
    // @param end_label_type_id 终点标签类型ID
    // @param end_vertex_id 终点顶点ID
    // @return 边ID
    virtual EdgeId  write_edge(RelationTypeId relation_type_id, LabelTypeId start_label_type_id, VertexId start_vertex_id, EdgeDirection direction, LabelTypeId end_label_type_id,
        VertexId end_vertex_id) = 0;
};