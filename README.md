# Euleraph
       
Euleraph 是面向评测/比赛场景的单机图存储与算法查询服务，使用`C++20`语言开发，使用 **Drogon** 提供 HTTP API，使用 **WiredTiger** 进行本地持久化存储，并提供数据导入能力将输入文件写入图存储。

---

## 文档导航（v1）

- [需求卡（PRD-lite）](docs/requirements/01_prd_lite.md)
- [技术方案（Design Doc Lite）](docs/design/01_design_lite.md)
- [API 文档（含 curl 示例）](docs/api/01_api_spec.md)
- [存储核心文档（WiredTiger Schema & 口径）](docs/data/01_data_dictionary.md)
- [测试清单（Checklist）](docs/test/01_test_checklist.md)
- [发布与回滚 Runbook](docs/release/01_release_runbook.md)

---

## 依赖环境

- C++20 编译器（推荐 g++ 11+ 或 clang 13+）
- [WiredTiger](https://source.wiredtiger.com/)
- [Drogon](https://github.com/drogonframework/drogon)
- vcpkg 包管理器（推荐使用项目指定 fork）

---

## 编译与构建（Ubuntu）

### 1. 安装系统依赖

 ```bash
sudo apt-get update
sudo apt-get install -y swig python3-dev libtool autoconf autoconf-archive automake
 ```

### 2. 安装 vcpkg（需要 VPN）

我们使用 fork 版本：

 ```bash
git clone https://github.com/IronsDu/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh
 ```

### 3. 构建本项目

在项目根目录执行（示例为 Release）：

 ```bash
cmake -B ./build -S .   -DCMAKE_TOOLCHAIN_FILE=<PathToVcpkgDir>/scripts/buildsystems/vcpkg.cmake   -DCMAKE_BUILD_TYPE=Release

cmake --build ./build -j
 ```

> `<PathToVcpkgDir>` 请替换为你本地 vcpkg 路径

产物通常位于：

- `./build/euleraph`

### 4) IDE 支持（可选）

如需 Windows + Visual Studio + WSL 的 CMake 开发方式，请参考团队知识库文档（内网链接）：

- Windows 下使用 Visual Studio 通过 CMake/WSL 开发 Linux 程序

---

## 运行

详细参数与示例见 [Runbook](docs/release/01_release_runbook.md)。

典型流程：

1. 首次运行：初始化 DB + 导入数据 + 启动服务
2. 后续运行：复用 DB 目录直接启动服务
3. 用 `/ping` 与核心算法接口做冒烟验证

---

## 存储设计概览

Euleraph 图存储涉及四类对象：**Label 类型、Relation 类型、Vertex 点实例、Edge 边实例**。  
设计目标：将用户可读的字符串压缩为内部数字 ID，减少存储与读取开销，支持高效查询。

详细设计见 [数据字典](docs/data/01_data_dictionary.md)。

## 系统结构（分层与模块）

Euleraph 分为三层：通信层、计算层、存储层。为支持多人并行开发，存储层对外提供 **Reader/Writer 抽象接口**；算法层与通信层只依赖接口与基础类型即可推进。

- **存储层**：Reader/Writer 抽象接口，见 `src/interface/storage/`
- **算法层**：见 `src/algo/`
- **通信层**：HTTP API，见 `src/service/http/`

### 1. 基本类型

- `src/interface/types/types.hpp`

### 2. 存储层（Storage）

- 抽象接口：
  - `src/interface/storage/reader.hpp`
  - `src/interface/storage/writer.hpp`
- 实现：
  - `src/storage/*.hpp`
  - `src/storage/*.cpp`

### 3. 算法层（Algo）

- 抽象接口：
  - `src/interface/algo/*.hpp`
- 实现：
  - `src/algo/*.hpp`
  - `src/algo/*.cpp`

### 4. 通信层（HTTP / Drogon）

- `src/service/http/*.hpp`
- `src/service/http/*.cpp`
- 路由注册与启动：`src/main.cpp`

---

## API 快速入口

详见 [API 文档](docs/api/01_api_spec.md)。

最小冒烟建议：
```bash
curl http://localhost:8200/ping curl -X POST http://localhost:8200/api/v1/algorithms/wcc -d '{"..."}' curl -X POST http://localhost:8200/api/v1/algorithms/k-hop-neighbors -d '{"..."}'
```

---

## 测试

建议按 [Checklist](docs/test/01_test_checklist.md) 跑最小回归集。

## 发布与回滚

详见 [Runbook](docs/release/01_release_runbook.md)。

## FAQ

- **Q: 编译报找不到 WiredTiger/Drogon？**  
  A: 请确认 vcpkg 路径正确，且已安装相关依赖。

- **Q: 导入大数据时写入速率下降？**  
  A: 建议定期 checkpoint，合理设置 cache_size，详见 [性能调优建议](docs/design/01_design_lite.md#性能调优)。

---