# 城市新能源汽车充电资源调度仿真系统

一个使用 C11、GTK 4、图算法和优先队列实现的新能源汽车充电调度仿真项目。系统在可交互路网上模拟车辆提交充电请求、行驶、排队、充电、设备故障与恢复，并提供“综合推荐 + 动态优先级”和“最近站点 + 先来先服务”两种实验模式的实时对比。

## 主要功能

- 加载道路节点、道路、车辆、充电站和充电桩 CSV 数据。
- 使用邻接表保存有向路网，使用 Dijkstra 计算最短通行时间并恢复路径。
- 综合行驶时间、预计排队时间、充电时间和低电量风险推荐充电站。
- 使用二叉最大堆维护动态优先级等待队列。
- 模拟车辆行驶、到站、排队、充电、完成以及充电桩故障与修复。
- 支持车辆分批提交、每 10 个仿真分钟自动提交和持续车流。
- 支持项目算法与对照算法分别运行，并统计完成时间、排队时间、行驶时间和吞吐量。
- 提供可缩放、拖动、点击的 GTK 4 地图界面，以及可折叠、可调宽度的操作面板。

## 算法与数据结构

| 领域 | 实现 |
| --- | --- |
| 道路网络 | 邻接表、带权有向图、道路等级 |
| 路径规划 | 单源 Dijkstra、二叉最小堆、前驱路径恢复 |
| 排队调度 | 二叉最大堆、动态优先级、先来先服务对照队列 |
| 实体查询 | 拉链哈希表和编号数组 |
| 充电桩管理 | 单链表 |
| 仿真推进 | 固定时间步和 GTK 周期定时器 |
| 数据交换 | CSV 原子加载与状态保存 |

动态优先级计算公式为：

```text
优先级 = 紧急程度 × 30
       + (100 - 当前电量百分比) × 0.5
       + 等待时间 × 0.2
```

推荐评分以总完成时间为主体：

```text
总时间 = 行驶时间 + 预计等待时间 + 预计充电时间
评分   = 总时间 + 低电量风险分
```

## 系统流程

```mermaid
flowchart LR
    A[加载 CSV 数据] --> B[建立路网与实体索引]
    B --> C[车辆提交请求]
    C --> D[Dijkstra 计算候选路径]
    D --> E[估算等待和充电时间]
    E --> F[选择充电站]
    F --> G[车辆行驶]
    G --> H[到站排队]
    H --> I[分配充电桩]
    I --> J[推进充电]
    J --> K[完成并释放资源]
```

车辆状态变化：

```text
空闲 → 前往充电站 → 排队 → 充电 → 完成
```

## 模块结构

| 模块 | 目录 | 职责 |
| --- | --- | --- |
| 程序入口 | [`src/app`](src/app/README.md) | 命令行演示程序和整体调用流程 |
| 公共模型 | [`src/common`](src/common/README.md) | 状态枚举、核心实体和容量限制 |
| 模块 1 | [`src/module1_graph`](src/module1_graph/README.md) | 图、道路管理、Dijkstra 和路径恢复 |
| 模块 2 | [`src/module2_scheduler`](src/module2_scheduler/README.md) | 动态优先级最大堆和等待时间估算 |
| 模块 3 | [`src/module3_entity`](src/module3_entity/README.md) | 哈希索引和实体生命周期管理 |
| 模块 4 | [`src/module4_charger`](src/module4_charger/README.md) | 充电桩链表、分配、更新和释放 |
| 模块 5 | [`src/module5_recommend`](src/module5_recommend/README.md) | 充电站候选过滤、评分和排序 |
| 模块 6 | [`src/module6_simulation`](src/module6_simulation/README.md) | 固定时间步业务仿真 |
| 模块 7 | [`src/module7_fileio`](src/module7_fileio/README.md) | CSV 加载、校验与保存 |
| 模块 8 | [`src/module8_ui`](src/module8_ui/README.md) | GTK 4 界面、地图和实验功能 |
| 模块 9 | [`src/module9_timer`](src/module9_timer/README.md) | GTK 定时器和动画时间累计 |

