# 独立日志查看器

固件捕获 `ESP_LOG`，保留串口原始输出，同时把去除 ANSI 颜色的文本放入固定环形缓冲。后台每两秒把缓冲写入内部 LittleFS，无需 SD 卡或打开日志应用。

内部路径为 `/littlefs/user/logs/`：`current.log` 达到约 64 KiB 后轮转为 `previous-1.log`、`previous-2.log`，三个文件合计约 192 KiB。实时缓冲保存最近 64 条、每条最多 383 字节，长日志按 UTF-8 完整字符截断并添加换行。日志线程不在捕获回调里做 Flash I/O。

## 安装

先更新包含日志后端的本轮固件，关闭串口监视器，然后在仓库根目录运行：

```powershell
python -m pip install .\.reference\esp-brookesia\tools\brookesia_usb
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\log-viewer\dist\net.0use.logs-1.0.0.bpk
```

打开桌面的“日志 / Logs”：

- **实时**：每秒读取新的日志；缓冲溢出会提示较早记录已覆盖。
- **历史**：读取内部 Flash 三个轮转文件的末尾，重启后仍可查看；当前 RPC 限制返回最多 8 KiB，界面保留最后 5000 个字符。
- **错误**：筛选错误级别行及包含 error、failed、panic、abort 的行；再次点击显示全部。这是文本筛选，不是完整堆栈分类。
- **暂停/继续**：暂停界面自动读取，固件仍持续记录。
- **清空**：必须连续点击两次确认；清除实时缓冲与三个日志文件。

应用进入后台会停止自己的轮询定时器，返回前台恢复；关闭应用会释放服务，不会停止固件日志记录。界面采用系统明暗主题及中英文，支持上下滚动查看。

## 修改和服务接口

界面位于 `src/res/screens/main.json`，逻辑位于 `src/app/main.js`。声明 Pocket 1.0.0，通过 `Pocket.Request` 调用 `LogsRead`、`LogsSaved`、`LogsFlush`、`LogsClear`，例如：

```js
const result = await brookesia.call_function_async(
  pocket, 'Request', JSON.stringify({Request: {Op: 'LogsRead', After: 0}})
);
```

RPC 返回包装为 `{success, data, error}`；`data` 包含 `ok`、`entries`、`last`、`missed`、`persistent`。下次把 `last` 作为 `After`，避免重复读取。

```powershell
python .\tools\package-runtime-apps.py
```

输出 `dist/net.0use.logs-1.0.0.bpk`，该应用不预装。

ROM 启动输出、`esp_rom_printf` 和 panic 的底层寄存器转储不经过 `ESP_LOG` 捕获；断电或崩溃前最后约两秒记录可能尚未落盘。这是日常诊断日志，不替代串口日志或 core dump。LittleFS 未挂载前的记录只能暂存在环形缓冲；重烧 LittleFS 镜像会替换内部日志及已安装应用，先备份有用的数据。
