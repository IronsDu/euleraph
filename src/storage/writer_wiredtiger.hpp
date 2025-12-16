#pragma once

#include <unordered_map>

#include "interface/storage/writer.hpp"
#include "wiredtiger.h"

// WiredTiger存储引擎的写入实现
class WriterWiredTiger : public WriterInterface
{
public:
    WriterWiredTiger(WT_CONNECTION* conn);
    ~WriterWiredTiger();

public:
    LabelTypeId           write_label_type(const LabelType& label_type) override;
    RelationTypeId        write_relation_type(const RelationType& relation_type) override;
    VertexId              write_vertex(const Vertex& vertice) override;
    std::vector<VertexId> write_vertices(const std::vector<Vertex>& vertices) override;
    void                  write_edge(const Edge& edge) override;
    void                  write_edges(const std::vector<Edge>& edges) override;

private:
    WT_SESSION* session_;

    // 缓存已写入的标签类型和关系类型
    std::unordered_map<LabelType, LabelTypeId> label_type_cache_;

    // 缓存已写入的关系类型
    std::unordered_map<RelationType, RelationTypeId> relation_type_cache_;
};