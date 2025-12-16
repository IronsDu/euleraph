#include "wiredtiger_common.hpp"

#include <filesystem>

#include "wiredtiger.h"

const std::string LabelTypeTableName    = "label_type";
const std::string RelationTypeTableName = "relation_type";
const std::string VertexTableName       = "vertex";
const std::string EdgeTableName         = "edge";

static std::string make_table_name(const std::string& base_name)
{
    return "table:" + base_name;
}

void wiredtiger_initialize_databse_schema(std::string database_name)
{
    WT_CONNECTION* conn;

    std::error_code ec;
    std::filesystem::create_directories(database_name, ec);

    wiredtiger_open(database_name.c_str(), nullptr, "create", &conn);

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

    conn->close(conn, nullptr);
}