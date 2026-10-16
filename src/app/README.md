# 命令行程序入口

## 模块定位

`main.c` 是不依赖图形界面的后端演示入口，用于展示从 CSV 加载、站点推荐、请求提交、时间步推进到结果保存的完整流程。

## 主要功能

- 初始化 `Graph`、`EntityStore` 和 `Simulation`；
- 加载 `data/input` 中的五个 CSV 文件；
- 为全部空闲车辆计算推荐并提交排名第一的有效站点；
- 按固定时间步推进仿真并定期打印状态；
- 将车辆最终状态保存到 `data/output/vehicles_result.csv`；
- 在退出前释放路网和实体内存。

## 内部函数

| 函数 | 作用 |
| --- | --- |
| `vehicle_state_name` | 把车辆状态枚举转换为英文文本 |
| `print_recommendations` | 输出一辆车的候选站点及时间分解 |
| `submit_demo_requests` | 为所有空闲车辆选择最佳有效站点并提交请求 |
| `print_status` | 输出当前仿真时间和全部车辆状态 |
| `main` | 组织加载、仿真、保存和清理流程 |

这些函数仅供命令行程序内部使用，不属于后端公共 API。

## 实现流程

```text
初始化 Graph 与 EntityStore
        ↓
fileio_load_all
        ↓
simulation_init(step_minutes = 1.0)
        ↓
recommend_all / recommend_best
        ↓
simulation_submit
        ↓
循环 simulation_step
        ↓
fileio_save_vehicles
        ↓
graph_free / entity_free
```

循环在没有活动请求或达到 500 个时间步时停止。正常示例数据应在上限内全部完成。

## 输入与输出

程序使用相对于项目根目录的路径：

```text
data/input/nodes.csv
data/input/roads.csv
data/input/stations.csv
data/input/chargers.csv
data/input/vehicles.csv
data/output/vehicles_result.csv
```

因此运行 `backend_demo.exe` 时应将项目根目录设为当前工作目录。

## 依赖模块

- 模块 1：路网和路径；
- 模块 3：实体仓库；
- 模块 5：站点推荐；
- 模块 6：业务仿真；
- 模块 7：文件读写。

## 构建

```bash
make build/backend_demo.exe
```

