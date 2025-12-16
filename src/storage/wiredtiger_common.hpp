#pragma once

#include "interface/types/types.hpp"

#pragma pack(push, 1)
struct WiredTigerEdgeStorageKey
{
    VertexId       start_vertex_id;  // 起始顶点ID
    EdgeDirection  direction;        // 边的方向
    RelationTypeId relation_type_id; // 关系类型ID
    VertexId       end_vertex_id;    // 终点顶点ID
};
#pragma pack(pop)

static_assert(sizeof(WiredTigerEdgeStorageKey) == 25);