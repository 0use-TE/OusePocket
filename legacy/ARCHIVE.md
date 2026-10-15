# 旧工程归档

2026-10-15 整理：旧源码与示例仅移动，未删除。仓库根目录现在是 Ouse Core 0.1.0 工程。

- components：原 Brookesia 组件与 pocket_platform。
- examples：原 Brookesia 固件、各 BPK 示例、managed_components 和已有 build。ouse-core 空目录是迁移后的原位置。
- ESP32-P4-WIFI6-Touch-LCD-3.5：原微雪完整示例。
- .reference：Brookesia、Hosted、微雪组件参考仓库和硬件资料。
- docs、README.md、COMPLETED.md：历史文档，里面的根目录路径为整理前路径。
- tools、.tools、.vscode：原工具、诊断日志、ELF 备份和 VS Code 配置。
- workspace-moves.json：此次已核验的目录移动清单。

旧 build/CMake 缓存包含原绝对路径，归档后仅保留产物用于取证和恢复，不能直接复用缓存构建。
旧工程若需再次构建，应使用新的独立 build 目录，并检查历史脚本、依赖清单中的绝对路径。
仓库 .git 历史和当前分支保留，没有创建嵌套的新项目仓库。

已有可烧录的旧固件已在移动前复制到根目录 tmp/brookesia-backup.zip，包含烧录说明与镜像 SHA256。
