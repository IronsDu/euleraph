# 01 数据文档：WiredTiger Schema & 数据口径（Euleraph）

> 本文档描述 Euleraph 在 WiredTiger 中的持久化结构（基于 `src/storage/wiredtiger_common.cpp/.hpp`），用于：
> - 快速理解数据如何落盘
> - 约束字段口径与索引
> - 支撑导入与查询的正确性/性能分析

---

## 1. 总体约定
- 存储引擎：WiredTiger（嵌入式 KV/表存储）
- schema 初始化入口：`wiredtiger_initialize_databse_schema(database_dir)`
- 表名前缀：`table:<base_name>`

内部使用的核心 ID：
- `LabelTypeId`：label 类型的内部 id（uint64）
- `RelationTypeId`：relation 类型内部 id（uint64）
- `VertexId`：顶点内部 id（uint64）
- `VertexPk / vertex_ident`：顶点外部标识（string），这是用户侧看到的顶点id

---

## 2. 表：label_type（标签类型）
**表名**：`table:label_type`  
**用途**：label name ↔ label_type_id 的映射（全局唯一）

**WiredTiger schema**
- `key_format=r`：record number（即 `LabelTypeId`）
- `value_format=S`：`name`（label 名称）
- `columns=(id,name)`

**索引**
- `index:label_type:name_pk_index`：以 `name` 建索引（实现中视作唯一）

**口径**
- label name（字符串）对用户可见，应视为全局唯一
- 允许重复写入同名 label，内部应返回同一个 `LabelTypeId`（见单测）

---

## 3. 表：relation_type（关系类型）
**表名**：`table:relation_type`  
**用途**：relation name ↔ relation_type_id 映射（全局唯一）

**WiredTiger schema**
- `key_format=r`：record number（即 `RelationTypeId`）
- `value_format=S`：`name`
- `columns=(id,name)`

**索引**
- `index:relation_type:name_pk_index`：以 `name` 建索引（实现中视作唯一）

---

## 4. 表：vertex（顶点）
**表名**：`table:vertex`  
**用途**：顶点 id ↔ (label_type_id, vertex_ident) 映射

**WiredTiger schema**
- `key_format=r`：record number（即 `VertexId`）
- `value_format=QS`：`(label_type_id, vertex_ident)`
- `columns=(id,label_type_id,vertex_ident)`

**索引**
- `index:vertex:vertex_ident_pk_index`：按 `vertex_ident` 建索引（实现中视作唯一）
  - ⚠️ 当前实现中：接口/算法层有时需要传入 label_type_id 才能通过 pk 找到 VertexId（见 API 文档中的“默认 label id”假设）
  - 若要支持“同 pk 不同 label”，需要：
    - 索引变为复合 `(label_type_id, vertex_ident)`，或
    - 增加二级表 `label+pk -> vertex_id`

**口径**
- `vertex_ident`：外部标识字符串（例如 Person 的主键），对用户可见

---

## 5. 表：edge（边）
**表名**：`table:edge`  
**用途**：存储邻接边，支持按起点/方向/关系进行前缀扫描

**WiredTiger schema**
- `key_format=u`：raw bytes（二进制 key）
- `value_format=Q`：`end_label_type_id`
- `columns=(edge_key,end_label_type_id)`

### 5.1 edge_key 编码：WiredTigerEdgeStorageKey
定义在 `src/storage/wiredtiger_common.hpp`，并强制 1 字节对齐：
- `start_vertex_id` (VertexId)
- `direction` (EdgeDirection，uint8)
- `relation_type_id` (RelationTypeId)
- `end_vertex_id` (VertexId)

`sizeof(WiredTigerEdgeStorageKey) == 25`

### 5.2 查询友好性
通过 key 的字段顺序，可以高效支持：
- 给定 `start_vertex_id + direction` 的邻居扫描
- 给定 `start_vertex_id + direction + relation_type_id` 的邻居扫描
- 算法层可在扫描结果上做 end 节点 label 过滤（通过 value 中的 `end_label_type_id`）

---

## 6. 数据一致性与幂等（建议口径）
当前写入行为（从测试与 writer 实现推断）：
- label/relation：同名写入应返回同一个 id（逻辑幂等）
- vertex：同 (label_type_id, vertex_ident) 写入应返回同一个 vertex_id（逻辑幂等）
- edge：同一条边（按 key 唯一）写入幂等；多次写不会产生重复记录

> 若后续要做并发导入，必须保证这些写入是线程安全/事务安全的。

---

## 7. 与 API 的映射关系（给调用方看的结论）
- API 输入的 `n_labels/r_labels` 使用 **name**，服务端查表转为内部 id
- API 输入的 `node_ids/start_vertex_pk` 是 **vertex_ident/pk**，服务端需能找到对应 `VertexId`
