# 上位机开发约定

- 遵循根目录 AGENTS.md 与本目录 README.md；先更新界面或目录约定，再实施调整。
- main.py 仅负责桌面启动、资源路径和关闭处理；legacy_tk.py 保留已完成的 Tkinter 实现作为回退与原测试基线。
- backend/ 使用纯 Python，控制线程独占业务状态、串口发送和数据库写入；桥接线程只能入队或读快照。协议与数据库语义必须与 legacy_tk.py 一致。
- frontend/ 使用 React/TypeScript，组件放 src/components/，桥接与数据类型放 src/lib/，测试放 src/ 对应模块旁；构建输出为 build/，不引入远程字体或 CDN。
- UI 只呈现已有遥测、风机、窗口、串口与日志功能。输入草稿保留到用户操作，滑块只在鼠标/触摸或调节键释放后发送；ACK 不表示机械动作完成。
- tests/ 与 frontend/ 测试只能使用模拟串口、内存数据库。预览截图放 build/ui-preview/；禁止自动连接硬件或创建生产数据库。
- 依赖使用本目录 .venv/ 与 frontend/node_modules/，提交 Python 依赖版本与 npm 锁文件，不修改全局工具或系统设置。
- 验证：仓库根目录运行 python -m unittest discover -s "通风改/上位机/tests"；frontend/ 运行 npm test 与 npm run build。桌面演示仅以 --demo 启动。
- 不删除现有文件或清理构建目录，不覆盖其他工程未提交改动；需要删除时先取得用户授权。
