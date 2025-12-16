#include "reader_wiredtiger.hpp"

#include "storage/wiredtiger_common.hpp"
#include "utils/defer.hpp"
#include <string.h>

ReaderWiredTiger::ReaderWiredTiger(WT_CONNECTION* conn)
{
    conn->open_session(conn, nullptr, nullptr, &session_);
}

std::optional<LabelTypeId> ReaderWiredTiger::get_label_type_id(const LabelType& label_type)
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

std::optional<RelationTypeId> ReaderWiredTiger::get_relation_type_id(const RelationType& relation_type)
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

std::optional<VertexId> ReaderWiredTiger::get_vertex_id(const LabelTypeId& label_type_id, const VertexPk& vertex_pk)
{
    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    code = session_->open_cursor(session_, "index:vertex:vertex_ident_pk_index(id)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    cursor->set_key(cursor, vertex_pk.c_str());
    code = cursor->search(cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    uint64_t record_number = 0;
    code                   = cursor->get_value(cursor, &record_number);
    if (code != 0)
    {
        return std::nullopt;
    }

    return record_number;
}

std::vector<Edge> ReaderWiredTiger::get_neighbors_by_start_vertex(const VertexId&               start_vertex_id,
                                                                  const LabelTypeId&            start_label_type_id,
                                                                  EdgeDirection                 direction,
                                                                  std::optional<RelationTypeId> relation_type_id)
{
    // 构造前缀key
    WiredTigerEdgeStorageKey k;
    k.start_vertex_id                                 = start_vertex_id;
    k.direction                                       = direction;
    const bool           need_filter_relation_type_id = relation_type_id.has_value();
    const RelationTypeId filter_relation_type_id      = [&]() -> RelationTypeId {
        if (relation_type_id.has_value())
        {
            return static_cast<RelationTypeId>(relation_type_id.value());
        }
        return static_cast<RelationTypeId>(0);
    }();
    if (need_filter_relation_type_id)
    {
        k.relation_type_id = filter_relation_type_id;
    }

    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    // 打开edge表的主表游标
    code = session_->open_cursor(session_, "table:edge(end_label_type_id)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return {};
    }

    // 构造WT_ITEM用于前缀搜索
    WT_ITEM key_item;
    key_item.data = &k;
    key_item.size = need_filter_relation_type_id ? (sizeof(VertexId) + sizeof(EdgeDirection) + sizeof(RelationTypeId))
                                                 : (sizeof(VertexId) + sizeof(EdgeDirection));

    // 设置key并search_near
    cursor->set_key(cursor, &key_item);
    int exact = 0;
    code      = cursor->search_near(cursor, &exact);
    if (code != 0)
    {
        return {};
    }

    std::vector<Edge> result_edges;

    if (exact == 0)
    {
    }
    else if (exact < 0)
    {
    }
    else
    {
        // 向后遍历所有前缀匹配的边
        do
        {
            WT_ITEM edge_key_item;
            cursor->get_key(cursor, &edge_key_item);
            LabelTypeId* end_label_type_id; // 终点标签类型ID
            if (cursor->get_value(cursor, &end_label_type_id) != 0)
            {
                break;
            }

            const auto* edge_key = reinterpret_cast<const WiredTigerEdgeStorageKey*>(edge_key_item.data);
            if (edge_key->start_vertex_id != start_vertex_id || edge_key->direction != direction ||
                (need_filter_relation_type_id && edge_key->relation_type_id != filter_relation_type_id))
                break;

            Edge edge;
            edge.relation_type_id    = edge_key->relation_type_id;
            edge.start_label_type_id = start_label_type_id;
            edge.start_vertex_id     = edge_key->start_vertex_id;
            edge.direction           = edge_key->direction;
            edge.end_vertex_id       = edge_key->end_vertex_id;
            edge.end_label_type_id   = *end_label_type_id;

            result_edges.push_back(edge);
        } while (cursor->next(cursor) == 0);
    }

    return result_edges;
}
