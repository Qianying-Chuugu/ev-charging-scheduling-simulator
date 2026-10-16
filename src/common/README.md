# 公共数据模型

## 模块定位

`model.h` 定义所有后端模块共享的容量限制、状态枚举和业务实体。该文件只提供数据结构，不实现业务函数。

## 容量限制

| 常量 | 默认值 | 含义 |
| --- | ---: | --- |
| `NAME_LEN` | 64 | 名称和短说明的最大长度 |
| `MAX_VEHICLES` | 100 | 最大车辆数 |
| `MAX_STATIONS` | 30 | 最大充电站数 |
| `MAX_CHARGERS` | 100 | 最大充电桩数 |
| `MAX_REQUESTS` | 200 | 最大有效请求数 |
| `MAX_GRAPH_NODES` | 2048 | 最大图节点数 |
| `MAX_PATH_NODES` | 2048 | 单条恢复路径的最大节点数 |

## 状态枚举

### `VehicleState`

```text
VEHICLE_IDLE → VEHICLE_TRAVELING → VEHICLE_WAITING
             → VEHICLE_CHARGING → VEHICLE_FINISHED
```

### `ChargerState`

- `CHARGER_FREE`：空闲；
- `CHARGER_BUSY`：正在充电；
- `CHARGER_FAULT`：故障。

### `RequestState`

- `REQUEST_TRAVELING`：车辆正在前往目标站点；
- `REQUEST_WAITING`：请求位于站点等待队列；
- `REQUEST_CHARGING`：请求已分配充电桩。

完成后的请求会从实体仓库注销并释放，因此没有单独的“已完成请求”状态。

## 核心结构

### `Vehicle`

保存编号、当前和初始位置、电量、目标电量、电池容量、紧急程度、运行状态、目标站点、当前充电桩、到达时间和当前请求。

### `ChargeRequest`

连接车辆与目标站点，保存请求时间、到达时间、到站电量、等待时间、优先级、剩余充电量、序列号和请求状态。

### `Charger`

保存充电桩编号、功率、初始和当前状态、正在处理的请求、剩余工作时间以及链表后继指针。

### `Station`

保存站点编号、道路节点、名称、充电桩链表以及由最大堆维护的等待请求数组。

## 对象关系

```text
Vehicle ←→ ChargeRequest ←→ Station
                              ├─ Charger 链表
                              └─ ChargeRequest 最大堆
```

指针关系由实体、充电桩和仿真模块共同维护。删除或重置对象时必须同步解除引用，避免悬空指针。

## 使用注意

- 修改容量常量时应检查栈上临时数组和测试数据规模。
- 枚举整数值会写入 CSV 输出，不应在缺少迁移方案时随意调整顺序。
- 业务模块应通过公开接口改变实体状态，不应绕过约束直接释放对象。

