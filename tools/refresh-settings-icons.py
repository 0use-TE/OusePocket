"""Generate transparent Settings glyphs; JPG tiles cannot carry an alpha channel."""
from pathlib import Path
import json
import copy
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'components/brookesia_app_settings/package/res'
for name in ['my_device', 'wifi', 'sound', 'display', 'more', 'power', 'language', 'time_zone']:
    image = Image.new('RGBA', (168, 168))
    d = ImageDraw.Draw(image)
    white = (255, 255, 255, 255)
    if name == 'my_device':
        d.rounded_rectangle((48, 20, 120, 148), 12, outline=white, width=9)
        d.line((73, 130, 95, 130), fill=white, width=7)
    elif name == 'display':
        d.rounded_rectangle((20, 28, 148, 116), 10, outline=white, width=9)
        d.line((84, 116, 84, 142), fill=white, width=9)
        d.line((53, 142, 115, 142), fill=white, width=9)
    elif name == 'wifi':
        for box in [(-4, 25, 172, 201), (24, 56, 144, 176), (53, 88, 115, 150)]:
            d.arc(box, 217, 323, fill=white, width=10)
        d.ellipse((76, 131, 92, 147), fill=white)
    elif name == 'sound':
        d.polygon([(22, 64), (52, 64), (82, 37), (82, 132), (52, 105), (22, 105)], fill=white)
        d.arc((56, 47, 126, 122), 300, 60, fill=white, width=9)
        d.arc((40, 25, 151, 144), 304, 56, fill=white, width=9)
    elif name == 'more':
        for x in [40, 84, 128]: d.ellipse((x-9, 75, x+9, 93), fill=white)
    elif name == 'power':
        d.rounded_rectangle((16, 48, 139, 120), 10, outline=white, width=9)
        d.rounded_rectangle((141, 67, 154, 101), 4, fill=white)
        d.polygon([(83, 54), (55, 91), (81, 91), (72, 117), (106, 79), (81, 79)], fill=white)
    elif name == 'language':
        d.ellipse((25, 25, 143, 143), outline=white, width=8)
        d.ellipse((55, 25, 113, 143), outline=white, width=7)
        d.line((25, 84, 143, 84), fill=white, width=7)
        d.arc((25, 10, 143, 100), 0, 180, fill=white, width=6)
    else:
        d.ellipse((25, 25, 143, 143), outline=white, width=8)
        d.line((84, 49, 84, 84, 113, 102), fill=white, width=9)
    image.resize((42, 42), Image.Resampling.LANCZOS).save(RES / f'images/icon/{name}_glyph.png')

index_path = RES / 'images/index.json'
index = json.loads(index_path.read_text(encoding='utf-8'))
for entry in index['images']:
    if entry['id'].startswith(('home.', 'more.')):
        name = entry['id'].split('.')[-1]
        if name in ['my_device', 'wifi', 'sound', 'display', 'more', 'power', 'language', 'time_zone']:
            entry['src'] = f'icon/{name}_glyph.png'
index_path.write_text(json.dumps(index, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

template = RES / 'templates/nav_menu_item.json'
data = json.loads(template.read_text(encoding='utf-8'))
node = data['node']
node['style'] = {'borderWidth':'0dp', 'padding':'6dp'}
node['layout'].update(gridTemplateRows=['24dp', '18dp'], gap='2dp')
node['placement']['height'] = '${constant.settings.layout.rowHeight}'
for child in node['children']:
    if child['id'] == 'icon':
        child['styleRefs'] = ['app.iconRecolor']
        child['style'] = {'bgOpacity':0, 'borderWidth':'0dp'}
    elif child['id'] in ['title_box', 'value_box']:
        child['style'] = {'padding':'0dp', 'bgColor':'', 'borderWidth':'0dp'}
        child['placement']['height'] = 'match'
template.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
# Per-screen icon backgrounds must not override the transparent template.
for screen in (RES / 'screens').glob('*.json'):
    obj = json.loads(screen.read_text(encoding='utf-8'))
    before = copy.deepcopy(obj)
    def visit(value):
        if isinstance(value, dict):
            if value.get('templateId') == 'nav_menu_item':
                icon = value.get('overrides', {}).get('icon', {})
                icon.pop('style', None)
            for child in value.values(): visit(child)
        elif isinstance(value, list):
            for child in value: visit(child)
    visit(obj)
    if obj != before:
        screen.write_text(json.dumps(obj, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

card = RES / 'templates/settings_card.json'
data = json.loads(card.read_text(encoding='utf-8'))
data['node'].setdefault('style', {})['borderWidth'] = '0dp'
card.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
