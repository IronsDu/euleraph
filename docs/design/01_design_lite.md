# 01 技术方案（Design Doc Lite）：Euleraph 图存储与算法服务

## 1. 概述
Euleraph 由 4 个主要子系统组成：
1) **Importer（导入器）**：从输入数据文件中读取边/点数据，写入存储  
2) **Storage（存储层）**：基于 WiredTiger，提供 label/relation/vertex/edge 的读写与邻接查询  
3) **Algo（算法层）**：基于 ReaderInterface 提供 k-hop、common neighbors、WCC、subgraph matching 等算法  
4) **HTTP Service（服务层）**：Drogon 注册路由，将 JSON 请求解析为算法参数并返回 JSON 响应

### 1.1 入口与关键文件
- HTTP 入口与路由注册：`src/main.cpp`
- HTTP 处理函数：`src/service/http/euleraph_http_handle.*`
- 导入器：`src/importer/importer.*`（使用 `xlnt` 读取 xlsx）
- 算法接口：`src/interface/algo/algo.hpp`，实现：`src/algo/*`
- 存储接口：`src/interface/storage/*`，实现：`src/storage/*`

## 2. 架构与数据流
### 2.1 启动流程（当前实现）
1) 解析 CLI 参数（db 目录、是否导入、输入文件、端口等）
2) 初始化日志（spdlog）
3) 初始化 WiredTiger 数据库 schema（创建表/索引）
4) 如 `need_import=true`：执行 Importer 导入数据并创建 checkpoint
5) 启动 Drogon HTTP Server 并注册路由

### 2.2 请求处理流程
以 `/api/v1/algorithms/k-hop-neighbors` 为例：
1) 检查 Content-Type 为 JSON
2) 解析 JSON → `KHopQueryParams`（将 label/relation name 转换为内部 id）
3) 调用算法层 `AlgoInterface::get_k_hop_neighbor_count`
4) 返回 JSON：`{"code":0,"count":<uint64>}`

## 3. 存储设计要点（WiredTiger）
### 3.1 表与索引（摘要）
- `label_type`：record number 作为 `LabelTypeId`，value 存储 label name，index: `name_pk_index`
- `relation_type`：record number 作为 `RelationTypeId`，value 存储 relation name，index: `name_pk_index`
- `vertex`：record number 作为 `VertexId`，value 为 `(label_type_id, vertex_ident)`，index: `vertex_ident_pk_index`
- `edge`：key 为二进制 `WiredTigerEdgeStorageKey`，value 为 `end_label_type_id`

### 3.2 边 key 编码（支持前缀扫描）
`WiredTigerEdgeStorageKey`（25 bytes，1 字节对齐）：
- `start_vertex_id` (VertexId)
- `direction` (EdgeDirection)
- `relation_type_id` (RelationTypeId)
- `end_vertex_id` (VertexId)

这样可以通过前缀（start_vertex_id + direction [+ relation_type_id]）快速扫描邻接边。

## 4. 并发与一致性
- 导入时：目前默认并发为 1（CLI 默认），代码注释提示多并发写边存在冲突风险；建议后续通过：
  - 分区写入（按顶点范围/文件分片）或
  - 将冲突点改为幂等写/批量写事务
- 查询时：HTTP 每个请求创建 Reader（或 OneTrxReader）用于读；WiredTiger 读并发天然支持，但需注意会话/游标生命周期。

## 5. 错误处理与返回规范（当前）
- `/ping`：纯文本返回 `pong`
- 算法类接口：一般返回 JSON `{ "code": 0, ... }`，解析失败返回 `{ "code": -1 }`
- 部分接口在 Content-Type 非 JSON 或 JSON 非法时返回 400 文本错误

> 建议后续统一：所有业务接口返回 `{code, message, data}`，并补充业务错误码表（见后续 ADR）。

## 6. 可观测性（当前与建议）
当前：
- 使用 spdlog 输出关键流程日志
- Drogon 日志级别设为 warn，并写入 `./web` 目录

建议最小增强：
- 为每个请求生成/传递 `request_id/trace_id`（可用 header `X-Request-Id`）
- 关键指标：
  - 请求总量、错误率（4xx/5xx）
  - 各接口 P99 延迟
  - WiredTiger 读写耗时/失败数
- 告警建议：
  - P99 延迟突增
  - 连续 5xx 超阈值
  - DB 打开/会话创建失败

## 7. 关键假设与限制（必须在对外文档中声明）
- 部分接口对 `node_ids` 的解释是“顶点的外部标识字符串（vertex_ident / pk）”，内部通过默认 label_type_id 获取 VertexId（当前实现中存在默认 label id 的假设，详见 API Spec）。
- 子图匹配接口中边方向仅支持 `"IN"` 或 `"OUT"`（非法则降级为 UNDIRECTED）。

## 8. 后续可选 ADR（若要继续演进）
- ADR-001：统一响应包与错误码体系
- ADR-002：导入并发策略（写边冲突的解决方案）
- ADR-003：API 版本化与 `/get_one_hop_neighbors` 迁移到 `/api/v1/...`
