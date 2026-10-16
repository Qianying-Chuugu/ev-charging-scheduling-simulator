# 模块 6：动态业务仿真

## 模块定位

模块 6 是后端业务流程的统一控制器，负责请求提交、车辆到达、排队、充电桩分配、充电推进、设备故障恢复和仿真重置。其他业务模块提供数据结构与局部操作，本模块负责按时间顺序组合它们。

## 核心结构 `Simulation`

| 字段 | 作用 |
| --- | --- |
| `graph` | 当前道路图 |
| `store` | 当前实体仓库 |
| `current_time` | 仿真时间，单位为分钟 |
| `step_minutes` | 每次推进的固定时间步 |
| `next_request_id` | 下一条请求编号 |

## 公开函数

### 控制与业务

| 函数 | 作用 |
| --- | --- |
| `simulation_init` | 绑定图和仓库并设置时间步 |
| `simulation_submit` | 根据有效推荐创建并提交车辆请求 |
| `simulation_step` | 推进一个固定时间步 |
| `simulation_reset` | 清除活动任务并恢复初始状态 |
| `simulation_has_active` | 判断是否仍有行驶、排队或充电车辆 |
| `simulation_break_charger` | 把充电桩设为故障，并重新排队原任务 |
| `simulation_repair_charger` | 把故障充电桩恢复为空闲状态 |

### 只读查询

| 函数 | 作用 |
| --- | --- |
| `simulation_vehicle_count` / `simulation_vehicle_at` | 遍历车辆 |
| `simulation_station_count` / `simulation_station_at` | 遍历站点 |
| `simulation_charger_count` / `simulation_charger_at` | 遍历指定站点的充电桩 |
| `simulation_waiting_count` / `simulation_waiting_vehicle_at` | 查看站点等待车辆 |
| `simulation_request_count` / `simulation_request_at` | 遍历当前有效请求 |

## 请求提交流程

`simulation_submit` 会验证车辆处于空闲状态、推荐有效且路径可达，然后创建请求并记录：

- 请求和到达时间；
- 目标站点；
- 到站预计电量；
- 需要补充的电量；
- 请求序列号和初始状态。

提交后车辆进入 `VEHICLE_TRAVELING`。行驶耗电不会在提交瞬间扣除，而是在车辆实际到站并成功进入等待队列时更新。

## 单步推进顺序

```text
current_time += step_minutes
        ↓
update_arrivals
        ↓
update_charging
        ↓
assign_waiting
```

1. 到达车辆进入目标站点最大堆。
2. 正在工作的充电桩推进充电；完成任务立即释放。
3. 各站点重建动态优先级堆，并为空闲充电桩分配请求。

## 故障处理

运行中充电桩发生故障时，正在服务的请求退回等待状态，车辆退回排队状态，再次加入原站点队列。修复后的设备恢复为空闲，可在后续时间步重新分配。

## 重置流程

重置会停止并释放全部活动请求、清空站点队列、恢复车辆初始位置和电量，并把充电桩恢复为输入数据记录的初始状态。

## 时间离散误差

状态转换只发生在时间步边界。例如预计 2.5 分钟到达、时间步为 1 分钟的车辆，会在第 3 分钟处理到站。减小时间步可降低误差，但会增加计算和界面刷新次数。

## 依赖

依赖模块 1、2、3、4 和 5。

