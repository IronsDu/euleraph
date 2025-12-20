#pragma once

#include <cstdint>
#include <string>

// 标签类型(名称), 用户可见, 全局唯一
using LabelType = std::string;
// 标签类型ID, 内部使用, 全局唯一
using LabelTypeId = std::uint64_t;

// 关系类型, 用户可见, 全局唯一
using RelationType = std::string;
// 关系类型ID, 内部使用, 全局唯一
using RelationTypeId = std::uint64_t;

// 顶点唯一标识, 用户可见, 标签类型内唯一, 不同标签类型下的顶点标识可以相同
using VertexPk = std::string;
// 顶点ID, 内部使用, 全局唯一
using VertexId = std::uint64_t;

// 边ID, 内部使用, 全局唯一
using EdgeId = std::uint64_t;

// 边的方向枚举
// 实际存储中, 边是有向的
// 当写入无向边时, 需要为起点和终点各写入一条有向边, 起点为出边, 终点为入边
// 当查询无向边时, 需要同时查询起点的入边和出边
enum class EdgeDirection : uint8_t
{
    // 出边
    OUTGOING = 1,
    // 入边
    INCOMING,
    // 无向边
    UNDIRECTED
};

// 边结构体, 表示图中的一条边
struct Edge
{
    // 关系类型ID
    RelationTypeId relation_type_id;
    // 起始标签类型ID
    LabelTypeId start_label_type_id;
    // 起始顶点ID
    VertexId start_vertex_id;
    // 边的方向
    EdgeDirection direction;
    //  终点标签类型ID
    LabelTypeId end_label_type_id;
    // 终点顶点ID
    VertexId end_vertex_id;
};