"""Reproducible, transparent PNG and declarative JSON assets for optional apps."""
from pathlib import Path
import json
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
def write(path,obj):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def label(id,text,size=14,height='wrap',align='left'):
    style={'fontSize':f'{size}sp'}
    if align!='left':style['textAlign']=align
    return {'type':'label','id':id,'placement':{'width':'100%','height':height},
        'styleRefs':['app.body'],'style':style,'commonProps':{'clickable':False},
        'labelProps':{'text':text},'bindings':{'labelProps.text':'text'}}
def row(id,children,gap='8dp'):
    for child in children:
        if child['type']=='label':child['placement'].update(width='match',flexGrow=1)
    return {'type':'container','id':id,'placement':{'width':'100%','height':'wrap'},
        'style':{'padding':'0dp','bgColor':'','borderWidth':'0dp'},'commonProps':{'scrollable':False},
        'layout':{'type':'flex','flexFlow':'row','gap':gap,'mainAlign':'center','crossAlign':'center'},'children':children}
def button(id,action,text=None,icon=None,primary=False,width='match',height='44dp',icon_size='26dp',font=13):
    node={'type':'button','id':id,'placement':{'width':width,'height':height,'flexGrow':1 if width=='match' else 0},
        'styleRefs':['app.buttonPrimary' if primary else 'app.buttonSecondary'],
        'style':{'padding':'6dp','borderWidth':'0dp','radius':'16dp'},'commonProps':{'scrollable':False},
        'layout':{'type':'flex','mainAlign':'center','crossAlign':'center'},'events':[{'type':'clicked','action':action}]}
    if icon:
        node['children']=[{'type':'image','id':'icon','placement':{'width':icon_size,'height':icon_size},
            'imageProps':{'src':'${image.'+icon+'}','innerAlign':'contain'},'style':{'imageRecolor':'${color.primary.on}' if primary else '${color.text.default}','imageRecolorOpacity':255},
            'commonProps':{'clickable':False},'bindings':{'imageProps.src':'src'}}]
    else:
        child=label('title',text,font,align='center');child['placement']['width']='match';node['children']=[child]
    return node
def glyph(kind,path):
    im=Image.new('RGBA',(192,192));d=ImageDraw.Draw(im);white='white'
    if kind in ['play','launcher']:
        if kind=='launcher':d.rounded_rectangle((0,0,191,191),radius=46,fill='#ff746b')
        d.polygon([(70,43),(70,149),(145,96)],fill=white)
    elif kind=='pause':
        d.rounded_rectangle((55,42,77,150),radius=5,fill=white);d.rounded_rectangle((110,42,132,150),radius=5,fill=white)
    elif kind in ['next','previous']:
        d.polygon([(51,45),(51,147),(122,96)],fill=white);d.rectangle((132,45,145,147),fill=white)
        if kind=='previous':im=im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    elif kind=='cover':
        d.ellipse((4,4,188,188),fill='#252d40');d.ellipse((28,28,164,164),outline='#d7dbe3',width=3)
        d.ellipse((48,48,144,144),outline='#6d7688',width=2);d.ellipse((70,70,122,122),fill='#ff746b')
        d.ellipse((90,90,102,102),fill='#252d40')
    elif kind=='logs':
        d.rounded_rectangle((0,0,191,191),radius=46,fill='#3184d1')
        for y in [56,94,132]:d.rounded_rectangle((43,y,145,y+10),radius=5,fill=white)
    path.parent.mkdir(parents=True,exist_ok=True)
    im.resize((92,92) if kind in ['launcher','logs'] else (128,128) if kind=='cover' else (48,48),Image.Resampling.LANCZOS).save(path)

music_en={'title':'Music','library':'Choose a song','empty':'No songs in this folder.',
    'refresh':'Refresh','sd':'SD','internal':'Internal','previous':'Previous','next':'Next','stop':'Stop','volume':'Volume',
    'ready':'Ready','playing':'Playing','paused':'Paused','idle':'Stopped','loading':'Loading songs…','elapsed':'Elapsed',
    'no_tracks':'No songs found','error':'Playback error','busy':'Please wait…','page':'songs','back':'Back',
    'swipe_songs':'Swipe left for songs','swipe_back':'Swipe right to return',
    'sd_fallback':'SD card unavailable. Showing internal songs.',
    'layout':'No audio service. Showing the layout only.'}