## 当前示例数据

示例数据位于 [`data/input`](data/README.md)，当前规模为：

- 305 个图节点：240 个道路节点、60 个车辆起点、5 个充电站入口；
- 442 条道路记录；
- 60 辆车，分布在 60 个不同位置；
- 5 个充电站；
- 24 个充电桩，每站 4～6 个，功率为 60、90、120 或 180 kW。

这些数据用于课程设计仿真，仅表达相对路网关系，不是现实导航或实时交通数据。

## 目录结构

```text
.
├─ assets/                   地图图标及第三方资源许可
├─ build/                    编译输出
├─ data/
│  ├─ input/                仿真输入 CSV
│  └─ output/               程序和测试输出
├─ src/
│  ├─ app/                  命令行入口
│  ├─ common/               公共数据模型
│  ├─ module1_graph/        路网与路径算法
│  ├─ module2_scheduler/    排队调度
│  ├─ module3_entity/       实体管理
│  ├─ module4_charger/      充电桩管理
│  ├─ module5_recommend/    站点推荐
│  ├─ module6_simulation/   业务仿真
│  ├─ module7_fileio/       CSV 文件读写
│  ├─ module8_ui/           GTK 4 前端
│  └─ module9_timer/        GTK 定时控制
├─ tests/                    单元测试和整体联调测试
├─ Makefile                  GNU Make 构建规则
├─ 构建GTK4.bat             Windows GTK 前端构建脚本
└─ 启动GTK4.bat             Windows 启动脚本
```

## 构建环境

项目使用 C11 和 GTK 4。Windows 构建脚本默认使用：

```text
C:\msys64\ucrt64\bin
```

可在 MSYS2 UCRT64 环境安装 GCC、GTK 4 和 pkg-config：

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-pkgconf make
```

## 构建与运行

### Windows 脚本

在项目根目录运行：

```bat
构建GTK4.bat --no-pause
启动GTK4.bat
```

生成的前端程序为：

```text
build/charging_system_gtk4.exe
```

### GNU Make

```bash
make          # 构建 GTK 前端和命令行演示程序
make gui      # 只构建 GTK 前端
make test     # 构建并运行全部测试
make clean    # 删除 Makefile 管理的编译产物
```

运行程序时应把项目根目录作为当前工作目录，因为输入数据和图标使用相对路径加载。

## 使用说明

1. 启动程序后，系统自动加载 `data/input` 中的五个 CSV 文件。
2. 在左侧输入或点击地图选择车辆，计算推荐并选择目标站点。
3. 可手动提交当前车辆，也可使用“提交下一批”或自动提交。
4. 点击“开始”只启动仿真时钟，不会自动提交第一批车辆。
5. 顶部可设置实验模式、持续车流、仿真时长和运行速度。
6. 右侧可查看站内充电和排队车辆，并维护充电桩。
7. 底部统计表实时对比两种实验模式。

## 实验模式与统计指标

- **本项目模式**：使用综合推荐和动态优先级队列。
- **对照实验模式**：选择行驶时间最近的有效站点，站内采用先来先服务。

统计指标包括：

- 平均、最大总完成时间；
- 平均、最大排队时间；
- 平均行驶时间；
- 累计完成车辆数；
- 单位时间完成车辆数。

## 测试

测试覆盖图、调度器、实体、充电桩、推荐、仿真、文件读写、定时器和完整后端流程。详见 [`tests/README.md`](tests/README.md)。

```bash
make test
```

## 数据格式

输入文件包括 `nodes.csv`、`roads.csv`、`stations.csv`、`chargers.csv` 和 `vehicles.csv`。字段、枚举值和约束详见 [`data/README.md`](data/README.md)。

## 第三方资源

车辆和充电站图标来自 Lucide Icons，按照 ISC License 使用。许可证全文位于 [`assets/icons/LUCIDE-LICENSE.txt`](assets/icons/LUCIDE-LICENSE.txt)。

## 开源许可

项目源代码按照 [MIT License](LICENSE) 开源。第三方资源仍遵循各自的许可证。

