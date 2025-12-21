# 01 发布 & 回滚 Runbook：Euleraph

> 适用：开发/评测环境快速启动、迭代发布、出问题可快速回滚。  
> 更新时间：2025-12-21

---

## 1. 版本与交付物
- 交付物：可执行文件 `euleraph`（CMake 生成，默认在 `./build/euleraph`）
- 运行依赖：
  - WiredTiger 动态库/头文件（通过 vcpkg 或系统包）
  - Drogon、spdlog、args、xlnt 等（见 `vcpkg.json` 与 CMake）

---

## 2. 构建（Build）
### 2.1 安装系统依赖（Ubuntu 示例）
README 中列出的依赖（供参考）：
- `swig python3-dev libtool autoconf autoconf-archive automake`

### 2.2 使用 CMake 构建
```bash
mkdir -p build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物：
- `build/euleraph`

---

## 3. 启动参数（CLI）
程序使用 `args.hxx`，主要参数如下（以源码为准）：

| 参数 | 必填 | 默认 | 说明 |
|---|---:|---:|---|
| --database_dir <path> | ✅ | - | WiredTiger DB 目录（会创建/初始化 schema） |
| --need_import | ❌ | false | 是否导入数据 |
| --data_path <path> | need_import=true 时必填 | - | 输入数据文件路径（当前实现读取 xlsx） |
| --batch_size <int> | ❌ | 1000 | 导入批大小 |
| --concurrency <int> | ❌ | 1 | 导入并发（目前建议保持 1） |
| --port <int> | ❌ | 8200 | HTTP 端口 |
| --cache_size <int> | ❌ | 2048 | WiredTiger cache（MB） |
| --log_level <str> | ❌ | info | spdlog 日志级别 |
| --csv_row_num <int> | ❌ | all | 限制导入行数（>0） |

> 注意：仓库中 `run.sh` 的参数示例（`-f/--db/--rebuild`）与当前 `main.cpp` CLI 参数**不一致**。  
> 以 `src/main.cpp` 的 `parse_cli_args()` 为准。

---

## 4. 启动示例
### 4.1 首次启动（初始化 + 导入 + 启动服务）
```bash
./build/euleraph   --database_dir ./euleraph_db   --need_import   --data_path ./test/data/sample_data.xlsx   --batch_size 1000   --concurrency 1   --port 8200   --cache_size 2048   --log_level info
```

### 4.2 仅启动服务（复用已导入数据库）
```bash
./build/euleraph --database_dir ./euleraph_db --port 8200
```

---

## 5. 上线验证（Smoke / 验证步骤）
### 5.1 基础连通性
```bash
curl -i http://127.0.0.1:8200/ping
```

### 5.2 核心接口冒烟（示例）
```bash
curl -s http://127.0.0.1:8200/api/v1/algorithms/wcc   -H 'Content-Type: application/json'   -d '{"n_labels":["Person"],"r_labels":["Like"]}'
```

### 5.3 日志检查
- 关注是否出现：
  - WiredTiger 初始化失败
  - 导入异常/解析失败
  - HTTP handler 报错（spdlog error）

---

## 6. 回滚（Rollback）
Euleraph 当前为单二进制 + 本地 DB 目录，回滚分两类：

### 6.1 代码版本回滚（最常见）
1) 将可执行文件回滚到上一个已知稳定版本（例如替换 `build/euleraph` 或切回 git tag 后重新构建）
2) **复用原 DB 目录** 启动
3) 跑 `ping + 关键接口` 验证

### 6.2 数据回滚/重建（当 schema 或导入逻辑变更）
如果本次变更影响了存储编码或 schema（例如 edge_key 字段顺序）：
1) 停服
2) 备份当前 DB 目录（以便问题排查）
```bash
cp -a ./euleraph_db ./euleraph_db.bak.$(date +%Y%m%d%H%M%S)
```
3) 删除/更换 DB 目录并重新导入
```bash
rm -rf ./euleraph_db
./build/euleraph --database_dir ./euleraph_db --need_import --data_path <file.xlsx> ...
```

---

## 7. 常见故障与排查
### 7.1 启动报 “not found vertex”
- 可能原因：API 内部通过默认 label_type_id 将 pk 转 VertexId，与你的数据 label 不一致  
- 解决：
  - 确认导入数据中 label 定义
  - 调整接口实现：将 label 参数透传或实现 “pk→vertex_id” 的无 label 查找

### 7.2 Content-Type 错误导致 400
- 算法接口要求 `Content-Type: application/json`，curl 必须带 header

### 7.3 WiredTiger 打不开或权限问题
- 确认 `--database_dir` 目录有读写权限
- 若目录为旧版本残留，尝试备份后重建

---

## 8. 发布记录（可按迭代补充）
- 2025-12-21: 初版 runbook（基于当前 main.cpp / storage schema / API handlers）
