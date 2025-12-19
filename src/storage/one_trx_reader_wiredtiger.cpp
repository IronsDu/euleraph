#include "one_trx_reader_wiredtiger.hpp"

#include <string.h>
#include <spdlog/spdlog.h>

#include "storage/wiredtiger_common.hpp"
#include "utils/defer.hpp"

OneTrxReaderWiredTiger::OneTrxReaderWiredTiger(WT_CONNECTION* conn)
{
    conn->open_session(conn, nullptr, nullptr, &session_);
    int code = session_->begin_transaction(session_, NULL);
    if (code != 0)
    {
        spdlog::error("OneTrxReaderWiredTiger begin_transaction failed, code:{}", code);
    }

    // 打开edge表的主表游标并缓存它
    code = session_->open_cursor(session_, "table:edge(end_label_type_id)", nullptr, "readonly=true", &edge_cursor_);
    if (code != 0)
    {
        spdlog::error("OneTrxReaderWiredTiger open edge cursor failed, code:{}", code);
    }
}

OneTrxReaderWiredTiger::~OneTrxReaderWiredTiger()
{
    if (edge_cursor_ != nullptr)
    {
        edge_cursor_->close(edge_cursor_);
    }
    session_->rollback_transaction(session_, NULL);
    session_->close(session_, nullptr);
}

