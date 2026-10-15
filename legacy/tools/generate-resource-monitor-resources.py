"""Generate the Resource Monitor's themed, declarative Brookesia resources.

Uses the same label, flex placement and progressBar schema as Espressif's
brookesia_gui_interface examples. The optional BPK stays separate from firmware.
"""
from pathlib import Path
import json
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'examples' / 'resource-monitor' / 'src'
RES = SOURCE / 'res'


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def label(identity, text='', size=14):
    return {'type': 'label', 'id': identity, 'styleRefs': ['app.body'],
            'placement': {'width': '100%', 'height': 'wrap'},
            'style': {'fontSize': f'{size}sp'}, 'labelProps': {'text': text},
            'commonProps': {'clickable': False}, 'bindings': {'labelProps.text': 'text'}}


def box(identity, children, card=False):
    node = {'type': 'container', 'id': identity,
            'placement': {'width': '100%', 'height': 'wrap'},
            'style': {'padding': '10dp' if card else '0dp', 'borderWidth': '0dp'},
            'commonProps': {'scrollable': False},
            'layout': {'type': 'flex', 'flexFlow': 'column', 'gap': '8dp'},
            'children': children}
    if card:
        node['styleRefs'] = ['app.card']
    return node


def button(identity, primary=False):
    title = label('title', '${constant.ui.' + identity + '}', 13)
    title['style']['textAlign'] = 'center'
    if primary:
        title['style']['textColor'] = '${color.primary.on}'
    return {'type': 'button', 'id': identity,
            'placement': {'width': 'match', 'height': '44dp', 'flexGrow': 1},
            'styleRefs': ['app.buttonPrimary' if primary else 'app.buttonSecondary'],
            'style': {'padding': '6dp', 'borderWidth': '0dp', 'radius': '14dp'},
            'layout': {'type': 'flex', 'mainAlign': 'center', 'crossAlign': 'center'},
            'commonProps': {'scrollable': False},
            'events': [{'type': 'clicked', 'action': 'resources.' + identity}],
            'children': [title]}


def row(identity, children, gap='8dp'):
    node = box(identity, children)
    node['layout'] = {'type': 'flex', 'flexFlow': 'row', 'gap': gap,
                      'mainAlign': 'center', 'crossAlign': 'center'}
    return node


def progress(identity, width='100%', height='5dp'):
    return {'type': 'progressBar', 'id': identity,
            'placement': {'width': width, 'height': height},
            'rangeProps': {'min': 0, 'max': 1000, 'value': 0},
            'commonProps': {'clickable': False},
            'style': {'bgColor': '${color.surface.base}', 'radius': '2dp', 'padding': '0dp'},
            'partStyles': {'indicator': {'bgColor': '${color.primary.fill}', 'radius': '2dp'}},
            'bindings': {'rangeProps.value': 'value', 'commonProps.hidden': 'hidden'}}