music_zh={'title':'音乐','library':'选择歌曲','empty':'这个目录里没有歌曲。',
    'refresh':'刷新','sd':'SD卡','internal':'内部','previous':'上一页','next':'下一页','stop':'停止','volume':'音量',
    'ready':'就绪','playing':'播放中','paused':'已暂停','idle':'已停止','loading':'正在读取歌曲…','elapsed':'已播放',
    'no_tracks':'没有找到歌曲','error':'播放失败','busy':'请稍候…','page':'首','back':'返回',
    'swipe_songs':'向左滑动查看歌曲','swipe_back':'向右滑动返回播放',
    'sd_fallback':'SD 卡不可用，已显示内部歌曲。',
    'layout':'没有音频服务，只显示界面。'}
logs_en={'title':'Logs','live':'Live','saved':'Saved','filter':'Errors','all':'All','hold':'Pause','resume':'Resume',
    'clear':'Clear','confirm':'Tap Clear again to delete saved logs','ready':'Waiting for logs…','paused':'View paused',
    'missed':'Older entries were overwritten','saved_hint':'Persistent tail from internal Flash','cleared':'Logs cleared',
    'not_persistent':'Flash logging unavailable','back':'Back','refresh':'Refresh'}
logs_zh={'title':'日志','live':'实时','saved':'历史','filter':'错误','all':'全部','hold':'暂停','resume':'继续',
    'clear':'清空','confirm':'再次点击清空，删除已保存日志','ready':'等待日志…','paused':'查看已暂停',
    'missed':'较早日志已被环形缓冲覆盖','saved_hint':'内部 Flash 保存的最近日志','cleared':'日志已清空',
    'not_persistent':'日志暂未写入 Flash','back':'返回','refresh':'刷新'}
