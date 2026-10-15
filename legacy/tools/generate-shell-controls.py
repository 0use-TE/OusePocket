"""Generate the fixed launcher and compact, theme-aware control centre."""
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'components/brookesia_system_super/resource/shell'

def load(path):
    return json.loads(path.read_text(encoding='utf-8'))

def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

def box(id, x, y, width, height, children=(), **extra):
    return dict(type='container', id=id,
        commonProps=dict(clickable=False, scrollable=False),
        placement=dict(mode='absolute', x=x, y=y, width=width, height=height),
        style=dict(padding=0), children=list(children), **extra)

def label(id, text, x, y, width, height, size='14sp'):
    node = box(id, x, y, width, height)
    node.update(type='label', labelProps=dict(text=text),
        bindings={'labelProps.text':'text'},
        style=dict(fontSize=size, textColor='${color.text.default}'))
    node.pop('children')
    return node

def icon(id, kind, x, y, size='24dp'):
    node = box(id, x, y, size, size)
    node.update(type='image', imageProps=dict(src='${image.quick.' + kind + '.dark}', innerAlign='contain'),
        bindings={'imageProps.src':'src'})
    node.pop('children')
    return node

def button(id, kind, action, x, y, width, height, text):
    node = box(id, x, y, width, height, [
        icon('icon',kind,'16dp','16dp'),
        label('label',text,'52dp','18dp','${expr(${env.widthDp} - 92dp)}','28dp')])
    node['commonProps'].update(clickable=True,pressLock=True)
    node['events']=[dict(type='clicked',action=action)]
    node['bindings']={'style.bgColor':'bg','commonProps.disabled':'disabled'}
    node['style'].update(bgColor='${color.surface.raised}',radius='22dp')
    node['stateStyles']={'pressed':{'bgColor':'${color.surface.pressed}'}}
    return node

