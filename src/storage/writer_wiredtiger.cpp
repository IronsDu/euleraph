#include "writer_wiredtiger.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>

#include "wiredtiger.h"
#include "storage/wiredtiger_common.hpp"

#include "utils/defer.hpp"

const std::string LabelTypeTableName    = "label_type";
const std::string RelationTypeTableName = "relation_type";
const std::string VertexTableName       = "vertex";
const std::string EdgeTableName         = "edge";

static std::string make_table_name(const std::string& base_name)
{
    return "table:" + base_name;
}

void WriterWiredTiger::initialize_databse_schema()
{
    WT_CONNECTION* conn;

    std::error_code ec;
    std::filesystem::create_directories("graph_database", ec);

    wiredtiger_open("graph_database", nullptr, "create", &conn);

    WT_SESSION* session;
    conn->open_session(conn, nullptr, nullptr, &session);
    int code;

    // 创建标签类型到标签类型表
    auto label_type_to_label_type_id_table = make_table_name(LabelTypeTableName);
    // 表的列有：{id, name}
    // id 为 record number (它即作为label type id)
    // name 为 labale名称
    code = session->create(session,
                           label_type_to_label_type_id_table.c_str(),
                           "key_format=r,value_format=S,columns=(id,name)");
    // 根据 label的name构建索引，在实现中，它是唯一索引
    code = session->create(session, "index:label_type:name_pk_index", "columns=(name)");

    // 创建关系类型到关系类型ID的映射表
    auto relation_type_to_relation_type_id_table = make_table_name(RelationTypeTableName);
    // 表的列有：{id, name}
    // id 为 record number (它即作为relation type id)
    // name 为 relation名称
    code = session->create(session,
                           relation_type_to_relation_type_id_table.c_str(),
                           "key_format=r,value_format=S,columns=(id,name)");
    // 根据 relation的name构建索引，在实现中，它是唯一索引
    code = session->create(session, "index:relation_type:name_pk_index", "columns=(name)");

    // 创建顶点表
    auto vertex_table = make_table_name(VertexTableName);
    // 表的列有：{id, label_type_id, vertex_ident}
    // id为 record number (它即作为vertex id)
    code = session->create(session,
                           vertex_table.c_str(),
                           "key_format=r,value_format=QS,columns=(id,label_type_id,vertex_ident)");
    // 根据 vertex_ident 构建索引，在实现中，它是唯一索引
    code = session->create(session, "index:vertex:vertex_ident_pk_index", "columns=(vertex_ident)");

    // 创建边表
    auto edge_table = make_table_name(EdgeTableName);
    // 表的列有：{edge_key, end_label_type_id}
    // edge_key为 `WiredTigerEdgeStorageKey` 的二进制
    // end_label_type_id为终点标签类型ID
    code = session->create(session,
                           edge_table.c_str(),
                           "key_format=u,value_format=Q,columns=(edge_key,end_label_type_id)");

    std::cout << code << std::endl;

    conn->close(conn, nullptr);
}

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
