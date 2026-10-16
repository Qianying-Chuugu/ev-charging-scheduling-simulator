# 测试

## 测试范围

| 目录 | 覆盖内容 |
| --- | --- |
| `module1_graph` | 节点、道路、Dijkstra、路径恢复和不可达判断 |
| `module2_scheduler` | 动态优先级、最大堆和等待时间估算 |
| `module3_entity` | 车辆、站点和哈希索引的增查改删 |
| `module4_charger` | 充电桩添加、分配、充电完成和删除 |
| `module5_recommend` | 站点过滤、在途负载和推荐排序 |
| `module6_simulation` | 提交、行驶、排队、充电、故障和重置 |
| `module7_fileio` | 原子加载、错误回滚、保存和再次加载 |
| `module9_timer` | GTK 定时控制器的初始化、启动和停止 |
| `integration` | 当前完整输入数据上的推荐与完整充电流程 |

模块 8 的界面交互主要通过构建、启动和人工操作验证；其业务依赖由其他模块测试覆盖。

## 运行全部测试

在已安装 GCC、GTK 4 和 pkg-config 的环境中，从项目根目录执行：

```bash
make test
```

也可以单独运行已经生成的测试程序：

```text
build/test_graph.exe
build/test_scheduler.exe
build/test_entity.exe
build/test_charger.exe
build/test_recommend.exe
build/test_simulation.exe
build/test_fileio.exe
build/test_backend.exe
build/test_timer.exe
```

Windows 运行 `test_timer.exe` 时，GTK 4 运行库目录需要位于 `PATH` 中。

## 整体联调测试

`integration/test_backend.c` 会：

1. 加载 `data/input` 中的完整数据；
2. 校验节点、车辆、站点和充电桩数量；
3. 验证车辆能够获得有效推荐；
4. 提交单车请求并推进至完成；
5. 重置后批量提交全部车辆；
6. 确认全部车辆在时间步上限内完成充电。

修改输入数据规模后，应同步更新集成测试中的数量断言。

