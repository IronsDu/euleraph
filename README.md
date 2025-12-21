* 1. ```bash
       # Euleraph
       
       Euleraph 是面向评测/比赛场景的单机图存储与算法查询服务，使用 **C++20 + Drogon** 提供 HTTP API，使用 **WiredTiger** 进行本地持久化存储，并提供数据导入能力将输入文件写入图存储。
       
       本 README 只保留三件“上手必看”的内容：
       
       1. **如何编译项目**
       2. **存储设计概览**：我们如何存储 Label / Relation / Vertex / Edge，并支持哪些查询
       3. **多人协作开发结构**：分层与模块边界，便于并行开发
       
       更详细的设计、接口、数据字典、测试与发布回滚都在 `docs/` 下（见下方导航）。
       
       ---
       
       ## 文档导航（v1）
       
       > 文档路径：`euleraph/euleraph/docs`
       
       - 需求卡（PRD-lite）：`docs/requirements/01_prd_lite.md`
       - 技术方案（Design Doc Lite）：`docs/design/01_design_lite.md`
       - API 文档（含 curl 示例）：`docs/api/01_api_spec.md`
       - 数据文档（WiredTiger Schema & 口径）：`docs/data/01_data_dictionary.md`
       - 测试清单（Checklist）：`docs/test/01_test_checklist.md`
       - 发布与回滚 Runbook：`docs/release/01_release_runbook.md`
       
       ---
       
       ## 编译（Ubuntu）
       
       ### 1) 安装系统依赖
       
       ​```bash
       sudo apt-get update
       sudo apt-get install -y swig python3-dev libtool autoconf autoconf-archive automake
       ​```
       
       ### 2) 安装 vcpkg（需要 VPN）
       
       我们使用 fork 版本：
       
       ​```bash
       git clone https://github.com/IronsDu/vcpkg.git
       cd vcpkg && ./bootstrap-vcpkg.sh
       ​```
       
       ### 3) 构建本项目（需要 VPN）
       
       在项目根目录执行（示例为 Release）：
       
       ​```bash
       cmake -B ./build -S .   -DCMAKE_TOOLCHAIN_FILE=<PathToVcpkgDir>/scripts/buildsystems/vcpkg.cmake   -DCMAKE_BUILD_TYPE=Release
       
       cmake --build ./build -j
       ​```
       
       产物通常位于：
       
       - `./build/euleraph`
       
       ### 4) IDE 支持（可选）
       
       如需 Windows + Visual Studio + WSL 的 CMake 开发方式，请参考团队知识库文档（内网链接）：
       
       - Windows 下使用 Visual Studio 通过 CMake/WSL 开发 Linux 程序
       
       ---
       
       ## 运行
       
       > 启动参数与示例以 Runbook 为准：`docs/release/01_release_runbook.md`
       
       典型流程：
       
       1. 首次运行：初始化 DB + 导入数据 + 启动服务
       2. 后续运行：复用 DB 目录直接启动服务
       3. 用 `/ping` 与核心算法接口做冒烟验证
       
       ---
       
       ## 存储设计概览（四类对象）
       
       图存储涉及四类对象：**Label 类型、Relation 类型、Vertex 点实例、Edge 边实例**。  
       设计目标：把用户可读的字符串表示压缩为内部数字 ID 表示，减少存储与读取开销，并支持题目查询需求（以“一度邻接”作为底座组合更高阶算法）。
       
       ### 1) Label 类型（LabelType）
       
       - 用户侧：`string` label name
       - 内部：`label_type_id(int64)`
       - 需要支持：
         - label name → label_type_id
         - label_type_id → label name
       - 实现细节：见 `docs/data/01_data_dictionary.md`
       
       ### 2) Relation 类型（RelationType）
       
       - 用户侧：`string` relation name
       - 内部：`relation_type_id(int64)`
       - 需要支持：
         - relation name → relation_type_id
         - relation_type_id → relation name
       - 实现细节：见 `docs/data/01_data_dictionary.md`
       
       ### 3) Vertex 点实例（Vertex）
       
       - 用户侧：`id(string)`（通常称 pk / vertex_ident）
       - 内部：`vertex_id(int64)`（全局唯一）
       - 需要支持：
         - id → vertex_id（及反向）
       - 实现细节：见 `docs/data/01_data_dictionary.md` 与 API 中对 pk 的解释：`docs/api/01_api_spec.md`
       
       ### 4) Edge 边实例（Edge）
       
       - 用户侧：五元组（起点 label+id、relation、终点 label+id）
       - 内部：用数字 ID 组合存储；key 的字段顺序支持前缀扫描
       - 需要支持：
         - 指定起点的一度邻接（按方向）
         - 指定起点 + relation 的一度邻接（按方向 + 关系过滤）
       - 实现细节：见 `docs/data/01_data_dictionary.md`
       
       ---
       
       ## 系统结构（分层与模块）
       
       Euleraph 分为三层：通信层、计算层、存储层。为支持多人并行开发，存储层对外提供 **Reader/Writer 抽象接口**；算法层与通信层只依赖接口与基础类型即可推进。
       
       ### 0) 基本类型
       
       - `src/interface/types/types.hpp`
       
       ### 1) 存储层（Storage）
       
       - 抽象接口：
         - `src/interface/storage/reader.hpp`
         - `src/interface/storage/writer.hpp`
       - 实现：
         - `src/storage/*.hpp`
         - `src/storage/*.cpp`
       
       ### 2) 算法层（Algo）
       
       - 抽象接口：
         - `src/interface/algo/*.hpp`
       - 实现：
         - `src/algo/*.hpp`
         - `src/algo/*.cpp`
       
       ### 3) 通信层（HTTP / Drogon）
       
       - `src/service/http/*.hpp`
       - `src/service/http/*.cpp`
       - 路由注册与启动：`src/main.cpp`
       
       ---
       
       ## API 快速入口
       
       接口说明与示例见：`docs/api/01_api_spec.md`
       
       最小冒烟建议：
       
       - `GET /ping`
       - `POST /api/v1/algorithms/wcc`
       - `POST /api/v1/algorithms/k-hop-neighbors`
       
       ---
       
       ## 测试
       
       建议按 Checklist 跑最小回归集：
       
       - `docs/test/01_test_checklist.md`
       
       ---
       
       ## 发布与回滚
       
       Runbook：
       
       - `docs/release/01_release_runbook.md`xxxxxxxxxx sudo apt-get updatesudo apt-get install -y swig python3-dev libtool autoconf autoconf-archive automakebash
       ```