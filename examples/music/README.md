# 独立音乐播放器

界面与交互参考微雪旧版 Brookesia 的圆形唱片、曲名、上一曲/播放/下一曲布局，改写成当前 Brookesia 的 JSON + JavaScript 应用。旧示例中的音乐页本身是 SquareLine UI 演示；本项目另外接通了真实的本地解码与 ES8311 输出。

支持 MP3、16 位 PCM WAV，8–48 kHz、单声道或双声道。使用系统音量、播放/暂停/继续/停止、原生顺序播放、分页歌曲列表。明暗主题和中英文随系统配置。文件名作为曲名，暂未读取 ID3、专辑封面或实现拖动快进；细进度条按文件已解码字节显示，播放时间来自实际输出 PCM。

## 准备与安装

1. 先更新本轮固件：固件提供解码器、音频 HAL、Pocket 服务；应用不内置。
2. 用电脑读卡器在 FAT32 SD 卡根目录建立 `music` 文件夹，将 `.mp3` / `.wav` 放进去，再插回设备。
3. 关闭串口监视器，用当前固件的 CH343 控制串口安装；下面 COM10 按实际串口替换。

```powershell
# 在 D:\Projects\OusePocket 下执行；安装上传工具只需一次。
python -m pip install .\.reference\esp-brookesia\tools\brookesia_usb
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\music\dist\net.0use.music-1.0.0.bpk
```

桌面出现“音乐 / Music”后打开，选择 SD 卡并刷新，点击歌曲即可播放。返回桌面或打开最近任务只暂停 UI，原生音频继续播放；上推关闭音乐卡片会停止音频、关闭文件并释放服务。后台播放期间禁止安全弹出或修改正在使用的音乐卷；意外拔卡会显示读取错误，并在播放器关闭文件后才卸载卷。

“内部”使用 `/littlefs/user/music`，适合少量测试音频。当前应用没有文件上传界面，常规使用请先用 SD 卡。SD 未挂载、目录不存在、文件格式不支持或打开失败会显示错误，不会格式化 SD 卡。

## 修改与打包

`src/res/screens/main.json` 定义界面，`src/app/main.js` 定义列表、服务调用和应用生命周期。Manifest 已声明 Pocket 1.0.0 与 AudioPlayback 0.8.2；不要删除这两项，否则服务调用会被 Core 拒绝。

```powershell
python .\tools\package-runtime-apps.py
```

输出 `dist/net.0use.music-1.0.0.bpk`，以同一 package id 安装新版本即可替换应用。修改 PNG/布局生成器后，运行 `tools/generate-runtime-app-resources.py`（需要 Pillow）再打包；如果手动修改 JSON，请不要再运行生成器覆盖手工修改。

功能和主机回归已完成；真实 SD 卡、ES8311 声音、触摸操作与长时间后台播放待实机验证。

参考：`ESP32-P4-WIFI6-Touch-LCD-3.5/examples/esp-idf/11_esp_brookesia_phone/components/brookesia_app_squareline_demo/ui/screens/ui_screen_music_player.c`；解码 API 参照微雪 `10_mp4_player/main/app_extractor.c` 和 Espressif `esp_audio_codec` 官方示例。
