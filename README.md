## 概述

从文档大概有三个目的：

1. **介绍如何编译项目**
1. **存储设计**：在这个文档中，我们先探讨下存储格式，看看如何存储各种数据的，描述它能满足哪些查询需求。
1. **多人协作开发**：然后探讨下系统的分层结构和模块化，以更好的实现多人协作并行开发。我们会以此安排任务，当然，具体的工作还需要详细勾兑进行细化（并记录到此文档里）。

> 此文档，会且应该不断地更新

## 编译
1. **安装系统依赖**
    - Ubuntu下使用下面命令安装：
        -  `apt-get install swig python3-dev libtool autoconf autoconf-archive automake`
1. **安装vcpkg(需要VPN)**
    1. clone 我们使用fork过的[https://github.com/IronsDu/vcpkg](https://github.com/IronsDu/vcpkg)
        - `git clone https://github.com/IronsDu/vcpkg.git`
    1. 运行clone得到的vcpkg文件夹中的`bootstrap-vcpkg.sh`脚本，完成vcpkg的编译
        - `cd vcpkg & ./bootstrap-vcpkg.sh`
1. **构建本项目(需要VPN)**
    1. 先在项目根目录下，运行以下命令生成CMake的build工程，默认为`RelWithDebInfo`版本,通过`-DCMAKE_BUILD_TYPE=Release`生成Release版本.
    ```bash
    cmake -B ./build -S . -DCMAKE_TOOLCHAIN_FILE=<PathToVcpkgDir>/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
    ```
    2. 使用cmake编译程序（我们后续可以指定编译Release）
    ```bash
    cmake --build ./build
    ```
1. **IDE支持**
    1. 参考我们知识库中的文档：[Windows下使用Visual Studio通过CMake/WSL开发Linux程序](http://10.86.11.249:8090/pages/viewpage.action?pageId=84803838)。


## 存储格式
这个章节讲解我们如何存储图存储里所涉及的一些对象（有四个：Label类型、Relation类型、Label点实例、Relation边实例），边的存储设计会影响其他类型的存储格式，但我们会最后讲边的存储格式，所以在其他设计中有疑惑的时候很正常，在看到边的设计时就自然明白了。

我们会在每个小章节中介绍每个类型会存在哪些查询需求，以及相关的存储格式，通常而言，存储格式应该直观的表达了它是如何支持查询需求的，如果不是很直观，我们会进行说明。

### 1. Label类型
* 对于用户而言，使用一个`string`标识一个Label类型
* 对于存储内部而言，每一个Label类型对应一个全局唯一的`int64_t`的数字型别的类型id，记为`label_type_id`。
* 查询相关需求
    1. 从Label类型字符串获取其对应的数字型别的类型id。
        * 用于将用户的查询语句中指定的label类型字符串转换为内部的数字id，因为内部的边实例中存储的Label类型时使用的数字id，详见关系存储章节
    2. 从数字型别的类型id获取Label类型字符串。
        * 用于在最终返回可阅读的结果集给用户时，将内部的数字id转换为类型字符串
* 相关存储格式
    * `<Label Type>(string)` -> `<label_type_id>(int64_t)`
    * `<label_type_id>(int64_t)` -> `<Label Type>(string)`
> `->`表示这是一个唯一映射，我们大概率会使用kv来存储映射关系。下文同。当然，我们的`key`只是一个二进制binary，它不一定是一个可打印的字符串！
> `=>`会在后文出现，表示一个非唯一映射。

> `<Label Type>(string)`中的`(string)`仅为了说明它是字符串类型，它并不作为一个字符串存储在文件中！下文同。

### 2. Relation类型
* 对于用户而言，使用一个`string`标识一个Relation类型
* 对于存储内部而言，每一个Relation类型对应一个全局唯一的`int64_t`的数字型别的类型id，记为`relation_type_id`
* 查询相关需求
    1. 从Relation类型字符串获取其对应的数字型别的类型id。（同理Label类型章节中的描述）
    2. 从数字型别的类型id获取Relation类型字符串。（同理Label类型章节中的描述）
* 相关存储格式
    * `<Relation Type>(string)` -> `<relation_type_id>(int64_t)`
    * `<relation_type_id>(int64_t)` -> `<Relation Type>(string)`

### 3. Label点实例(vertex实例)
* 对于用户而言，每一个点都有一个`string`类型的`id`标识，这个表示在Label空间内唯一，不同Label下可能存在相同的`id`。
* 对于存储内部而言，每一个Label点实例有一个全局唯一的`int64_t`的数字id，记为`vertex_id`。
    * `id`在不同的Label下都不会冲突。这是为了在一些算法的实现中，简化数据结构的设计和实现。
* 查询相关需求
    1. 根据`Label类型字符串`和`id`标识获取其全局唯一的数字id。
        * 用于执行用户的查询语句时，先获取点的内部数字id，用于后续的查询
    2. 根据点实例的数字id获取`Label类型字符串`和`id`标识。
        * 因为边的存储和图的算法实现中都是使用数字id，那就按么需要在最终返回可阅读的结果集给用户时，将id转换为字符串标识）
* 相关存储格式
    * `<label_type_id>#<id>` -> `<vertex_id>(int64_t)`
        * `label_type_id`是Label类型对应的数字id
        * `id`是点的string类型的标识
        * `#`表示这是一个连接符，它是实际存储中的一部分！
    * `<vertex_id>(int64_t)` ->  `<label_type_id>#<id>`
    * `<id>` => `<label_type_id>#<vertex_id>(int64_t)`
        * 注意！这是一个非唯一映射，用于可以查询指定字符串id对应的Label类型id和点id
            * 因为比赛题目中，可以不指定label type。
* TODO
    * `vertex_id`如何生成和得到？是否可以使用自增id？

### 4. Relation边实例(edge实例)
* 对于用户而言，每一个边实例由五元组组成：{`<label类型>(string)`, `<label id>(string)`, `<Relation Type>(string)`, `<label类型>(string)`, `<label id>(string)`}。它们都是字符串来表示的，如果直接存储，则空间消耗很大，查询时需要读取的数据量也很大。正如前文所述，我们已经将这五个部分都在内部映射到了数字id，因此我们在内部也会使用数字id来存储一条边实例。
* 查询相关需求
    1. 指定起点的邻接查询
        * 指定起点查询它的一度邻接。我们在存储上只关注一度邻接。因为通过一度邻接可以实现多度邻接。
    2. 指定起点id和关系类型的邻接查询
    
    3. 我们是不是还要支持不指定点，只指定关系类型的邻接查询？
* 相关存储格式
    * `<vertex_id>#<direction>#<relation_type_id>#<end_vertex_id>` -> `<end_label_type_id>`
        * key即表示一条完整的边
        * `direction`表示方向
        * 通过它可以满足指定点的查询（不区分关系类型），也可以满足带关系类型的查询。指定关系类型时，也必须指定方向。
        * 当然，我们实际的底层存储支持的查询接口中是必须指定方向的，只不过当用户不指定方向时，我们会分别查询出边和入边.
        * 虽然vertex_id本就是全局唯一，且能够通过它获取其对应的label类型，但我们在此映射中的value中还是冗余存储了label_type_id，这是为了能够更快的实现终点的标签过滤。

## 系统结构
整个图数据库（目前仅限于比赛场景），我们分为：通信层、计算层、存储层。我们会在存储层提炼出一些抽象接口，用于多人并行开发，下游模块使用接口开发，而不用等待组员完成模块开发才继续开发，即，我们提炼出存储的抽象接口后，计算层就可以开始编写算法了）。同时，也会定义一些基本的数据类型，那么通信层也可以编写代码了。

### 模块
### 0. 基本类型定义
* 定义各个模块交互所需的基本类型
    * 各个模块就可以分别编写代码，使用基本类型进行交互即可。
* 代码位置
    * `src/interface/types/types.hpp`

### 1. 存储层
为了更好的模块化，我们分别提炼出`writer`和`reader`两个**抽象接口**，这俩接口使用存储内部的表示来进行，它是一个低阶接口，前者用于写入数据相关的接口，后者用于查询相关的接口。
这两个抽象接口用于算法层和数据写入请求，当然，我们可能还需要抽象出更高级的接口，这些接口可以更好的对应到用户的读写语句（比如是以字符串表示来表示读写）

* 抽象接口
    * 代码位置
        * `src/interface/storage/reader.hpp`
        * `src/interface/storage/writer.hpp`
* 实现接口
    * 代码位置
        * `src/storage/*.hpp`
        * `src/storage/*.cpp`

### 2. 算法层

* 抽象接口
    * 代码位置
        * `src/interface/algo/*.hpp`
* 实现接口
    * 代码位置
        * `src/algo/*.hpp`
        * `src/algo/*.cpp`

### 3. 通信层
* 定义HTTP请求和响应的结构
    * 它可以独立于算法的实现，就可以编写通信层的代码。算法只是返回通信层所需的结构对象就行。
* 我们采用[`drogon`](https://github.com/drogonframework/drogon)库来编写。

* 代码位置
    * `src/service/http/*.hpp`
    * `src/service/http/*.cpp`

## 题目解构
这里阐述此次比赛相关题目所需要的查询需求，以衡量我们的设计是否能够满足题目。

TODO


## 任务划分

1. **基本数据结构定义**


* 任务列表:
    - [x] 标签类型
        * 标签类型的标识的类型(string)
        * 标签类型的数字id的类型(int64_t)
    - [x] 边类型
        * 边类型的标识的类型(string)
        * 边类型的数字id的类型(int64_t)
    - [x] 点实例
    - [x] 边实例
2. **存储层接口的定义**

* 依赖的任务:
    1. (1)

* 任务列表:
    - [x] 创建标签类型的接口
    - [x] 创建边类型的接口
    - [x] 数据写入的接口
        * 写点实例的接口
        * 写边实例
    - [x] 查询数据的接口
        * 根据字符串查询标签类型id
        * 根据字符串查询边类型id
        * 根据(标签类型, 点id标识)查询点实例id
        * 根据点实例id查询它所有的一度邻接

        > 定义好接口后，我们可以先实现memory，可以手动mock数据，以此来提供给算法等其他模块来测试。

3. **数据导入功能**
* 我们使用[xlnt](https://github.com/xlnt-community/xlnt)来读取excel文件。在`src\importer\importer.hpp`中实现功能。
* 依赖的任务:
    1. (1)
    2. (2)

* 任务列表:
    - [ ] 根据标签类型标识创建唯一数字id，并记录到数据库中
    - [ ] 根据边类型标识创建唯一数字id，并记录到数据库中
    - [ ] 根据点的id标识创建唯一的数字id，并记录到数据库中
    - [ ] 使用存储层的抽象接口完成数据导入的功能
4. **实现存储层的接口**
* 依赖的任务:
    1. (1)
    2. (2)

* 任务列表:
    可以采用[`rocksdb`](https://github.com/facebook/rocksdb)或者[`wiredtiger`](https://github.com/wiredtiger/wiredtiger)来实现。

    - [] 实现WiredTiger的存储引擎
    
5. **设计算法的抽象接口**
* 依赖的任务:
    1. (1)

* 任务列表:

6. **根据存储层的接口实现算法**
* 依赖的任务:
    1. (1)
    2. (2)

7. **根据算法接口实现HTTP接口**
在`src\http\euleraph_http_handle.hpp`中添加和实现接口。
* 依赖的任务:
    1. (1)
    2. (2)
    3. (5)