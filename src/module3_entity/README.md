# 模块 3：实体与哈希索引管理

## 模块定位

模块 3 负责车辆、充电站、充电桩和充电请求的全局索引与生命周期约束。每类对象使用独立的拉链哈希表按编号查询，同时使用编号数组支持稳定遍历。

## 核心数据结构

### `HashNode`

保存整数键、对象地址和同一哈希桶中的下一个节点。

### `EntityStore`

包含四张哈希表、四组编号数组及对应数量：

```text
vehicles / vehicle_ids
stations / station_ids
chargers / charger_ids
requests / request_ids
```

哈希桶数量为 `HASH_SIZE = 101`，桶下标由编号对 101 取模得到，负编号也会转换为合法下标。

## 公开函数

### 仓库生命周期

| 函数 | 作用 |
| --- | --- |
| `entity_init` | 初始化空仓库 |
| `entity_free` | 释放实体、请求和全部哈希索引 |

### 车辆

| 函数 | 作用 |
| --- | --- |
| `entity_add_vehicle` | 校验节点与参数并创建车辆 |
| `entity_find_vehicle` | 按编号查询车辆 |
| `entity_update_vehicle` | 修改未参与任务的车辆基础数据 |
| `entity_remove_vehicle` | 删除未参与任务的车辆 |

### 充电站和道路节点

| 函数 | 作用 |
| --- | --- |
| `entity_add_station` | 创建站点并初始化队列和链表 |
| `entity_find_station` | 按编号查询站点 |
| `entity_update_station` | 修改当前没有业务任务的站点 |
| `entity_remove_station` | 删除没有充电桩、队列和任务引用的站点 |
| `entity_remove_graph_node` | 删除未被车辆或站点引用的道路节点 |

### 充电桩和请求索引

| 函数 | 作用 |
| --- | --- |
| `entity_register_charger` / `entity_unregister_charger` | 注册或注销充电桩索引 |
| `entity_find_charger` | 按编号查询充电桩 |
| `entity_register_request` / `entity_unregister_request` | 注册或注销请求索引 |
| `entity_find_request` | 按编号查询请求 |

充电桩对象由模块 4 的站点链表拥有；请求对象由仿真流程创建，并在完成后由充电桩模块释放。因此注销接口只移除索引，不直接释放对象。

## 添加流程

```text
参数和引用校验
      ↓
检查容量与编号重复
      ↓
分配并初始化对象
      ↓
写入哈希表
      ↓
编号加入遍历数组
```

删除时按相反顺序解除引用、移除索引和编号，再释放对象。

## 复杂度

- 按编号查询：平均 `O(1)`，最坏 `O(n)`；
- 添加和删除索引：平均 `O(1)`；
- 编号数组删除：`O(n)`；
- 遍历全部实体：`O(n)`。

## 约束与注意事项

- 活动车辆不能修改或删除。
- 仍有充电桩、等待请求或车辆任务引用的站点不能删除。
- 被车辆或站点引用的图节点不能删除。
- 哈希表保存的是对象地址，不负责所有对象的业务状态转换。

## 依赖

依赖公共模型和模块 1 的图节点查询、删除接口。

