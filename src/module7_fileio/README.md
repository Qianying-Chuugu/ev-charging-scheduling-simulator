# 模块 7：CSV 文件读写

## 模块定位

模块 7 负责加载和保存路网、车辆、充电站及充电桩数据，并向调用方返回文件、行号和错误原因。完整加载采用临时对象，保证失败时不破坏原有有效数据。

## 核心结构

### `DataFiles`

保存五个基础数据文件路径：节点、道路、站点、充电桩和车辆。

### `LoadError`

| 字段 | 作用 |
| --- | --- |
| `file` | 出错文件路径 |
| `line` | 出错行号；无法打开文件时为 0 |
| `message` | 简短错误原因 |

## 公开函数

| 函数 | 作用 |
| --- | --- |
| `fileio_load_all` | 原子加载五个基础 CSV 文件 |
| `fileio_save_all` | 保存基础配置快照 |
| `fileio_save_vehicles` | 保存车辆当前运行状态 |

## 原子加载流程

```text
初始化临时 Graph 和 EntityStore
        ↓
节点 → 道路 → 站点 → 充电桩 → 车辆
        ↓
全部成功：释放旧数据并替换
任一失败：释放临时数据并保留旧数据
```

文件第一行为表头并被跳过，空行会被忽略。重复编号、非法数值、无效枚举或不存在的引用都会导致加载失败。

## 输入格式

### 节点

```csv
id,x,y,type
```

### 道路

```csv
from,to,distance_km,travel_minutes,two_way,road_level
```

`two_way=1` 时加载器自动添加反向边。

### 充电站

```csv
id,node_id,name
```

站点名称读取到行尾，支持 UTF-8 中文。

### 充电桩

```csv
id,station_id,power_kw,state
```

初始状态只接受空闲 `0` 或故障 `2`。

### 车辆

```csv
id,node_id,battery_percent,target_percent,capacity_kwh,urgency
```

完整字段说明参见 [`data/README.md`](../../data/README.md)。

## 保存规则

`fileio_save_all` 保存可重新加载的基础配置：

- 节点保留类型；
- 内部有向边分别写出，因此 `two_way` 保存为 `0`；
- 车辆保存初始位置和初始电量；
- 充电桩保存初始状态，运行中的忙碌状态不会写入基础配置。

`fileio_save_vehicles` 保存当前车辆状态：

```csv
id,node_id,battery_percent,state,station_id,charger_id
```

## 注意事项

- 路径由调用方提供；前端和命令行程序使用相对于项目根目录的路径。
- 保存操作按文件依次执行，不提供跨多个输出文件的事务回滚。
- CSV 名称字段不支持带逗号的引号转义格式。

## 依赖

依赖模块 1 的图接口、模块 3 的实体接口和模块 4 的充电桩创建接口。

