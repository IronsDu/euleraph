#include "writer_wiredtiger.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>

#include <wiredtiger.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
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

    int        code   = 0;
    WT_CURSOR* cursor = nullptr;
    DEFER(if (cursor != nullptr) { cursor->close(cursor); });

    // TODO::测试config为`overwrite`的情况
    code = session_->open_cursor(session_, "table:vertex", nullptr, "append", &cursor);
    if (code != 0)
    {
        throw std::runtime_error(fmt::format("write vertex, pk:{}, label id:{} open_cursor failed, code:{}",
                                             vertice.vertex_pk,
                                             vertice.label_type_id,
                                             code));
    }
    cursor->set_value(cursor, vertice.label_type_id, vertice.vertex_pk.c_str());
    code = cursor->insert(cursor);
    if (code != 0)
    {
        throw std::runtime_error(fmt::format("write vertex, pk:{}, label id:{} insert failed, code:{}",
                                             vertice.vertex_pk,
                                             vertice.label_type_id,
                                             code));
    }
    code = cursor->get_key(cursor, &record_number);
    if (code != 0)
    {
        throw std::runtime_error(fmt::format("write vertex, pk:{}, label id:{} get_key failed, code:{}",
                                             vertice.vertex_pk,
                                             vertice.label_type_id,
                                             code));
    }

    return record_number;
}

std::vector<VertexId> WriterWiredTiger::write_vertices(const std::vector<Vertex>& vertices)
{
    int code = 0;
    code     = session_->begin_transaction(session_, NULL);
    if (code != 0)
    {
        throw std::runtime_error(fmt::format("batch write vertexs, begin transaction failed code:{}", code));
    }

    std::vector<VertexId> result;
    result.resize(vertices.size());

    try
    {
        WT_CURSOR* cursor = nullptr;
        DEFER(if (cursor != nullptr) { cursor->close(cursor); });

        // TODO::测试config为`overwrite`的情况
        code = session_->open_cursor(session_, "table:vertex", nullptr, "append", &cursor);
        if (code != 0)
        {
            throw std::runtime_error(fmt::format("batch write vertexs, open_cursor failed code:{}", code));
        }

        const auto vertices_size = vertices.size();
        for (int i = 0; i < vertices_size; i++)
        {
            const auto vertice = vertices[i];

            cursor->set_value(cursor, vertice.label_type_id, vertice.vertex_pk.c_str());
            code = cursor->insert(cursor);
            if (code != 0)
            {
                throw std::runtime_error(
                    fmt::format("batch write vertexs, insert failed code:{}, vertex pk:{}, label type id:{}",
                                code,
                                vertice.vertex_pk,
                                vertice.label_type_id));
            }
            uint64_t record_number = 0;
            code                   = cursor->get_key(cursor, &record_number);
            if (code != 0)
            {
                throw std::runtime_error(
                    fmt::format("batch write vertexs, get_key failed code:{}, vertex pk:{}, label type id:{}",
                                code,
                                vertice.vertex_pk,
                                vertice.label_type_id));
            }

            result[i] = record_number;
        }
    }
    catch (const std::runtime_error& e)
    {
        spdlog::error("batch write vertexs catch exception:{}", e.what());
        session_->rollback_transaction(session_, NULL);
        throw;
    }
    catch (...)
    {
        spdlog::error("batch write vertexs catch unknown exception");
        session_->rollback_transaction(session_, NULL);
        throw;
    }

    code = session_->commit_transaction(session_, NULL);

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
        throw std::runtime_error(fmt::format("write edge, begin transaction failed code:{}", code));
    }

    WT_ITEM key_item;
    key_item.data = static_cast<const void*>(&k);
    key_item.size = sizeof(k);
    cursor->set_key(cursor, &key_item);
    cursor->set_value(cursor, edge.end_label_type_id);
    code = cursor->insert(cursor);
    if (code != 0)
    {
        throw std::runtime_error(fmt::format("write edge, insert failed code:{}", code));
    }
}

void WriterWiredTiger::write_edges(const std::vector<Edge>& edges)
{
    int code = 0;
    code     = session_->begin_transaction(session_, NULL);
    if (code != 0)
    {
        throw std::runtime_error(fmt::format("batch write edge, begin_transaction failed code:{}", code));
    }

    try
    {
        WT_CURSOR* cursor = nullptr;
        DEFER(if (cursor != nullptr) { cursor->close(cursor); });

        // TODO::测试config为`overwrite`的情况
        code = session_->open_cursor(session_, "table:edge", nullptr, "append", &cursor);
        if (code != 0)
        {
            throw std::runtime_error(fmt::format("batch write edge, open cursor failed code:{}", code));
        }

        WiredTigerEdgeStorageKey k;
        WT_ITEM                  key_item;
        key_item.size = sizeof(k);
        for (const auto& edge : edges)
        {
            k.start_vertex_id  = edge.start_vertex_id;
            k.direction        = edge.direction;
            k.relation_type_id = edge.relation_type_id;
            k.end_vertex_id    = edge.end_vertex_id;

            key_item.data = static_cast<const void*>(&k);
            cursor->set_key(cursor, &key_item);
            cursor->set_value(cursor, edge.end_label_type_id);
            code = cursor->insert(cursor);
            if (code != 0)
            {
                throw std::runtime_error(fmt::format("batch write edge:{}-{}-{}-{}, insert failed code:{}",
                                                     k.start_vertex_id,
                                                     (int)k.direction,
                                                     k.relation_type_id,
                                                     k.end_vertex_id,
                                                     code));
            }
        }
    }
    catch (const std::runtime_error& e)
    {
        spdlog::error("batch write edge catch exception:{}", e.what());
        session_->rollback_transaction(session_, NULL);
        throw;
    }
    catch (...)
    {
        spdlog::error("batch write edge catch unknown exception");
        session_->rollback_transaction(session_, NULL);
        throw;
    }
    code = session_->commit_transaction(session_, NULL);
    return;
}
