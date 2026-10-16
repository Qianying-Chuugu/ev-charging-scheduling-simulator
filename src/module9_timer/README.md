# 模块 9：GTK 定时控制

## 模块定位

模块 9 封装 GTK 主循环定时器和固定时间步累计逻辑。它不修改车辆、请求或充电桩，真正的业务状态转换由模块 6 的 `simulation_step` 完成。

## 核心结构 `TimerControl`

| 字段 | 作用 |
| --- | --- |
| `source_id` | GTK 定时源编号，`0` 表示未运行 |
| `last_frame_us` | 上次回调的单调时钟时间 |
| `accumulator_ms` | 尚未转换为完整时间步的累计毫秒数 |
| `step_interval_ms` | 一个仿真时间步对应的真实毫秒数 |

## 公开函数

| 函数 | 作用 |
| --- | --- |
| `timer_init` | 初始化控制器和时间步间隔 |
| `timer_start` | 使用 `g_timeout_add` 注册周期回调 |
| `timer_stop` | 主动移除定时源并清空累计时间 |
| `timer_finish` | 回调自然结束时清除运行状态 |
| `timer_collect_steps` | 把真实经过时间转换为本次应推进的步数 |
| `timer_progress` | 返回当前时间步的动画进度，范围 0～1 |
| `timer_is_running` | 判断定时器是否已启动 |

## 工作流程

```text
timer_start
     ↓
GTK 周期回调
     ↓
timer_collect_steps
     ↓
执行 0～N 个 simulation_step
     ↓
timer_progress 计算帧间动画位置
```

`timer_collect_steps` 使用单调时钟，避免系统时间调整影响仿真。单次真实耗时最多按 100 ms 累计，防止窗口阻塞后一次补做过多仿真步。

## 倍速调整

前端通过修改 `step_interval_ms` 改变仿真速度，并按比例保留当前累计进度，从而减少切换速度时的动画跳变。

## 注意事项

- 重复调用 `timer_start` 不会创建第二个定时源。
- GTK 回调返回移除状态后，应调用 `timer_finish`，而不是再次移除同一来源。
- 该模块必须在 GTK/GLib 主循环环境中使用。

## 依赖

依赖 GLib；业务上由模块 8 调用，并间接驱动模块 6。

