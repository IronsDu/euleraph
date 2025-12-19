#pragma once

#include <unordered_map>

#include "interface/storage/reader.hpp"
#include "wiredtiger.h"

// 只支持单事务的读取器
// 适用于只读场景, 避免了频繁开启和关闭事务的开销
// 注意: 使用该读取器时, 必须确保在其生命周期内不进行任何写操作, 否则可能导致读取到不一致的数据

class OneTrxReaderWiredTiger : public ReaderInterface
{
public:
    OneTrxReaderWiredTiger(WT_CONNECTION* conn);
    ~OneTrxReaderWiredTiger();

public:
    std::optional<LabelTypeId>    get_label_type_id(const LabelType& label_type) override;
    std::optional<LabelType>      get_label_type_by_id(LabelTypeId label_type_id) override;
    std::optional<RelationTypeId> get_relation_type_id(const RelationType& relation_type) override;
    std::optional<RelationType>   get_relation_type_by_id(RelationTypeId relation_type_id) override;

    std::vector<std::optional<VertexId>> get_vertex_ids(const std::vector<VertexPk>& vertex_pks) override;
    // 目前label_type_id参数没有使用, 因为比赛数据里顶点标识是全局唯一的
    std::optional<VertexId> get_vertex_id(const LabelTypeId& label_type_id, const VertexPk& vertex_pk) override;
    std::optional<VertexPk> get_vertex_pk_by_id(VertexId vertex_id) override;

    // 目前start_label_type_id参数只是在构造返回的边中使用
    std::vector<Edge> get_neighbors_by_start_vertex(const VertexId&               start_vertex_id,
                                                    const LabelTypeId&            start_label_type_id,
                                                    EdgeDirection                 direction,
                                                    std::optional<RelationTypeId> relation_type_id) override;

private:
    WT_SESSION* session_     = nullptr;
    WT_CURSOR*  edge_cursor_ = nullptr;

    // 缓存标签类型和关系类型
    std::unordered_map<LabelType, LabelTypeId> label_type_cache_;

    // 缓存的关系类型
    std::unordered_map<RelationType, RelationTypeId> relation_type_cache_;
};