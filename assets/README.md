# 界面资源

## 目录用途

本目录保存 GTK 4 前端使用的图标和相关许可证文件。

## 资源清单

| 文件 | 用途 |
| --- | --- |
| `icons/car-front.svg` | 汽车图标矢量源文件 |
| `icons/car-front.png` | GTK 地图实际加载的汽车图标 |
| `icons/charging-station.svg` | 充电站图标矢量源文件 |
| `icons/charging-station.png` | GTK 地图实际加载的充电站图标 |
| `icons/LUCIDE-LICENSE.txt` | Lucide Icons 的 ISC License |

## 加载方式

`gtk4_ui.c` 使用相对路径加载 PNG 文件。运行程序时应将项目根目录设为当前工作目录，并保留 `assets/icons` 的目录结构。资源加载失败时，地图会使用内置的简单图形作为回退样式。

## 第三方许可

汽车和充电站图标来自 Lucide Icons，并按照 ISC License 使用。发布项目或便携版程序时，应保留 `LUCIDE-LICENSE.txt`。

