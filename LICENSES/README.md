# 第三方许可证

本仓库不包含第三方库的源码，只在此收录它们的许可证原文，以满足再分发时的署名要求。

| 文件 | 对应依赖 | 许可证 | 用在哪 |
| --- | --- | --- | --- |
| `GLFW.txt` | [GLFW](https://www.glfw.org/) | zlib/libpng | Echo 的窗口与输入（`echo/`） |
| `ImGui.txt` | [Dear ImGui](https://github.com/ocornut/imgui) | MIT | Echo 的绘制与控件底座；KeelUI 的 `ui.*` 指令目标 |

本仓库自己的代码——`keel/`、`echo/`、`keelui/`——全部由作者独立编写，统一使用仓库根的
[LICENSE](../LICENSE) 那一份 MIT 许可证，没有按子项目分叉的条款。上面两个第三方库只是
依赖，不改变本项目自身的许可方式。

获取方式（与 `echo/build.ps1` 的约定一致）：

- Dear ImGui：<https://github.com/ocornut/imgui/releases>，解压到 `echo/libs/imgui/`
- GLFW：<https://www.glfw.org/download.html> 的 Windows 预编译包，解压到 `echo/libs/glfw/`
  （需要 `include/GLFW/` 与 `lib-mingw-w64/libglfw3.a`）
