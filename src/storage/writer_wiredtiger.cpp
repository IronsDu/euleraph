#include "writer_wiredtiger.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>

#include "wiredtiger.h"
#include "storage/wiredtiger_common.hpp"

#include "utils/defer.hpp"

WriterWiredTiger::WriterWiredTiger(WT_CONNECTION* conn)
{
    conn->open_session(conn, nullptr, nullptr, &session_);
}

WriterWiredTiger::~WriterWiredTiger()
{
    session_->close(session_, nullptr);
}

// TODO::目前的实现方案可能存在写入多个重复的label type的问题，但幸亏查询时会查询出record number最小的那个
LabelTypeId WriterWiredTiger::write_label_type(const LabelType& label_type)
{
    const auto it = label_type_cache_.find(label_type);
    if (it != label_type_cache_.end())
    {
        return it->second;
    }

    uint64_t record_number = 0;

    while (true)
    {
        int        code   = 0;
        WT_CURSOR* cursor = nullptr;
        DEFER(if (cursor != nullptr) { cursor->close(cursor); });

        // TODO::测试config为`overwrite`的情况
        code = session_->open_cursor(session_, "table:label_type", nullptr, "append", &cursor);
        if (code != 0)
        {
            continue;
        }
        cursor->set_value(cursor, label_type.c_str());
        code = cursor->insert(cursor);
        if (code != 0)
        {
            continue;
        }

        code = session_->open_cursor(session_, "index:label_type:name_pk_index(id)", nullptr, nullptr, &cursor);
        if (code != 0)
        {
            continue;
        }
        cursor->set_key(cursor, label_type.c_str());
        code = cursor->search(cursor);
        if (code != 0)
        {
            continue;
        }

        code = cursor->get_value(cursor, &record_number);
        if (code != 0)
        {
            continue;
        }

        label_type_cache_[label_type] = LabelTypeId(record_number);
        break;
    }

    return record_number;
}

// TODO::目前的实现方案可能存在写入多个重复的relation type的问题，但幸亏查询时会查询出record number最小的那个
RelationTypeId WriterWiredTiger::write_relation_type(const RelationType& relation_type)
{
    const auto it = relation_type_cache_.find(relation_type);
    if (it != relation_type_cache_.end())
    {
        return it->second;
    }

    uint64_t record_number = 0;

    while (true)
    {
        int        code   = 0;
        WT_CURSOR* cursor = nullptr;
        DEFER(if (cursor != nullptr) { cursor->close(cursor); });

        // TODO::测试config为`overwrite`的情况
        code = session_->open_cursor(session_, "table:relation_type", nullptr, "append", &cursor);
        if (code != 0)
        {
            continue;
        }
        cursor->set_value(cursor, relation_type.c_str());
        code = cursor->insert(cursor);
        if (code != 0)
        {
            continue;
        }

        code = session_->open_cursor(session_, "index:relation_type:name_pk_index(id)", nullptr, nullptr, &cursor);
        if (code != 0)
        {
            continue;
        }
        cursor->set_key(cursor, relation_type.c_str());
        code = cursor->search(cursor);
        if (code != 0)
        {
            continue;
        }

        code = cursor->get_value(cursor, &record_number);
        if (code != 0)
        {
            continue;
        }

        relation_type_cache_[relation_type] = RelationTypeId(record_number);
        break;
    }

    return record_number;
}

VertexId WriterWiredTiger::write_vertex(const Vertex& vertice)
{
    uint64_t record_number = 0;

    while (true)
    {
        int        code   = 0;
        WT_CURSOR* cursor = nullptr;
        DEFER(if (cursor != nullptr) { cursor->close(cursor); });

        // 先查询
        code = session_->open_cursor(session_, "index:vertex:vertex_ident_pk_index(id)", nullptr, nullptr, &cursor);
        if (code != 0)
        {
            continue;
        }
        cursor->set_key(cursor, vertice.vertex_pk.c_str());
        code = cursor->search(cursor);
        if (code == 0)
        {
            code = cursor->get_value(cursor, &record_number);
            if (code != 0)
            {
                continue;
            }
            break;
        }

        // TODO::测试config为`overwrite`的情况
        code = session_->open_cursor(session_, "table:vertex", nullptr, "append", &cursor);
        if (code != 0)
        {
            continue;
        }
        cursor->set_value(cursor, vertice.label_type_id, vertice.vertex_pk.c_str());
        code = cursor->insert(cursor);
    }

    return record_number;
}

std::vector<VertexId> WriterWiredTiger::write_vertices(const std::vector<Vertex>& vertices)
{
    std::vector<VertexId> result;
    result.reserve(vertices.size());

    std::unordered_map<VertexPk, VertexId> cache;

    for (const auto& vertex : vertices)
    {
        if (const auto it = cache.find(vertex.vertex_pk); it != cache.end())
        {
            result.push_back(it->second);
            continue;
        }
        VertexId vertex_id      = write_vertex(vertex);
        cache[vertex.vertex_pk] = vertex_id;
        result.push_back(vertex_id);
    }

    return result;
}

void WriterWiredTiger::write_edge(const Edge& edge)
{
    WiredTigerEdgeStorageKey k;
    k.start_vertex_id  = edge.start_vertex_id;
    k.direction        = edge.direction;
    k.relation_type_id = edge.relation_type_id;
    k.end_vertex_id    = edge.end_vertex_id;

    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    // TODO::测试config为`overwrite`的情况
    code = session_->open_cursor(session_, "table:edge", nullptr, "append", &cursor);
    if (code != 0)
    {
        throw std::runtime_error("");
    }

    WT_ITEM key_item;
    key_item.data = static_cast<const void*>(&k);
    key_item.size = sizeof(k);
    cursor->set_key(cursor, &key_item);
    cursor->set_value(cursor, &edge.end_label_type_id);
    code = cursor->insert(cursor);
    if (code != 0)
    {
        throw std::runtime_error("");
    }
}

void WriterWiredTiger::write_edges(const std::vector<Edge>& edges)
{
    int code = 0;
    code     = session_->begin_transaction(session_, NULL);
    for (const auto& edge : edges)
    {
        write_edge(edge);
    }
    code = session_->commit_transaction(session_, NULL);
    return;
}
