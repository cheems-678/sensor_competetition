# 第三方组件与许可

新界面只包含运行所需的本地组件与资源，不加载远程字体、CDN 或管理后台模板。完整 npm 依赖版本记录于 `frontend/package-lock.json`，依赖包许可证随本地安装包分发；打包时应同时保留本文件。

| 组件 | 用途 | 许可 | 来源 |
| --- | --- | --- | --- |
| React / React DOM | 前端界面 | MIT | https://github.com/facebook/react |
| shadcn/ui | Button 组件结构与界面设计基础；已改为项目本地样式 | MIT | https://github.com/shadcn-ui/ui |
| Radix UI Slot | Button 的 `asChild` 组合支持 | MIT | https://github.com/radix-ui/primitives |
| class-variance-authority | Button 样式变体 | Apache-2.0 | https://github.com/joe-bell/cva |
| clsx | 样式类组合 | MIT | https://github.com/lukeed/clsx |
| tailwind-merge | Tailwind 类合并 | MIT | https://github.com/dcastil/tailwind-merge |
| Tailwind CSS | 样式工具与基础样式 | MIT | https://github.com/tailwindlabs/tailwindcss |
| Lucide | 线性 SVG 图标 | ISC | https://github.com/lucide-icons/lucide |
| Vite / Vitest | 本地构建与测试；运行程序不需要 | MIT | https://github.com/vitejs/vite / https://github.com/vitest-dev/vitest |
| TypeScript | 构建时类型检查 | Apache-2.0 | https://github.com/microsoft/TypeScript |
| pywebview | Windows WebView2 桌面窗口与 Python 桥接 | BSD-3-Clause | https://github.com/r0x0r/pywebview |
| pythonnet | Python 与 .NET 运行时桥接 | MIT | https://github.com/pythonnet/pythonnet |
| pyserial | 原有串口读写 | BSD-3-Clause | https://github.com/pyserial/pyserial |

Python 运行依赖及其传递依赖版本固定在 `requirements.txt`；完整许可证保留在安装环境各包的 `*.dist-info/` 中。后续 EXE 分发须同时收集 Python 与前端依赖的许可证文件。

## shadcn/ui MIT License

Copyright (c) 2023 shadcn

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
