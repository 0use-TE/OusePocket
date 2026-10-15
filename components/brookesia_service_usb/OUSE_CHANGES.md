# Ouse 本地改动

来源：`.reference/esp-brookesia/service/system/brookesia_service_usb`，0.8.1。

2026-10-15：日志 ELF `f1bcf6262` 的第二次重启是启动期间的 panic。匹配 ELF 解码：`Usb::Impl::update_connection_state → pthread_mutex_lock_internal → xQueueSemaphoreTake`，故障队列地址 `0x48746cb0` 落在 PSRAM。正常动态 FreeRTOS 队列应分配于内部 RAM，日志无法确定句柄为何失效。

USB 状态锁和发送锁改为提前创建、位于内部 RAM 的 FreeRTOS mutex，避免普通 malloc 分配的 pthread 包装层；每次获取前校验句柄区域，创建后检查堆完整性。USB 协议、UART 传输与安装接口保持原实现。此为针对故障路径的加固，是否消除偶发故障仍须实机重复重启验证；不将其记为已证实根因修复。
