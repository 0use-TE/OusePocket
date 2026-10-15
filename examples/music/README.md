# 独立音乐播放器

播放页沿用主题色：圆封面、曲名、上一曲/播放/下一曲、停止和音量滑条都会跟着系统的明暗主题变。向左滑动进入曲库，曲库里有 SD 卡、内部、刷新和歌曲列表，正在播放的那一首用主题色标出；向右滑动回到播放页。旧示例中的音乐页本身是 SquareLine UI 演示；本项目另外接通了真实的本地解码与 ES8311 输出。

支持 MP3、16 位 PCM WAV，8–48 kHz、单声道或双声道。使用系统音量、播放/暂停/继续/停止、原生顺序播放、分页歌曲列表。明暗主题和中英文随系统配置。文件名作为曲名，暂未读取 ID3、专辑封面或实现拖动快进；细进度条按文件已解码字节显示，播放时间来自实际输出 PCM。

## 准备与安装

固件已内置一段原创二十秒测试旋律。更新主程序后，启动时写到
`/littlefs/user/music/pocket-test.wav`。已有同名文件且大小一致时保留；旋律更新后大小不同，下次启动会换成新的。
打开音乐后默认读 SD 卡。没有卡或读取失败时自动改显示内部曲库，点 `pocket-test.wav` 或播放按钮即可测试。
不会开机自动播放或修改系统音量。测试文件约 313 KiB，8 kHz、单声道、16 位 PCM WAV。
首次保存失败只输出警告，不影响进入桌面，下次启动重试。

测试旋律和音频驱动随主程序更新（`app-flash`）。本轮播放器界面和生命周期修复还需安装新版音乐 BPK，保留内部文件即可。
若重烧 LittleFS，会替换设备上已安装的应用，届时需重新安装音乐 BPK。
测试旋律的资源和可重复生成脚本位于
`components/pocket_platform/assets/pocket-test.wav` 与 `tools/generate-test-music.py`。

1. 先更新本轮固件：固件提供解码器、音频 HAL、Pocket 服务；应用不内置。
2. 用电脑读卡器在 FAT32 SD 卡根目录建立 `music` 文件夹，将 `.mp3` / `.wav` 放进去，再插回设备。
3. 关闭串口监视器，用当前固件的 CH343 控制串口安装；下面 COM10 按实际串口替换。

```powershell
# 在项目根目录执行；安装上传工具只需一次。
python -m pip install .\.reference\esp-brookesia\tools\brookesia_usb
python .\tools\install-bpk.py --port COM10 .\examples\music\dist\net.0use.music-1.1.1.bpk
```

项目安装脚本先配置 DTR/RTS 再打开串口，复用官方安装协议；用法见
[`install-bpk.README.md`](../../tools/install-bpk.README.md)。

桌面出现“音乐 / Music”后打开。播放页向左滑动进入曲库，选择来源或歌曲。返回桌面或打开最近任务只暂停 UI，原生音频继续播放；上推关闭音乐卡片会停止音频、关闭文件并释放服务。后台播放期间禁止安全弹出或修改正在使用的音乐卷；意外拔卡会显示读取错误，并在播放器关闭文件后才卸载卷。

“内部”使用 `/littlefs/user/music`，适合少量测试音频。当前应用没有文件上传界面，常规使用请先用 SD 卡。SD 未挂载、目录不存在、文件格式不支持或打开失败会显示错误，不会格式化 SD 卡。

## 修改与打包

`src/res/screens/main.json` 定义界面，`src/app/main.js` 定义列表、服务调用和应用生命周期。Manifest 已声明 Pocket 1.0.0 与 AudioPlayback 0.8.2；不要删除这两项，否则服务调用会被 Core 拒绝。

```powershell
python .\tools\package-runtime-apps.py
```

输出 `dist/net.0use.music-1.1.1.bpk`，以同一 package id 安装新版本即可替换应用。修改 PNG/布局生成器后，运行 `tools/generate-runtime-app-resources.py`（需要 Pillow）再打包；如果手动修改 JSON，请不要再运行生成器覆盖手工修改。

网页预览不包含 Pocket 和音频服务。打开后仍显示播放页和曲库，曲目是界面示例，状态写明只显示界面；滑动、翻页和音量滑条可以操作，不会出声。设备上这些服务存在时，仍按 SD 卡和真实播放工作。

1.1.1 修复了原生服务返回字符串错误时提示为空、后台切换时未完成的曲库加载丢失，以及连续调节音量时请求交错的问题。服务调用设有超时；关闭播放器后，旧请求结果不会再操作新实例。播放页和曲库按屏幕实际逻辑尺寸布局，适配 75%、100%、125% 显示密度。

主机回归使用实际应用 JavaScript、原生服务实现、真实资源解析器与 LVGL 布局，覆盖生命周期、错误恢复、音量请求、播放页和曲库的中英文/明暗主题/三种密度。真实 SD 卡、ES8311 声音、触摸操作与长时间后台播放仍待实机验证。

参考：`ESP32-P4-WIFI6-Touch-LCD-3.5/examples/esp-idf/11_esp_brookesia_phone/components/brookesia_app_squareline_demo/ui/screens/ui_screen_music_player.c`；解码 API 参照微雪 `10_mp4_player/main/app_extractor.c` 和 Espressif `esp_audio_codec` 官方示例。