for name,strings,zh in [('music',music_en,music_zh),('log-viewer',logs_en,logs_zh)]:
    src=ROOT/'examples'/name/'src';res=src/'res'
    write(src/'manifest.json',{'package':{'id':'net.0use.'+('music' if name=='music' else 'logs'),
        'name':{'en':strings['title'],'zh_CN':zh['title']},'version':'1.1.1' if name=='music' else '1.0.0','visible':True,'systems':['super']},
        'runtime':{'type':'JavaScript','entry':'app/main.js','resource_dir':'res','arguments':[]},
        'services':[{'name':'Pocket','version':'1.0.0'}]+([{'name':'AudioPlayback','version':'0.8.2'}] if name=='music' else [])})
    write(res/'constants/en.json',{'type':'constant','data':{'ui':strings}})
    write(res/'constants/zh.json',{'type':'constant','data':{'ui':zh}})
    write(res/'root.json',{'version':'0.1.0','assets':['constants/en.json','images/index.json','screens/main.json','flows/main.json'],
        'variants':[{'when':'${expr(${env.language} == "zh_CN")}','assets':['constants/zh.json']}]})
    write(res/'profile.json',{'root':'root.json','icon_id':'launcher_icon','screen_flows':[{'screen_flow':'main','layer':'AppDefault'}]})
    write(res/'flows/main.json',{'type':'screenFlow','id':'main','screens':['main'],'initial':'main'})
    launcher='music_launcher.png' if name=='music' else 'logs_launcher.png'
    images=[{'id':'launcher_icon','src':launcher,'width':92,'height':92}]
    glyph('launcher' if name=='music' else 'logs',res/'images'/launcher)
    if name=='music':
        for kind in ['play','pause','previous','next','cover']:
            glyph(kind,res/f'images/music_{kind}.png');size=128 if kind=='cover' else 48
            images.append({'id':kind,'src':'music_'+kind+'.png','width':size,'height':size})
    write(res/'images/index.json',{'type':'imageSet','images':images})
    def page(id,hidden,down,up,swipe,children):
        return {'type':'container','id':id,'placement':{'mode':'absolute','x':'0dp','y':'0dp','width':'${env.widthDp}','height':'${env.heightDp}'},
            'style':{'padding':'24dp' if id=='player' else '14dp','paddingTop':'28dp','paddingBottom':'8dp','borderWidth':'0dp','bgColor':''},
            'commonProps':{'scrollable':False,'clickable':True,'hidden':hidden},
            'bindings':{'commonProps.hidden':'hidden'},
            'layout':{'type':'flex','flexFlow':'column','gap':'5dp','crossAlign':'stretch'},
            'events':[{'type':'pressed','action':down},{'type':'released','action':up},{'type':'gesture','action':swipe}],
            'children':children}
    if name=='music':
        # Theme colors follow the system light/dark palette. Swipe left opens the library.
        cover={'type':'image','id':'cover','placement':{'width':'100%','height':'0dp','flexGrow':1},
            'imageProps':{'src':'${image.cover}','innerAlign':'contain'},
            'style':{'radius':'56dp','clipCorner':True},'commonProps':{'clickable':False}}
        progress={'type':'progressBar','id':'progress','placement':{'width':'100%','height':'8dp'},
            'rangeProps':{'min':0,'max':1000,'value':0},'commonProps':{'clickable':False},
            'style':{'bgColor':'${color.surface.base}','radius':'4dp'},
            'partStyles':{'indicator':{'bgColor':'${color.primary.fill}','radius':'4dp'}},
            'bindings':{'rangeProps.value':'value'}}
        slider={'type':'slider','id':'slider','commonProps':{'clickable':True,'scrollable':False,'pressLock':True},
            'placement':{'width':'100%','height':'44dp'},
            'style':{'padding':'0dp','bgColor':'${color.border.default}','radius':'22dp'},
            'rangeProps':{'min':0,'max':100,'value':40},
            'events':[{'type':'valueChanged','action':'music.volume'}],
            'partStyles':{'indicator':{'bgColor':'${color.primary.fill}','radius':'22dp'},
                'knob':{'style':{'bgColor':'${color.surface.raised}','borderColor':'${color.primary.fill}',
                    'borderWidth':'2dp','radius':'20dp','padding':'0dp'}}},
            'bindings':{'rangeProps.value':'value'}}
        sources=[]
        for sid,action,text in [('sd','music.sd','${constant.ui.sd}'),('internal','music.internal','${constant.ui.internal}'),('refresh','music.refresh','${constant.ui.refresh}')]:
            item=button(sid,action,text);item['bindings']={'style.bgColor':'bg'};sources.append(item)
        tracks=[]
        for i in range(4):
            item=button('track'+str(i),'music.track'+str(i),'',width='100%')
            item['bindings']={'commonProps.hidden':'hidden','style.bgColor':'bg'};tracks.append(item)
        screen={'type':'viewScreen','id':'main','styleRefs':['app.page'],
            'style':{'padding':'0dp','borderWidth':'0dp'},
            'placement':{'width':'${env.widthDp}','height':'${env.heightDp}'},'commonProps':{'scrollable':False},'children':[
                page('player',False,'music.player_down','music.player_up','music.swipe_player',[
                    cover,label('song','${constant.ui.library}',18,'wrap','center'),
                    label('status','${constant.ui.ready}',14,'wrap','center'),label('elapsed','00:00',14,'wrap','center'),
                    progress,
                    row('transport',[button('previous','music.previous',icon='previous'),
                        button('play','music.play',icon='play',primary=True),
                        button('next','music.next',icon='next')]),
                    button('stop','music.stop','${constant.ui.stop}',width='100%'),
                    label('volume_value','${constant.ui.volume}',15,'wrap','center'),slider,
                    label('hint','${constant.ui.swipe_songs}',13,'wrap','center')]),
                page('library',True,'music.library_down','music.library_up','music.swipe_library',[
                    label('hint','${constant.ui.swipe_back}',13,'wrap','center'),
                    row('sources',sources),label('summary','${constant.ui.empty}',14,'wrap','center'),*tracks,
                    row('pages',[button('previous','music.page_previous','${constant.ui.previous}'),
                        label('number','1 / 1',13,'wrap','center'),
                        button('next','music.page_next','${constant.ui.next}')])])]}
    else:
        screen={'type':'viewScreen','id':'main','styleRefs':['app.page'],
            'style':{'padding':'12dp','paddingTop':'30dp','paddingBottom':'28dp','borderWidth':'0dp'},
            'placement':{'width':'${env.widthDp}','height':'${env.heightDp}'},'commonProps':{'scrollable':True},
            'layout':{'type':'flex','flexFlow':'column','gap':'8dp','crossAlign':'center'},'children':[]}
        c=screen['children'];c.append(label('title','${constant.ui.title}',22))
        c.append(row('modes',[button('live','logs.live','${constant.ui.live}'),button('saved','logs.saved','${constant.ui.saved}'),button('filter','logs.filter','${constant.ui.filter}')]))
        c.append(row('actions',[button('hold','logs.hold','${constant.ui.hold}'),button('refresh','logs.refresh','${constant.ui.refresh}'),button('clear','logs.clear','${constant.ui.clear}')]))
        c.append(label('status','${constant.ui.ready}',12))
        body={'type':'container','id':'body','styleRefs':['app.card'],'style':{'padding':'8dp','borderWidth':'0dp'},
            'placement':{'width':'100%','height':'200dp','flexGrow':1},'commonProps':{'scrollable':True},
            'layout':{'type':'flex','flexFlow':'column'},'children':[label('text','',12)]}
        c.append(body)
    def flow(node):
        node.setdefault('placement',{}).setdefault('mode','flow')
        for child in node.get('children',[]):flow(child)
    flow(screen)
    write(res/'screens/main.json',screen)
print('Generated transparent, themed JSON UI resources for Music and Logs')
