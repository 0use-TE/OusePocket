"""Generate the product's declarative GUI resources (run once when editing layouts)."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def read(p): return json.loads((ROOT/p).read_text(encoding='utf-8'))
def write(p, v, indent=2):
    p = ROOT/p; p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(v, ensure_ascii=False, indent=indent)+'\n', encoding='utf-8')
def label(id, text, binding=False):
    v = {'type':'label','id':id,'labelProps':{'text':text},'commonProps':{'clickable':False}, 'style':{'fontSize':'16sp'}}
    if binding: v['bindings']={'labelProps.text':'text'}
    return v
def button(id, text, action, width='100%', height='40dp'):
    return {'type':'button','id':id,'placement':{'width':width,'height':height},
        'style':{'padding':'8dp','radius':'8dp','borderWidth':'1dp','borderColor':'#687080'},
        'layout':{'type':'flex','mainAlign':'center','crossAlign':'center'},
        'events':[{'type':'clicked','action':action}], 'children':[label('title',text,True)]}
def column(id, children):
    return {'type':'container','id':id,'placement':{'width':'100%','height':'wrap'},
        'commonProps':{'scrollable':False},
        'layout':{'type':'flex','flexFlow':'column','gap':'8dp'},'children':children}
overlay='components/brookesia_system_super/resource/shell/screens/overlay.json'
o=read(overlay)
o['children']=[c for c in o['children'] if c['id']!='recents']
# Native LVGL children provide Phone-style screenshots and drag navigation.
# Keep a declarative host so its actions still pass through SystemGui workers.
recents=column('recents',[label('title','Recent apps',True)])
recents.update({'placement':{'mode':'absolute','x':0,'y':0,'width':'${env.widthDp}','height':'${env.heightDp}'},
    'commonProps':{'hidden':True,'clickable':True,'scrollable':False},
    'bindings':{'commonProps.hidden':'hidden'},
    'events':[{'type':'clicked','action':'ouse.recents.native'}],
    'style':{'padding':0}})
recents['children'][0]['placement']={'mode':'absolute','x':0,'y':'8dp','width':'100%','height':'24dp'}
recents['children'][0]['style'].update({'fontSize':'16sp','textAlign':'center'})
o['children'].append(recents)
# Battery icon and quick controls are owned by generate-shell-controls.py.
write(overlay,o)
launcher_path='components/brookesia_system_super/resource/shell/screens/app_launcher.json'
launcher=read(launcher_path)
launcher.setdefault('bindings',{})['commonProps.hidden']='launcher_hidden'
write(launcher_path,launcher)

base='components/brookesia_app_settings/package/res/'
power=column('page',[label('status','Battery: unknown'),button('refresh','Refresh','ouse.power.refresh'),
    *[button(f'timeout{s}', 'Keep screen on' if not s else f'Screen off: {s}s',f'ouse.power.timeout{s}') for s in [0,30,60,120,300]]])
power['placement']={'mode':'absolute','x':'8dp','y':'64dp','width':'${expr(${env.widthDp} - 16dp)}','height':'${expr(${env.heightDp} - 72dp)}'}
power['commonProps']={'scrollable':True}
write(base+'screens/power.json', {'type':'viewScreen','id':'power','mountMode':'dynamic','styleRefs':['app.page'],'children':[power]})
root=read(base+'root.json')
if 'screens/power.json' not in root['assets']: root['assets'].append('screens/power.json')
write(base+'root.json',root)
flow=read(base+'flows/content.json')
if 'power' not in flow['screens']: flow['screens'].append('power')
if not any(t['action']=='ouse.power.open' for t in flow['transitions']):
    flow['transitions'].append({'from':[],'action':'ouse.power.open','to':'power'})
write(base+'flows/content.json',flow)
home=read(base+'screens/settings_home.json'); page=home['children'][0]
page['children']=[c for c in page['children'] if c['id']!='power']
main_list=next(c for c in page['children'] if c['id']=='main_list')
items=main_list['slots']['content']
items[:]=[item for item in items if item['id']!='power']
# Reuse the default navigation template, including its icon, spacing, chevron,
# pressed feedback and theme styles instead of a separate generic button.
items.append({'type':'templateRef','id':'power','templateId':'nav_menu_item',
    'overrides':{'.':{'events':[{'type':'clicked','action':'ouse.power.open'}]},
        'icon':{'imageProps':{'src':'${image.home.power}'}},
        'title_box/title':{'labelProps':{'text':'Power & battery'}},
        'value_box/value':{'labelProps':{'text':'Battery & screen timeout'}}}})
write(base+'screens/settings_home.json',home)
images=read(base+'images/index.json')
images['images']=[image for image in images['images'] if image['id']!='home.power']
images['images'].append({'id':'home.power','src':'icon/power.png','width':42,'height':42})
write(base+'images/index.json',images,indent=4)
# Match the binding store keys used by the generated labels and remove obsolete
# BLE translations after the Bluetooth controls were removed from the product.
for locale in ['en','zh_CN']:
    bundle=read(base+f'i18n/{locale}.json')
    bundle.pop('bindings',None)
    bundle['updates']=[entry for entry in bundle.get('updates',[]) if '/ble_' not in entry.get('path','')]
    for entry in bundle['updates']:
        if entry['path']=='/settings_home/page/power/title':
            entry['path']='/settings_home/page/main_list/power/title_box/title'
        if entry['path']=='/settings_home/page/main_list/power/title_box/title':
            entry['key']='labelProps.text'
        elif entry['path'].startswith('/power/'):
            entry['key']='text'
    write(base+f'i18n/{locale}.json',bundle)

base='examples/file-manager/src/'
write(base+'manifest.json',{'package':{'id':'net.0use.files','name':{'en':'Files','zh_CN':'文件管理'},'version':'1.0.0','visible':True,'systems':['super']},
    'runtime':{'type':'JavaScript','entry':'app/main.js','resource_dir':'res','arguments':[]},
    'services':[{'name':'Pocket','version':'1.0.0'}]})
write(base+'res/root.json',{'version':'0.1.0','assets':['screens/main.json','flows/main.json']})
write(base+'res/profile.json',{'root':'root.json','screen_flows':[{'screen_flow':'main_flow','layer':'AppDefault'}]})
write(base+'res/flows/main.json',{'type':'screenFlow','id':'main_flow','screens':['main'],'initial':'main','transitions':[]})
children=[label('path','Files'),label('status','Internal storage / SD'),
    column('volumes',[button('internal','Internal','files.internal'),button('sd','SD card / connect','files.sd')]),
    button('up','Parent / back','files.back')]
for i in range(6):
    b=button(f'entry{i}','',f'files.entry{i}'); b['bindings']={'commonProps.hidden':'hidden'}; children.append(b)
children += [button('previous','Previous page','files.previous'),button('next','Next page','files.next'),
    button('mkdir','New folder','files.mkdir'),button('rename','Rename selected','files.rename'),
    button('copy','Copy selected','files.copy'),button('paste','Paste here','files.paste'),
    button('delete','Delete selected (tap twice)','files.delete'),button('refresh','Refresh','files.refresh'),
    button('eject','Safely remove SD','files.eject')]
screen=column('main',children); screen['type']='viewScreen'
screen['placement']={'width':'${env.widthDp}','height':'${env.heightDp}'}
screen['commonProps']={'scrollable':True}; screen['style']={'padding':'12dp'}
write(base+'res/screens/main.json',screen)
write('examples/file-manager/package.json',{'name':'net.0use.files','version':'1.0.0','private':True,
    'scripts':{'build':'brookesia build','release':'brookesia release'}})