en = {
    'title': 'Resources', 'overview': 'Overview', 'memory': 'Memory', 'storage': 'Storage', 'system': 'System',
    'hold': 'Pause', 'resume': 'Resume', 'refresh': 'Refresh', 'loading': 'Reading resources…',
    'live': 'Live · refresh every 1 s', 'paused': 'Paused · last snapshot', 'ready': 'Snapshot updated',
    'sampling': 'Sampling…', 'unavailable': 'Unavailable', 'cpu_unavailable': 'CPU load unavailable',
    'upgrade': 'Update system firmware to use Resource Monitor.',
    'storage_upgrade': 'Storage statistics need newer system firmware.', 'error': 'Read failed',
    'timeout': 'Request timed out. Automatic refresh paused; tap Refresh to retry.',
    'cpu': 'CPU load', 'core': 'Core', 'frequency': 'CPU frequency',
    'trend': 'CPU · last 60 s · 0–100%', 'trend_hint': 'Each column averages 2 s; gaps have no sample.',
    'internal_ram': 'Internal RAM', 'psram': 'PSRAM heap', 'used': 'Used', 'free': 'Free',
    'total': 'Total', 'minimum_free': 'Lowest free since boot', 'largest': 'Largest free block',
    'largest_hint': 'Largest blocks: cached snapshots, sampled up to every 5 s.',
    'heap_hint': 'Allocatable heap only. Reserved memory and PSRAM code / read-only data are excluded.',
    'flash': 'Physical Flash', 'partitions': 'Flash partitions', 'partition_hint': 'Partition allocation is separate from file storage space.',
    'firmware': 'Firmware', 'firmware_image': 'Image', 'firmware_partition': 'Partition',
    'internal_storage': 'Internal files (LittleFS)', 'sd': 'SD card', 'sd_missing': 'SD card is not mounted.',
    'sd_busy': 'Playback active; capacity query deferred.',
    'sd_hint': 'Read-only statistics; this app does not mount or format cards.',
    'storage_hint': 'File storage refreshes every 10 s while visible.',
    'chip': 'Chip', 'revision': 'Revision', 'cores': 'CPU cores', 'uptime': 'Uptime', 'reset': 'Reset reason',
    'version': 'Firmware version', 'idf': 'ESP-IDF', 'sample_time': 'Sampling time', 'interval': 'CPU sample interval',
    'system_hint': 'CPU load measures busy time; frequency is shown separately.', 'days': 'd', 'hours': 'h', 'minutes': 'm', 'seconds': 's',
    'reset_unknown': 'Unknown', 'reset_power_on': 'Power on', 'reset_external': 'External reset',
    'reset_software': 'Software restart', 'reset_panic': 'System exception',
    'reset_interrupt_watchdog': 'Interrupt watchdog', 'reset_task_watchdog': 'Task watchdog',
    'reset_watchdog': 'Watchdog', 'reset_deep_sleep': 'Wake from deep sleep', 'reset_brownout': 'Low voltage',
    'reset_sdio': 'SDIO reset', 'reset_usb': 'USB reset', 'reset_jtag': 'JTAG reset',
    'reset_efuse': 'eFuse error', 'reset_power_glitch': 'Power glitch', 'reset_cpu_lockup': 'CPU lockup'
}
zh = {
    'title': '资源监视', 'overview': '总览', 'memory': '内存', 'storage': '存储', 'system': '系统',
    'hold': '暂停', 'resume': '继续', 'refresh': '刷新', 'loading': '正在读取资源…',
    'live': '实时 · 每秒刷新', 'paused': '已暂停 · 显示上次快照', 'ready': '快照已更新',
    'sampling': '统计中…', 'unavailable': '不可用', 'cpu_unavailable': 'CPU 负载暂不可用',
    'upgrade': '请升级系统固件以使用资源监视。',
    'storage_upgrade': '存储统计需要更新系统固件。', 'error': '读取失败',
    'timeout': '读取超时，已暂停自动刷新；点击刷新重试。',
    'cpu': 'CPU 负载', 'core': '核心', 'frequency': 'CPU 频率',
    'trend': 'CPU · 最近 60 秒 · 0–100%', 'trend_hint': '每柱为 2 秒均值；空缺表示没有采样。',
    'internal_ram': '内部 RAM', 'psram': 'PSRAM 可分配堆', 'used': '已用', 'free': '可用',
    'total': '总计', 'minimum_free': '开机以来最低可用', 'largest': '最大连续空闲块',
    'largest_hint': '最大连续块是低频快照，最多每 5 秒采样一次。',
    'heap_hint': '仅统计可动态分配的堆；不包含保留区及 PSRAM 中的代码、只读数据。',
    'flash': 'Flash 物理容量', 'partitions': 'Flash 分区', 'partition_hint': '分区预留空间与可存文件的空间分开统计。',
    'firmware': '系统固件', 'firmware_image': '镜像大小', 'firmware_partition': '所在分区',
    'internal_storage': '内部文件（LittleFS）', 'sd': 'SD 卡', 'sd_missing': 'SD 卡尚未挂载。',
    'sd_busy': '播放中，容量暂缓查询。',
    'sd_hint': '仅查看统计；不会自动挂载或格式化存储卡。',
    'storage_hint': '本页可见时，每 10 秒刷新文件存储。',
    'chip': '芯片', 'revision': '版本', 'cores': 'CPU 核心数', 'uptime': '运行时间', 'reset': '复位原因',
    'version': '固件版本', 'idf': 'ESP-IDF', 'sample_time': '采样耗时', 'interval': 'CPU 统计间隔',
    'system_hint': 'CPU 负载按忙碌时间统计，频率单独显示。', 'days': '天', 'hours': '时', 'minutes': '分', 'seconds': '秒',
    'reset_unknown': '未知', 'reset_power_on': '上电启动', 'reset_external': '外部复位',
    'reset_software': '软件重启', 'reset_panic': '系统异常',
    'reset_interrupt_watchdog': '中断看门狗', 'reset_task_watchdog': '任务看门狗',
    'reset_watchdog': '看门狗', 'reset_deep_sleep': '深度休眠唤醒', 'reset_brownout': '供电电压过低',
    'reset_sdio': 'SDIO 复位', 'reset_usb': 'USB 复位', 'reset_jtag': 'JTAG 复位',
    'reset_efuse': 'eFuse 异常', 'reset_power_glitch': '电源毛刺', 'reset_cpu_lockup': 'CPU 锁定'
}

write(SOURCE / 'manifest.json', {
    'package': {'id': 'net.0use.resources', 'name': {'en': 'Resources', 'zh_CN': '资源监视'},
                'version': '1.0.1', 'visible': True, 'systems': ['super']},
    'runtime': {'type': 'JavaScript', 'entry': 'app/main.js', 'resource_dir': 'res', 'arguments': []},
    'services': [{'name': 'Pocket', 'version': '1.0.0'}]
})
write(RES / 'constants/en.json', {'type': 'constant', 'data': {'ui': en}})
write(RES / 'constants/zh.json', {'type': 'constant', 'data': {'ui': zh}})
write(RES / 'root.json', {'version': '0.1.0',
    'assets': ['constants/en.json', 'images/index.json', 'screens/main.json', 'flows/main.json'],
    'variants': [{'when': '${expr(${env.language} == "zh_CN")}', 'assets': ['constants/zh.json']}]})
