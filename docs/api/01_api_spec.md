# 01 API Spec：Euleraph HTTP 接口文档（Drogon）

## 0. 通用约定
- 服务监听：`0.0.0.0:<port>`（默认端口见 CLI：8200）
- Content-Type：
  - **算法接口（POST）要求** `Content-Type: application/json`
- 返回格式（当前实现）：
  - `/ping`：`text/plain`，body 为 `pong`
  - 其他接口：多数返回 `application/json`，包含 `code` 与结果字段
- 错误处理（当前实现差异较大）：
  - Content-Type 不对 / JSON 无效：返回 `400`，纯文本错误
  - 参数解析失败：通常返回 `200`，JSON `{ "code": -1 }`
  - 业务找不到 label/relation 等：部分接口会返回 JSON `{ "code": 500, "message": "..." }`（并不总是 HTTP 500）

> 建议调用方：优先根据 HTTP 状态码判定协议错误，再根据 JSON `code` 判定业务成功与否。

---

## 1. 健康检查
### GET `/ping`
**Response**
- `200 text/plain`
- Body: `pong`

**curl**
```bash
curl -i http://127.0.0.1:8200/ping
```

---

## 2. 一度邻居查询
### POST `/get_one_hop_neighbors`
**说明**
- 查询指定顶点的一度邻居（按方向，可选按关系过滤）
- 当前实现内部使用一个“默认 label_type_id”（代码中为 `1`）将 `start_vertex_pk` 转换为内部 `VertexId`  
  - 因此如果你的数据里 label_type_id 不匹配，可能会出现 “not found vertex”  
  - 若需要泛化，应在后续迭代将 label 作为请求参数或在存储层支持“无需指定 label 的 pk → vertex_id”查找

**Request (JSON)**
| 字段 | 类型 | 必填 | 说明 |
|---|---|---:|---|
| start_vertex_pk | string | ✅ | 起点顶点外部标识（vertex_ident/pk） |
| direction | int | ❌ | 0=OUTGOING，1=INCOMING，2=UNDIRECTED；默认 2（双向查询会拆成 OUT+IN 两次） |
| relation | string | ❌ | 关系类型名称；不填则不过滤关系类型 |

**Response (JSON)**
- `data`: string array，每条为可读边描述：
  - 出边：`<start_pk>:<start_label> -> [<relation>] -> <end_pk>:<end_label>`
  - 入边：`<end_pk>:<end_label> -> [<relation>] -> <start_pk>:<start_label>`

**curl**
```bash
curl -s http://127.0.0.1:8200/get_one_hop_neighbors   -H 'Content-Type: application/json'   -d '{"start_vertex_pk":"Alice","direction":0,"relation":"Like"}' | jq .
```

---

## 3. K-hop 邻居总数
### POST `/api/v1/algorithms/k-hop-neighbors`
**Request (JSON)**
| 字段 | 类型 | 必填 | 说明 |
|---|---|---:|---|
| node_ids | string[] | ✅ | 起始点外部标识列表（内部通过默认 label_type_id=0 转换为 VertexId） |
| k | int | ✅ | 跳数（k>=1） |
| n_labels | string[] | ❌ | 节点标签过滤（label name 列表，会被转换成 LabelTypeId） |
| r_labels | string[] | ❌ | 边关系类型过滤（relation name 列表 → RelationTypeId） |
| direction | int | ✅ | 1=OUT，2=IN，3=BOTH/UNDIRECTED（见算法实现） |

**Response (JSON)**
- 成功：`{"code":0,"count":<uint64>}`
- 解析失败：`{"code":-1}`

**curl**
```bash
curl -s http://127.0.0.1:8200/api/v1/algorithms/k-hop-neighbors   -H 'Content-Type: application/json'   -d '{"node_ids":["Alice"],"k":2,"direction":1,"r_labels":["Like"],"n_labels":["Person"]}' | jq .
```

---

## 4. 共同邻居总数
### POST `/api/v1/algorithms/common-neighbors`
**Request (JSON)**
| 字段 | 类型 | 必填 | 说明 |
|---|---|---:|---|
| node_ids | string[] | ✅ | 起始点外部标识列表（至少 2 个点才有意义） |
| r_labels | string[] | ❌ | 边关系类型过滤 |

**Response (JSON)**
- 成功：`{"code":0,"count":<int>}`
- 解析失败：`{"code":-1}`

**curl**
```bash
curl -s http://127.0.0.1:8200/api/v1/algorithms/common-neighbors   -H 'Content-Type: application/json'   -d '{"node_ids":["Alice","Bob"],"r_labels":["Like"]}' | jq .
```

---

## 5. WCC（弱连通分量）数量
### POST `/api/v1/algorithms/wcc`
**Request (JSON)**
| 字段 | 类型 | 必填 | 说明 |
|---|---|---:|---|
| n_labels | string[] | ❌ | 节点标签过滤 |
| r_labels | string[] | ❌ | 边关系类型过滤 |

**Response (JSON)**
- 成功：`{"code":0,"component_count":<int>}`
- 解析失败：`{"code":-1}`

**curl**
```bash
curl -s http://127.0.0.1:8200/api/v1/algorithms/wcc   -H 'Content-Type: application/json'   -d '{"n_labels":["Person"],"r_labels":["Like"]}' | jq .
```

---

## 6. 子图匹配（同态）数量
### POST `/api/v1/algorithms/subgraph-matching`
**Request (JSON)**
`nodes`：模式图节点列表，每个节点：
- `var`: string，模式变量名（例如 `"a"`）
- `labels`: string[]，该变量允许的 label 类型名称列表

`edges`：模式边列表，每条边：
- `source`: string，源节点变量名
- `target`: string，目标节点变量名
- `direction`: string，只支持 `"IN"` 或 `"OUT"`（否则记日志并返回 UNDIRECTED）
- `labels`: string[]，边关系类型名称列表

示例：
```json
{
  "nodes": [
    {"var":"a","labels":["Person"]},
    {"var":"b","labels":["Person"]}
  ],
  "edges": [
    {"source":"a","target":"b","direction":"OUT","labels":["Like"]}
  ]
}
```

**Response (JSON)**
- 成功：`{"code":0,"count":<int>}`
- 解析失败：`{"code":-1}`

**curl**
```bash
curl -s http://127.0.0.1:8200/api/v1/algorithms/subgraph-matching   -H 'Content-Type: application/json'   -d '{"nodes":[{"var":"a","labels":["Person"]},{"var":"b","labels":["Dog"]}],
       "edges":[{"source":"a","target":"b","direction":"OUT","labels":["Like"]}]}' | jq .
```
