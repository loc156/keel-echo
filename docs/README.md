# 文档索引

先看仓库根的 [README](../README.md) 了解三个项目是什么，再按"我想做什么"查下面。

## 按任务

| 我想… | 读这篇 | 面向 |
| --- | --- | --- |
| 搞清 Keel / Echo / KeelUI 三者的关系与边界 | [ARCHITECTURE.md](ARCHITECTURE.md) | 所有人 |
| 把代码编起来、排查编译/链接错误 | [BUILD.md](BUILD.md) | 所有人 |
| 知道哪些没做、哪些没验证 | [ROADMAP.md](ROADMAP.md) | 所有人 |
| 用 Echo 做第一个界面 | [../echo/docs/GETTING-STARTED.md](../echo/docs/GETTING-STARTED.md) | 使用者 |
| 查 Echo 的某个控件/函数签名 | [../echo/docs/API.md](../echo/docs/API.md) | 使用者 |
| 了解 Keel 的语言模型与定位 | [../keel/docs/LANGUAGE.md](../keel/docs/LANGUAGE.md) | 使用者 |
| 知道 Keel 为什么"默认不信任" | [../keel/docs/SURFACE.md](../keel/docs/SURFACE.md) 第 6 节 | 使用者 |
| 查 Keel 有哪些指令、栈效果是什么 | [../keel/docs/REFERENCE.md](../keel/docs/REFERENCE.md) | 使用者 |
| 在 C++ 里装载并运行 Keel 脚本 | [../keel/docs/LOADER.md](../keel/docs/LOADER.md) | 宿主开发者 |
| 给脚本加自己的指令、接输出、单步调试 | [../keel/docs/EMBEDDING.md](../keel/docs/EMBEDDING.md) | 宿主开发者 |
| 写 `let / if / while / fn` 而不是裸 token | [../keel/docs/SURFACE.md](../keel/docs/SURFACE.md) | 脚本作者 |
| 用脚本画界面（`ui.*`） | [../keelui/README.md](../keelui/README.md) | 脚本作者 |

## 按项目

- **Keel**（脚本语言）：[README](../keel/README.md) → [LANGUAGE](../keel/docs/LANGUAGE.md) · [REFERENCE](../keel/docs/REFERENCE.md) · [LOADER](../keel/docs/LOADER.md) · [SURFACE](../keel/docs/SURFACE.md) · [EMBEDDING](../keel/docs/EMBEDDING.md)
- **Echo**（UI 库）：[README](../echo/README.md) → [GETTING-STARTED](../echo/docs/GETTING-STARTED.md) · [API](../echo/docs/API.md)
- **KeelUI**（脚本画界面）：[README](../keelui/README.md)

## 约定

- 头文件里的中文注释是**第一手资料**；`API.md` / `REFERENCE.md` 是人手整理的查阅版，两者可能有轻微不同步，以头文件为准。
- 所有代码示例都在 Windows + MinGW-w64 g++ 11.5.0 上编译过（GUI 程序另说运行时）。
- 仓库里没有第三方源码，`GLFW` / `Dear ImGui` 的获取方式见 [BUILD.md](BUILD.md)。
