# 无线在桌面就绪后初始化

2026-10-15。用户要求将无线初始化放到系统启动后；本次实现顺序调整，用于隔离启动前Hosted路径。
源码和主机回归完成后，仍需用户重新构建主固件并进行实机重复启动验证；不是对随机BUS_ERR已修复的承诺。

## 启动顺序

原顺序为Hosted全局constructor先配置SDMMC、分配队列和创建任务，然后FreeRTOS才创建Idle和Timer任务并启动调度器。
新顺序为：

```
SDK与FreeRTOS启动
 -> app_main / ouse_setup
 -> ServiceManager、显示、Core/Super、Settings和桌面
 -> DESKTOP READY
 -> USB安装接收服务
 -> 临时ouse_wireless任务
 -> esp_hosted_init
 -> 注册并绑定Wi-Fi，读取Settings保存的开关
 -> 开关为开时提交Start，由原Wi-Fi服务连接保存的AP
 -> 注册并绑定SNTP，加载原时区/服务器并等待网络
 -> 临时任务退出
```

USB接收服务绑定失败但桌面已成功时，仍尝试启动无线；桌面初始化失败则不启动无线。
没有设置固定几秒延迟，入口依据实际桌面初始化结果。

## 官方组件与产品覆盖

产品CMake只替换Hosted target中的 `port_esp_hosted_host_init.c` 自动生命周期源，
不改managed_components、SDK或`.reference`。原文件只含constructor/destructor；保留Hosted库的其他源、依赖和WHOLE_ARCHIVE属性。
覆盖检查已审阅的原源结构与哈希，组件更新导致源漂移时应先审阅，不能静默忽略。

Wi-Fi/SNTP的自动注册同时关闭，由产品在Hosted初始化之后显式加入ServiceManager。
这是必要条件：Settings安装、Shell状态和Super时间同步会提前绑定这些服务，仅移除Hosted constructor不足以保证顺序。
HAL、保存的SSID、Wi-Fi开关及SNTP配置仍由已有服务管理；Settings和Shell补充处理服务晚到的绑定恢复。
初次偏好恢复期间暂不接受Settings的Wi-Fi操作，避免旧的自动Start覆盖用户刚提交的关闭。
显式开关保存失败时强制排入最新回滚动作；普通关闭状态不会向Idle重复提交无效Stop。

## 栈与失败边界

无线初始化在一个临时16 KiB内部RAM任务上执行，完成后释放该任务栈；不在GUI事件回调中等待C6初始化。
入口要求调度器已运行，并防止重复创建初始化任务。任务创建失败尚未调用Hosted，可重新请求；
进入Hosted初始化后不自动重试部分初始化，也不盲目调用deinit。

Hosted 2.12.3内部仍保留ESP_ERROR_CHECK和assert，某些内部初始化失败会直接panic。
本次没有改造其错误传播与部分回滚，不能声称所有无线故障都只影响联网、绝不影响桌面。
可返回的服务注册/绑定失败会记录错误，且不会主动停止已启动的桌面。

## 编译和实机验收

此启动顺序修改不需要更新LittleFS；同轮音乐/日志BPK修复和最终固件构建记录见
`docs/20-local-build-and-app-review.md`。
启动日志应先出现：

```
Wireless startup deferred until desktop ready
DESKTOP READY: System Super + Settings
```

之后才出现：

```
OuseWireless: Desktop ready; initializing Hosted after scheduler startup
ESP-Hosted starting
OuseWireless: Deferred Wi-Fi and SNTP services ready
```

比较时保持CPU400 MHz、PSRAM200 MHz、XiP、缓存参数和诊断捕获策略不变。
分别覆盖冷启动和软件重启，统计Guru和看门狗；若故障继续发生，保留新首个panic及匹配ELF。
确认保存开关为开时自动连接，为关或未保存时不自动启动Wi-Fi；检查状态栏、Settings、SNTP和USB安装。
若在无线尚未注册时已经打开Settings，注册完成后应能自动恢复功能，无需退出重进。
