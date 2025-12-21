# 01 测试清单（Checklist）：Euleraph（后台 + 算法接口）

---

## A. 构建与启动（必测）
- [ ] `cmake -B build -S . && cmake --build build -j` 编译通过
- [ ] 使用本地测试数据导入后可正常启动（建议用 `test/data/sample_data.xlsx`）
- [ ] 启动后 `/ping` 返回 `pong`

---

## B. 数据导入（Importer）
### B1 正常导入
- [ ] 输入文件存在且格式正确：导入完成无 crash
- [ ] 导入完成后创建 checkpoint（重启后数据仍可读）
- [ ] 重复导入（相同数据）结果幂等：label/relation/vertex/edge 不产生重复/不爆炸

### B2 边界与异常
- [ ] 输入文件路径不存在：程序给出明确错误并退出
- [ ] 输入文件为空/缺列：程序拒绝导入（或给出明确错误）
- [ ] `csv_row_num` 指定小于等于 0：CLI 解析应报错

---

## C. 存储层（WiredTiger）单测（已有 gtest）
参考：`test/test_wiredtiger_impl.cpp`
- [ ] label/relation：同名写入返回同一 id
- [ ] vertex：写入后可通过 pk 查询到 vertex_id
- [ ] edge：写入后可按 start_vertex_id 读取邻居边
- [ ] OneTrxReaderWiredTiger / ReaderWiredTiger 行为一致（至少在读取结果上）

---

## D. API 冒烟（主链路）
### D1 /ping
- [ ] GET `/ping` 返回 200 + `pong`

### D2 /get_one_hop_neighbors
- [ ] 正常请求返回 JSON，`data` 为数组
- [ ] relation 过滤生效（不填 relation 返回更多/等于过滤后的结果）
- [ ] direction=0/1/2 三种都能返回（或空数组，但不报错）

### D3 /api/v1/algorithms/k-hop-neighbors
- [ ] `node_ids` 传 1 个点，k=1，direction=1 返回 count
- [ ] 可选过滤 `n_labels/r_labels` 不报错且结果合理
- [ ] Content-Type 非 json 时返回 400

### D4 /api/v1/algorithms/common-neighbors
- [ ] `node_ids` 传 >=2 个点返回 count
- [ ] 可选过滤 r_labels 生效
- [ ] Content-Type 非 json 时返回 400

### D5 /api/v1/algorithms/wcc
- [ ] 不带过滤参数可返回 component_count
- [ ] 带 n_labels/r_labels 可返回 component_count
- [ ] Content-Type 非 json 时返回 400

### D6 /api/v1/algorithms/subgraph-matching
- [ ] 最小模式（1 条边 2 个点）可返回 count
- [ ] direction=IN/OUT 正常；非法 direction 不崩溃（结果可为 0）
- [ ] Content-Type 非 json 时返回 400

---

## E. 权限与安全（当前为“观察项”）
当前代码未实现鉴权/授权；若接入生产环境，至少补：
- [ ] 基本鉴权（token / mTLS / 内网 ACL）
- [ ] 请求体大小限制/防止大 JSON DoS
- [ ] 日志脱敏（如未来接入敏感数据）

---

## F. 性能与稳定性
- [ ] 压测 3 个核心接口（k-hop / common-neighbors / subgraph-matching），记录 P50/P95/P99
- [ ] 长时间运行 1~2 小时无明显内存增长（粗略检查）
- [ ] 并发请求下无崩溃（Drogon thread_num=hardware_concurrency）

---

## G. 回归范围
- 涉及：导入逻辑 / edge key / reader 扫描 → 必回归 B/C/D + 性能小测
- 涉及：接口参数解析 → 必回归 D 全量
