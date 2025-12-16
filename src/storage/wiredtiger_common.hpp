#pragma once

#include <string>
#include "interface/types/types.hpp"

// 必须按照1字节对齐，避免填充！因为此结构会用于构造前缀搜索中的Item.
#pragma pack(push, 1)
// wiredtiger存储边的表结构的key编码
// 这个key可唯一定义一条边
struct WiredTigerEdgeStorageKey
{
    VertexId       start_vertex_id;  // 起始顶点ID
    EdgeDirection  direction;        // 边的方向
    RelationTypeId relation_type_id; // 关系类型ID
    VertexId       end_vertex_id;    // 终点顶点ID
};
#pragma pack(pop)

static_assert(sizeof(WiredTigerEdgeStorageKey) == 25);

// 初始化数据库模式, 创建所需的表和索引
void wiredtiger_initialize_databse_schema(std::string database_name);