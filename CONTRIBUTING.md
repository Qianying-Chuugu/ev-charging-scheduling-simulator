# 贡献指南

感谢你关注新能源汽车充电资源调度仿真系统。项目欢迎错误修复、测试补充、文档改进，以及与路径规划、排队调度、充电推荐和离散时间仿真有关的功能建议。

## 开始之前

1. 先阅读根目录 `README.md` 和相关模块目录中的 README。
2. 搜索现有 Issue，确认问题或建议没有被重复提出。
3. 较大的算法、数据格式或界面调整，请先创建 Issue 说明目标和方案。
4. 不要提交 EXE、DLL、日志、测试输出、本机配置或包含个人信息的数据。

## 本地构建

项目使用 C11、GNU Make 和 GTK 4。Windows 推荐使用 MSYS2 UCRT64：

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-pkgconf make
```

在项目根目录构建并测试：

```bash
make
make test
```

Windows 也可以执行：

```bat
构建GTK4.bat --no-pause
```

## 开发约定

- 使用 C11，保持 `-Wall -Wextra -pedantic` 下无新增警告。
- 保持模块职责清晰，通过头文件公开必要接口，避免跨模块访问内部状态。
- 检查所有动态内存、文件句柄、GTK/Cairo 资源的生命周期。
- 新增算法或状态变化时，优先补充对应模块测试。
- 修改 CSV 字段、枚举值或数据约束时，同步更新 `data/README.md`。
- 修改用户可见行为时，同步更新根 README、模块 README 或界面截图。
- 不要为了整理代码而混入与当前 Issue 无关的大范围格式化。

## 提交信息

建议使用简短、明确的提交信息：

```text
feat: add time-of-use charging price
fix: prevent duplicate vehicle submission
test: cover unreachable station recommendation
docs: explain experiment metrics
```

常用类型包括 `feat`、`fix`、`test`、`docs`、`refactor` 和 `build`。

## Pull Request

提交 PR 前请确认：

- `make test` 全部通过；
- 新功能具有测试或明确的手动验证步骤；
- 没有加入构建产物和运行输出；
- PR 只解决一个清晰的问题；
- 说明了行为变化、验证方法以及可能的兼容性影响。

维护者可能会要求缩小变更范围、补充测试或更新文档。讨论应聚焦技术问题并保持尊重。
