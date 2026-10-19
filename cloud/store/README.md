# Ouse 应用商店

Next.js 服务端、网页发布入口和协议 v1。客户端是仓库的 `apps/store` BPK。

## 运行

使用 Node.js 24 以上版本（内置 SQLite），在本目录执行：

```powershell
npm ci
$env:STORE_ADMIN_TOKEN = [guid]::NewGuid().ToString('N')
$env:STORE_DATA_DIR = 'D:/OuseStoreData'
npm run build
npm run start
```

打开 `http://本机局域网IP:3000/`，在独立“发布应用”栏目选择 BPK，自动读取包内图标、名称、作者、介绍、版本和发布日期；令牌为上面生成的 `$env:STORE_ADMIN_TOKEN`，请保存以便重启服务时复用。管理员令牌不写入网页存储。客户端右上角“源”填写同一个地址。P4 和电脑需要互通，Windows 防火墙需允许所选端口入站；HTTP 可直接用于局域网测试。

发布时检查自动填入的信息，并选择分类。同一应用 ID 的版本序号必须递增；发布服务会把最终作者、介绍、发布日期、版本及最低 API 写入 BPK 后重新计算 SHA256。元数据不需要额外描述文件；最低 API 必须与应用实际能力匹配。固件 0.32.0 / native API 36 可解析这些元数据并通过 packages.list 提供给应用。

网页支持分类、应用名/作者/介绍搜索、按更新时间/名称/体积排序、真实包内图标和详情预览。图标由 OIC1 BGRA 无损生成 PNG，原始 OIC1 仍供设备使用。作者字段是填写信息，不是身份认证或发布者签名。

`STORE_DATA_DIR` 必须是支持硬链接的持久化本地目录。`store.sqlite` 保存应用索引和版本记录，采用 WAL 与事务；不可变 BPK 和图标仍放在 `objects/`。发布先完整写入对象，再事务提交索引；版本竞争会拒绝重复发布。备份时停止服务后复制整个目录，不要在运行中单独复制数据库文件。此实现不适合无持久卷的 Serverless 文件系统。

已有 JSON 索引可停服后执行 `npm run import-catalog`，显式导入同一数据目录的 `catalog.json`。目标数据库必须为空，脚本校验包与哈希后整批提交；原 JSON 保留用于备份，运行服务不再读取它。

## 协议 v1

客户端保存以 `/` 结尾的商店基地址。以下路径相对于该地址，下载路径不拼接为站点根目录。

| 请求 | 返回 |
| --- | --- |
| `GET api/store/v1/info` | `protocol:1,name,maxPackageBytes` |
| `GET api/store/v1/apps?cursor=0&limit=12&q=关键词&category=all&sort=newest` | `protocol:1,items,total,nextCursor`；结束时 nextCursor 为 null |
| `GET api/store/v1/apps/user.example` | `protocol:1,app` |
| `GET files/{sha256}.bpk` | 原始 BPK；Content-Length、无跳转 |
| `GET files/{iconSha256}.bin` | OIC1 BGRA 图标 |
| `GET icons/{iconSha256}.png` | 浏览器使用的 PNG 图标 |
| `POST api/admin/releases?versionCode=1&versionName=1.0&minNativeApi=36&description=介绍&author=作者&releasedAt=2026-10-19&category=tools` | 请求体原始 BPK，Authorization: Bearer 管理员令牌 |

索引条目含 `id,name,versionCode,versionName,minNativeApi,size,sha256,url,icon,iconPng,description,author,releasedAt,category,capabilities,publishedAt,updatedAt`。每页最多 12 条，介绍最多 512 UTF-8 字节、作者最多 64 UTF-8 字节，BPK 最多 512 KiB。releasedAt 是包内 YYYY-MM-DD 发布日期；publishedAt 是商店首次上架时间，更新保留；updatedAt 是商店本次更新时间，两个商店时间均自动记录。分类为 tools/life/media/games/development/other，查询 all 表示不限制；排序为 name/newest/size。

内核解析 ZIP、CRC、资源限制、页面结构和 JS 语法后，显示系统安装确认。取消、校验失败或 Flash 写入失败不覆盖旧包；完成暂存后原子替换包文件，安装激活异常时提示失败，重启从已提交包恢复。降级安装被拒绝；同版本可以重装。SD 卡在资源管理器点击 `.bpk` 即可使用同一安装流程，无需联网，应用仍安装在内部 Flash。

## 身份边界

HTTP 被允许，系统确认中明确显示未加密来源。HTTP 下索引和哈希都可能被篡改，SHA256 只检查传输完整性。HTTPS 证书验证服务器身份，应用 ID 是应用标识，二者均不是发布者签名；当前不验证发布者证书，不保证同 ID 包来自同一作者。仅安装可信来源。公网管理入口应使用 HTTPS，令牌经 HTTP 传输会暴露。

设备列表采用两列图标卡片、分类与连续滚动，详情显示作者和发布日期；网页采用浅色 App Store 风格并显示真实应用图标，发布入口独立。没有账号、自动安装、付费、OTA 或发布者签名系统。

## 验证

```powershell
npm test
npm run build
```

主仓库还提供 `node tools/test-store-app.cjs` 和原生 BPK/内核回归测试。
