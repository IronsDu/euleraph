#pragma once

#include <unordered_map>

#include "interface/storage/reader.hpp"
#include "wiredtiger.h"

class ReaderWiredTiger : public ReaderInterface
{
public:
    ReaderWiredTiger(WT_CONNECTION* conn);

public:
    std::optional<LabelTypeId>    get_label_type_id(const LabelType& label_type) override;
    std::optional<LabelType>      get_label_type_by_id(LabelTypeId label_type_id) override;
    std::optional<RelationTypeId> get_relation_type_id(const RelationType& relation_type) override;
    std::optional<RelationType>   get_relation_type_by_id(RelationTypeId relation_type_id) override;

    std::vector<std::optional<VertexId>> get_vertex_ids(const std::vector<VertexPk>& vertex_pks) override;
    // 目前label_type_id参数没有使用, 因为比赛数据里顶点标识是全局唯一的
    std::optional<VertexId> get_vertex_id(const LabelTypeId& label_type_id, const VertexPk& vertex_pk) override;
    std::optional<VertexPk> get_vertex_pk_by_id(VertexId vertex_id) override;

    void scan_vertex_id(ReaderInterface::VertexIdCallback callback) override;

    // 目前start_label_type_id参数只是在构造返回的边中使用
    std::vector<Edge> get_neighbors_by_start_vertex(const VertexId&               start_vertex_id,
                                                    const LabelTypeId&            start_label_type_id,
                                                    EdgeDirection                 direction,
                                                    std::optional<RelationTypeId> relation_type_id) override;

private:
    WT_SESSION* session_;

    // 缓存标签类型和关系类型
    std::unordered_map<LabelType, LabelTypeId> label_type_cache_;

    // 缓存的关系类型
    std::unordered_map<RelationType, RelationTypeId> relation_type_cache_;
};