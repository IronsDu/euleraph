#pragma once

#include <memory>
#include <vector>
#include "interface/types/types.hpp"

// 写入数据的抽象接口
// 此接口较为低阶, 直接面向存储引擎, 不直接用于服务层
class WriterInterface
{
public:
    using Ptr = std::shared_ptr<WriterInterface>;

    // 定义写入接口所需的顶点结构体
    struct Vertex
    {
        LabelTypeId label_type_id; // 标签类型ID
        VertexPk    vertex_pk;     // 顶点唯一标识
    };

    // 定义写入接口所需的边结构体
    struct Edge
    {
        RelationTypeId relation_type_id;    // 关系类型ID
        LabelTypeId    start_label_type_id; // 起始标签类型ID
        VertexId       start_vertex_id;     // 起始顶点ID
        EdgeDirection  direction;           // 边的方向
        LabelTypeId    end_label_type_id;   // 终点标签类型ID
        VertexId       end_vertex_id;       // 终点顶点ID
    };

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
    // @param vertice 顶点
    // @return 顶点ID
    virtual VertexId write_vertex(const Vertex& vertice) = 0;

    // @brief 批量写入顶点, 如果已经存在则返回已存在的顶点ID
    // @param vertices 顶点列表
    // @return 顶点ID列表, 顺序与输入顶点列表一致
    virtual std::vector<VertexId> write_vertices(const std::vector<Vertex>& vertices) = 0;

    // @brief 写入边
    // @param edge 边
    virtual void write_edge(const Edge& edge) = 0;

    // @brief 批量写入边
    // @param edges 边列表
    virtual void write_edges(const std::vector<Edge>& edges) = 0;
};