def generate():
    index=load(BASE/'images/index.json')
    index['images']=[i for i in index['images'] if not i['id'].startswith(('quick.', 'battery.'))]
    out=BASE/'images/icon/quick'; out.mkdir(exist_ok=True)
    # Small monochrome icons, drawn at 4x resolution for clean edges.
    for theme,color in [('light','#17202d'),('dark','#f3f5fb')]:
        for kind in ['wifi','sun','volume','mute','theme','settings']:
            im=Image.new('RGBA',(128,128)); d=ImageDraw.Draw(im)
            if kind=='wifi':
                for r in [46,32,18]:
                    d.arc((64-r,78-r,64+r,78+r),220,320,fill=color,width=7)
                d.ellipse((59,76,69,86),fill=color)
            elif kind=='sun':
                d.ellipse((42,42,86,86),outline=color,width=7)
                for a in range(0,360,45):
                    c,s=math.cos(math.radians(a)),math.sin(math.radians(a))
                    d.line((64+34*c,64+34*s,64+48*c,64+48*s),fill=color,width=7)
            elif kind in ['volume','mute']:
                d.polygon([(23,51),(40,51),(61,32),(61,96),(40,77),(23,77)],fill=color)
                if kind=='volume':
                    for r in [27,41]: d.arc((57-r,64-r,57+r,64+r),310,50,fill=color,width=6)
                else:
                    d.line((79,49,106,79),fill=color,width=6)
                    d.line((79,79,106,49),fill=color,width=6)
            elif kind=='theme':
                d.ellipse((25,25,103,103),outline=color,width=7)
                d.pieslice((31,31,97,97),90,270,fill=color)
            else:
                d.ellipse((33,33,95,95),outline=color,width=15)
                d.ellipse((53,53,75,75),outline=color,width=6)
                for a in range(0,360,45):
                    c,s=math.cos(math.radians(a)),math.sin(math.radians(a))
                    d.line((64+34*c,64+34*s,64+46*c,64+46*s),fill=color,width=14)
            name=f'{kind}_{theme}.png'
            im.resize((32,32),Image.Resampling.LANCZOS).save(out/name)
            index['images'].append(dict(id=f'quick.{kind}.{theme}',src=f'icon/quick/{name}',width=32,height=32))
    # Espressif's old Phone selects discrete battery images. Use the same
    # mechanism with five 20% steps, matching this product's horizontal bars.
    battery_out = BASE/'images/icon/battery'; battery_out.mkdir(exist_ok=True)
    states=[*(f'level{i}' for i in range(6)), *(f'charging{i}' for i in range(6)), 'unknown']
    for theme,state in [(t,s) for t in ('light','dark') for s in states]:
        im=Image.new('RGBA',(128,80)); d=ImageDraw.Draw(im)
        charging=state.startswith('charging')
        level=0 if state=='unknown' else int(state[-1])
        foreground='#17202d' if theme=='light' else '#f3f5fb'
        color=('#16834f' if theme=='light' else '#54d99b') if charging else \
            ('#c52d3e' if theme=='light' else '#ff7373') if level<=1 and state!='unknown' else foreground
        d.rounded_rectangle((4,14,111,66),radius=8,outline=foreground,width=5)
        d.rounded_rectangle((115,30,124,50),radius=3,fill=foreground)
        for bar in range(level):
            x=13+bar*18
            d.rounded_rectangle((x,23,x+12,57),radius=2,fill=color)
        if state=='unknown':
            d.line((47,31,55,26,65,30,65,37,56,43,56,47),fill=foreground,width=5)
            d.ellipse((54,53,59,58),fill=foreground)
        elif charging:
            bolt=[(65,20),(47,42),(58,42),(52,61),(72,36),(61,36)]
            d.polygon(bolt,fill=foreground,outline='#f3f5fb' if theme=='light' else '#162a22',width=2)
        name=f'{state}_{theme}.png'
        im.resize((32,20),Image.Resampling.LANCZOS).save(battery_out/name)
        index['images'].append(dict(id=f'battery.{state}.{theme}',src=f'icon/battery/{name}',width=32,height=20))
    save(BASE/'images/index.json',index)

    overlay=load(BASE/'screens/overlay.json')
    status=next(n for n in overlay['children'] if n['id']=='status')
    status.setdefault('commonProps',{}).update(clickable=True,pressLock=True,scrollable=False)
    right=next(n for n in status['children'] if n['id']=='status_right')
    right['children']=[n for n in right['children'] if n['id']!='battery']
    battery=box('battery',0,0,'32dp','20dp')
    battery.update(type='image',imageProps={'src':'${image.battery.unknown.dark}', 'innerAlign':'contain'},
        bindings={'imageProps.src':'src'})
    battery['placement']['mode']='flow'
    battery.pop('children')
    right['children'].append(battery)
    clock=next(n for n in right['children'] if n['id']=='clock')
    clock['children'][0]['styleRefs']=['shell.statusTiny']

    panel_children=[label('title','Quick controls',0,0,'${expr(${env.widthDp} - 80dp)}','30dp','22sp'),
        label('battery','--%',0,'34dp','100%','24dp'),
        button('settings','settings','ouse.quick.settings','${expr(${env.widthDp} - 76dp)}',0,'44dp','44dp',''),
        button('wifi','wifi','ouse.quick.wifi',0,'76dp','100%','64dp','Wi-Fi'),
        button('mute','mute','ouse.quick.mute',0,'152dp','${expr((${env.widthDp} - 44dp) / 2)}','64dp','Mute'),
        button('theme','theme','ouse.quick.theme','${expr((${env.widthDp} - 20dp) / 2)}','152dp','${expr((${env.widthDp} - 44dp) / 2)}','64dp','Theme')]
    for node in panel_children:
        if node['id'] in ['mute','theme']:
            node['children'][1]['placement']['width']='${expr((${env.widthDp} - 44dp) / 2 - 60dp)}'
            node['children'][1]['placement'].update(y='10dp',height='46dp')
        if node['id']=='theme':
            node['children'][1]['placement'].update(y='10dp',height='46dp')
            node['children'][1]['style']['fontSize']='12sp'
    # Icon-only settings tile: do not create an empty, oversized label.
    panel_children[2]['children']= [icon('icon','settings','10dp','10dp')]
    for i,kind in enumerate(['brightness','volume']):
        y=232+i*76
        panel_children.append(label(kind+'_label',kind,0,f'{y}dp','100%','22dp'))
        panel_children.append(icon(kind+'_icon','sun' if i==0 else 'volume',0,f'{y+30}dp'))
        # Keep a half-knob margin at both ends, including volume at 100%.
        slider=box(kind,'48dp',f'{y+28}dp','${expr(${env.widthDp} - 104dp)}','32dp')
        slider.update(type='slider',rangeProps=dict(min=10 if i==0 else 0,max=100,value=70 if i==0 else 40),
            bindings={'rangeProps.value':'value','commonProps.disabled':'disabled'},
            events=[dict(type='valueChanged',action=f'ouse.quick.{kind}')],
            partStyles={'indicator':{'bgColor':'${color.primary.fill}','radius':'16dp'},
                'knob':{'style':{'bgColor':'${color.surface.raised}','borderColor':'${color.primary.fill}',
                    'borderWidth':'2dp','radius':'18dp','padding':0}}})
        slider['commonProps'].update(clickable=True,pressLock=True)
        slider['style'].update(bgColor='${color.border.default}',radius='16dp')
        slider.pop('children')
        panel_children.append(slider)
    panel_children.append(label('feedback','',0,'380dp','100%','24dp','12sp'))
    panel=box('panel','16dp','${constant.ui.overlay.metric.topBarHeight}',
        '${expr(${env.widthDp} - 32dp)}','${expr(${env.heightDp} - ${constant.ui.overlay.metric.topBarHeight} - 32dp)}',panel_children)
    # The content stays scrollable on high DPI/small viewports. The bottom
    # handle lives outside it, so scrolling never also dismisses the sheet.
    panel['commonProps'].update(clickable=True,scrollable=True)
    handle=box('handle','${expr((${env.widthDp} - 40dp) / 2)}','${expr(${env.heightDp} - 18dp)}','40dp','4dp')
    handle['style'].update(bgColor='${color.text.default}',radius='2dp')
    sheet=box('sheet',0,0,'${env.widthDp}','${env.heightDp}',[panel,handle])
    sheet['style']['bgColor']='${color.bg.base}'
    sheet['bindings']={'placement.y':'y'}
    shield=box('input_shield',0,0,'${env.widthDp}','${env.heightDp}')
    shield['commonProps'].update(clickable=True,pressLock=True,hidden=True)
    shield['bindings']={'commonProps.hidden':'hidden'}
    quick=box('quick_controls',0,0,'${env.widthDp}','${env.heightDp}',[sheet,shield])
    quick['commonProps'].update(clickable=True,pressLock=True,hidden=True)
    quick['bindings']={'commonProps.hidden':'hidden'}
    overlay['children']=[quick if n['id']=='quick_controls' else n for n in overlay['children']]
    save(BASE/'screens/overlay.json',overlay)

    launcher=load(BASE/'screens/app_launcher.json')
    content=launcher['children'][0]; stage=content['children'][0]
    stage['commonProps']['scrollable']=False
    stage['placement']['height']='${expr(${env.heightDp} - ${constant.ui.content.metric.y} - ${constant.ui.content.metric.launcherGridTop} - 28dp)}'
    layer=next(n for n in stage['children'] if n['id']=='item_layer')
    layer['bindings'].update({'placement.x':'page_x','placement.width':'pages_width'})
    content['children']=[stage,label('page_indicator','',0,'${expr(${env.heightDp} - ${constant.ui.content.metric.y} - 28dp)}','100%','20dp','12sp')]
    content['children'][1]['style']['textAlign']='center'
    save(BASE/'screens/app_launcher.json',launcher)

if __name__ == '__main__':
    generate()
