# 01 需求卡（PRD-lite）：Euleraph 图数据服务

## 1. 背景
Euleraph 是一个面向图数据的后台服务，提供：
- 图数据的**导入与持久化存储**（基于 WiredTiger）
- 基于存储的若干**图算法查询能力**（通过 HTTP API 暴露）

当前工程以 **C++20 + Drogon** 实现 HTTP 服务入口，以 **WiredTiger** 作为本地嵌入式 KV/表存储，并提供导入器（Importer）读取数据文件导入图。

## 2. 目标（迭代内最小目标）
- 能从输入数据构建图并持久化到本地数据库目录
- 启动后暴露 HTTP 服务，提供以下查询能力：
  - Ping 健康检查
  - 一度邻居查询（带方向/关系过滤）
  - K-hop 邻居数量查询（带方向/标签/关系过滤）
  - 共同邻居数量查询（带关系过滤）
  - WCC（弱连通分量）数量查询（带标签/关系过滤）
  - 子图匹配（同态）数量查询（基于模式图描述）

## 3. 非目标（Out of scope）
- 分布式存储/分片/多机扩展（当前为单机本地数据库目录）
- 复杂的鉴权、租户隔离（当前 API 未实现鉴权）
- 完整的管理后台（当前以 API 为主）

## 4. 角色与使用场景
- **算法调用方/评测程序**：通过 REST API 发起查询并获得计数或邻居结果
- **开发/运维**：通过命令行参数启动服务、导入数据、观察日志与指标（目前以日志为主）

## 5. 功能需求（按接口）
### 5.1 健康检查
- GET `/ping` 返回 `pong`

### 5.2 一度邻居查询
- POST `/get_one_hop_neighbors`
- 输入：起点 pk、方向、关系类型（可选）
- 输出：邻居边的可读字符串列表（包含起点/终点/标签/关系）

### 5.3 K-hop 邻居数量
- POST `/api/v1/algorithms/k-hop-neighbors`
- 输入：node_ids、k、direction，可选 n_labels/r_labels
- 输出：count（k-hop 邻居总数）

### 5.4 共同邻居数量
- POST `/api/v1/algorithms/common-neighbors`
- 输入：node_ids，可选 r_labels
- 输出：count（共同邻居数量）

### 5.5 WCC 数量
- POST `/api/v1/algorithms/wcc`
- 输入：可选 n_labels/r_labels
- 输出：component_count（连通分量数量）

### 5.6 子图匹配数量（同态）
- POST `/api/v1/algorithms/subgraph-matching`
- 输入：nodes（变量与候选标签集合），edges（source/target/direction/labels）
- 输出：count（匹配实例数量）

## 6. 非功能需求（建议验收口径）
- **正确性**：同一数据集，同一查询，多次调用结果一致
- **可用性**：服务启动后 `/ping` 立即可用；异常输入返回明确错误
- **性能**：在给定数据规模下，算法接口在合理时间内返回（具体阈值由评测侧给出/另行确定）
- **稳定性**：导入/查询过程中异常不会导致数据库目录损坏（至少支持重建/重新导入）

## 7. 验收标准（Acceptance Criteria）
- [ ] 可用脚本/命令完成编译与启动
- [ ] 可导入测试数据（例如 `test/data/sample_data.xlsx`）并成功启动服务
- [ ] 6 个接口均可通过 curl 调用获得预期返回（详见 API Spec）
- [ ] 数据库目录下可见 WiredTiger 数据文件，并可在重启后复用（若未强制重建）