std::optional<LabelTypeId> OneTrxReaderWiredTiger::get_label_type_id(const LabelType& label_type)
{
    const auto it = label_type_cache_.find(label_type);
    if (it != label_type_cache_.end())
    {
        return it->second;
    }

    uint64_t record_number = 0;

    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    code = session_->open_cursor(session_, "index:label_type:name_pk_index(id)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    cursor->set_key(cursor, label_type.c_str());
    code = cursor->search(cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    code = cursor->get_value(cursor, &record_number);
    if (code != 0)
    {
        return std::nullopt;
    }

    label_type_cache_[label_type] = LabelTypeId(record_number);

    return record_number;
}

std::optional<LabelType> OneTrxReaderWiredTiger::get_label_type_by_id(LabelTypeId label_type_id)
{
    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    code = session_->open_cursor(session_, "table:label_type(name)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    cursor->set_key(cursor, label_type_id);
    code = cursor->search(cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    const char* label_type_name = nullptr;
    code                        = cursor->get_value(cursor, &label_type_name);
    if (code != 0)
    {
        return std::nullopt;
    }

    return LabelType(label_type_name);
}

std::optional<RelationTypeId> OneTrxReaderWiredTiger::get_relation_type_id(const RelationType& relation_type)
{
    const auto it = relation_type_cache_.find(relation_type);
    if (it != relation_type_cache_.end())
    {
        return it->second;
    }

    uint64_t record_number = 0;

    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    code = session_->open_cursor(session_, "index:relation_type:name_pk_index(id)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    cursor->set_key(cursor, relation_type.c_str());
    code = cursor->search(cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    code = cursor->get_value(cursor, &record_number);
    if (code != 0)
    {
        return std::nullopt;
    }

    relation_type_cache_[relation_type] = RelationTypeId(record_number);

    return record_number;
}

std::optional<RelationType> OneTrxReaderWiredTiger::get_relation_type_by_id(RelationTypeId relation_type_id)
{
    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    code = session_->open_cursor(session_, "table:relation_type(name)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    cursor->set_key(cursor, relation_type_id);
    code = cursor->search(cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    const char* relation_type_name = nullptr;
    code                           = cursor->get_value(cursor, &relation_type_name);
    if (code != 0)
    {
        return std::nullopt;
    }

    return RelationType(relation_type_name);
}

std::vector<std::optional<VertexId>> OneTrxReaderWiredTiger::get_vertex_ids(const std::vector<VertexPk>& vertex_pks)
{
    std::vector<std::optional<VertexId>> result;
    const auto                           vertex_pks_size = vertex_pks.size();
    result.resize(vertex_pks_size);

    int code = 0;
    code     = session_->begin_transaction(session_, NULL);
    if (code != 0)
    {
        return result;
    }
    DEFER(session_->rollback_transaction(session_, NULL));

    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });
    code = session_->open_cursor(session_, "index:vertex:vertex_ident_pk_index(id)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return result;
    }

    for (int i = 0; i < vertex_pks_size; i++)
    {
        const auto& vertex_pk = vertex_pks[i];

        cursor->set_key(cursor, vertex_pk.c_str());
        code = cursor->search(cursor);
        if (code != 0)
        {
            continue;
        }

        uint64_t record_number = 0;
        code                   = cursor->get_value(cursor, &record_number);
        if (code != 0)
        {
            continue;
        }
        result[i] = record_number;
    }

    return result;
}

std::optional<VertexId> OneTrxReaderWiredTiger::get_vertex_id(const LabelTypeId& label_type_id,
                                                              const VertexPk&    vertex_pk)
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

std::optional<VertexPk> OneTrxReaderWiredTiger::get_vertex_pk_by_id(VertexId vertex_id)
{
    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    code = session_->open_cursor(session_, "table:vertex(vertex_ident)", nullptr, nullptr, &cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    cursor->set_key(cursor, vertex_id);
    code = cursor->search(cursor);
    if (code != 0)
    {
        return std::nullopt;
    }

    const char* vertex_pk = nullptr;
    code                  = cursor->get_value(cursor, &vertex_pk);
    if (code != 0)
    {
        return std::nullopt;
    }

    return VertexPk(vertex_pk);
}

std::vector<Edge> OneTrxReaderWiredTiger::get_neighbors_by_start_vertex(const VertexId&    start_vertex_id,
                                                                        const LabelTypeId& start_label_type_id,
                                                                        EdgeDirection      direction,
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

    int code = 0;

    // 构造WT_ITEM用于前缀搜索
    WT_ITEM key_item;
    key_item.data = &k;
    key_item.size = need_filter_relation_type_id ? (sizeof(VertexId) + sizeof(EdgeDirection) + sizeof(RelationTypeId))
                                                 : (sizeof(VertexId) + sizeof(EdgeDirection));

    // 设置key并search_near
    edge_cursor_->set_key(edge_cursor_, &key_item);
    int exact = 0;
    code      = edge_cursor_->search_near(edge_cursor_, &exact);
    if (code != 0)
    {
        return {};
    }

    std::vector<Edge> result_edges;

    if (exact == 0)
    {
        WT_ITEM edge_key_item;
        code = edge_cursor_->get_key(edge_cursor_, &edge_key_item);
        LabelTypeId* end_label_type_id; // 终点标签类型ID
        code = edge_cursor_->get_value(edge_cursor_, &end_label_type_id);

        const auto* edge_key = reinterpret_cast<const WiredTigerEdgeStorageKey*>(edge_key_item.data);

        Edge edge;
        edge.relation_type_id    = edge_key->relation_type_id;
        edge.start_label_type_id = start_label_type_id;
        edge.start_vertex_id     = edge_key->start_vertex_id;
        edge.direction           = edge_key->direction;
        edge.end_vertex_id       = edge_key->end_vertex_id;
        edge.end_label_type_id   = *end_label_type_id;

        result_edges.push_back(edge);
    }
    else if (exact < 0)
    {
        // 向后遍历所有前缀匹配的边
        while (edge_cursor_->next(edge_cursor_) == 0)
        {
            WT_ITEM edge_key_item;
            code = edge_cursor_->get_key(edge_cursor_, &edge_key_item);
            if (code != 0)
            {
                break;
            }

            const auto* edge_key = reinterpret_cast<const WiredTigerEdgeStorageKey*>(edge_key_item.data);
            if (edge_key->start_vertex_id != start_vertex_id || edge_key->direction != direction ||
                (need_filter_relation_type_id && edge_key->relation_type_id != filter_relation_type_id))
            {
                break;
            }

            LabelTypeId end_label_type_id; // 终点标签类型ID
            code = edge_cursor_->get_value(edge_cursor_, &end_label_type_id);
            if (code != 0)
            {
                spdlog::error("get value failed, code:{}, result_edges size:%d", code, result_edges.size());
                break;
            }

            Edge edge;
            edge.relation_type_id    = edge_key->relation_type_id;
            edge.start_label_type_id = start_label_type_id;
            edge.start_vertex_id     = edge_key->start_vertex_id;
            edge.direction           = edge_key->direction;
            edge.end_vertex_id       = edge_key->end_vertex_id;
            edge.end_label_type_id   = end_label_type_id;

            result_edges.push_back(edge);
        };
    }
    else
    {
        // 向后遍历所有前缀匹配的边
        do
        {
            WT_ITEM edge_key_item;
            edge_cursor_->get_key(edge_cursor_, &edge_key_item);
            LabelTypeId end_label_type_id; // 终点标签类型ID
            code = edge_cursor_->get_value(edge_cursor_, &end_label_type_id);
            if (code != 0)
            {
                spdlog::error("get value failed, code:{}, result_edges size:%d", code, result_edges.size());
                break;
            }

            const auto* edge_key = reinterpret_cast<const WiredTigerEdgeStorageKey*>(edge_key_item.data);
            if (edge_key->start_vertex_id != start_vertex_id || edge_key->direction != direction ||
                (need_filter_relation_type_id && edge_key->relation_type_id != filter_relation_type_id))
            {
                break;
            }

            Edge edge;
            edge.relation_type_id    = edge_key->relation_type_id;
            edge.start_label_type_id = start_label_type_id;
            edge.start_vertex_id     = edge_key->start_vertex_id;
            edge.direction           = edge_key->direction;
            edge.end_vertex_id       = edge_key->end_vertex_id;
            edge.end_label_type_id   = end_label_type_id;

            result_edges.push_back(edge);
        } while (edge_cursor_->next(edge_cursor_) == 0);
    }

    return result_edges;
}