write(RES / 'profile.json', {'root': 'root.json', 'icon_id': 'launcher_icon',
    'screen_flows': [{'screen_flow': 'main', 'layer': 'AppDefault'}]})
write(RES / 'flows/main.json', {'type': 'screenFlow', 'id': 'main', 'screens': ['main'], 'initial': 'main'})
write(RES / 'images/index.json', {'type': 'imageSet', 'images': [
    {'id': 'launcher_icon', 'src': 'resources_launcher.png', 'width': 92, 'height': 92}]})
image = Image.new('RGBA', (276, 276))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((0, 0, 275, 275), radius=68, fill='#267D87')
for x, top in [(63, 144), (113, 94), (163, 55)]:
    draw.rounded_rectangle((x, top, x + 33, 214), radius=8, fill='white')
image.resize((92, 92), Image.Resampling.LANCZOS).save(RES / 'images/resources_launcher.png')

trend = row('trend', [progress('bar' + str(i), 'match', '48dp') for i in range(30)], '2dp')
trend['placement']['height'] = '48dp'
trend['layout']['crossAlign'] = 'end'
for child in trend['children']:
    child['placement']['flexGrow'] = 1

overview = box('overview', [
    box('cpuCard', [label('caption', '${constant.ui.cpu}', 13)], True),
    label('cpuText', '${constant.ui.sampling}', 18), label('coreText', '', 13),
    label('trendTitle', '${constant.ui.trend}', 13), trend, label('trendHint', '${constant.ui.trend_hint}', 12),
    label('internalText', '${constant.ui.internal_ram}', 14), progress('internalBar'),
    label('psramText', '${constant.ui.psram}', 14), progress('psramBar'),
    label('storageText', '${constant.ui.internal_storage}', 14), progress('storageBar'),
    label('heapHint', '${constant.ui.heap_hint}', 12)])
memory = box('memory', [
    label('heading', '${constant.ui.memory}', 18),
    box('internalCard', [label('caption', '${constant.ui.internal_ram}', 16)], True),
    label('internalText', '${constant.ui.sampling}', 14), progress('internalBar'),
    box('psramCard', [label('caption', '${constant.ui.psram}', 16)], True),
    label('psramText', '${constant.ui.sampling}', 14), progress('psramBar'),
    label('largestHint', '${constant.ui.largest_hint}', 12), label('heapHint', '${constant.ui.heap_hint}', 12)])
storage = box('storage', [
    label('heading', '${constant.ui.storage}', 18), label('flashText', '${constant.ui.sampling}', 16),
    label('partitionHint', '${constant.ui.partition_hint}', 12),
    label('internalText', '${constant.ui.internal_storage}', 14), progress('internalBar'),
    label('sdText', '${constant.ui.sd}', 14), progress('sdBar'),
    label('sdHint', '${constant.ui.sd_hint}', 12), label('firmwareText', '', 13),
    label('partitionTitle', '${constant.ui.partitions}', 16), label('partitionText', '', 12),
    label('storageHint', '${constant.ui.storage_hint}', 12)])
system = box('system', [label('heading', '${constant.ui.system}', 18),
    label('details', '${constant.ui.sampling}', 14), label('hint', '${constant.ui.system_hint}', 12)])
for view in [overview, memory, storage, system]:
    view['bindings'] = {'commonProps.hidden': 'hidden'}
    view['commonProps']['hidden'] = view['id'] != 'overview'

screen = {'type': 'viewScreen', 'id': 'main', 'styleRefs': ['app.page'],
    'style': {'padding': '12dp', 'paddingTop': '30dp', 'paddingBottom': '28dp', 'borderWidth': '0dp'},
    'placement': {'width': '${env.widthDp}', 'height': '${env.heightDp}'},
    'commonProps': {'scrollable': True},
    'layout': {'type': 'flex', 'flexFlow': 'column', 'gap': '8dp', 'crossAlign': 'center'},
    'children': [label('title', '${constant.ui.title}', 22), label('status', '${constant.ui.loading}', 12),
        row('tabsTop', [button('overview'), button('memory')]),
        row('tabsBottom', [button('storage'), button('system')]),
        row('actions', [button('hold'), button('refresh', True)]), overview, memory, storage, system]}


def flow(node):
    node.setdefault('placement', {}).setdefault('mode', 'flow')
    for child in node.get('children', []):
        flow(child)


flow(screen)
write(RES / 'screens/main.json', screen)
print('Generated Resource Monitor resources (themes, en / zh_CN, 30 native progress bars).')
