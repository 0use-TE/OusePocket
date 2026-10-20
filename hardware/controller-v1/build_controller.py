"""Mechanical controller V1. Run with FreeCAD's Python or run build.FCMacro in GUI."""
import FreeCAD as App, Part, math, json, os, traceback
from pathlib import Path
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[1]
V=App.Vector
P={'CaseWidth':110.0,'CaseHeight':168.0,'CaseDepth':28.0,'Wall':2.4,'CornerRadius':9.0,
 'HostCenterY':46.0,'HostFrontZ':-2.65,'HostClearance':0.30,'MainPcbWidth':96.0,'MainPcbHeight':88.0,
 'MainPcbCenterY':-34.0,'MainPcbTopZ':-8.0,'PcbThickness':1.6,'StickPcbTopZ':-18.8,
 'SwitchHeight':4.3,'SwitchStroke':0.25,'ButtonFreeGap':0.15,'ButtonTravelStop':0.40,
 'ButtonDiameter':10.0,'ButtonHoleDiameter':10.5,'ButtonFlangeDiameter':12.4,'ButtonTopZ':1.6,
 'StickX':-28.0,'StickY':-8.0,'StickBodyWidth':18.2,'StickBodyDepth':21.7,'StickBodyHeight':11.2,
 'StickLeverHeight':18.95,'StickTiltDeg':23.0,'StickPushMax':0.9,'StickHoleDiameter':18.5,
 'StickCapDiameter':20.0,'StickCapBottomZ':6.5,'StickCapTopZ':9.5,'DpadX':-28.0,'DpadY':-47.0,
 'DpadLength':27.0,'DpadWidth':8.0,'DpadSwitchPitch':9.0,'DpadPivotZ':-6.5,
 'AbxyCenterX':28.0,'AbxyCenterY':-25.0,'AbxyPitch':12.5,'MenuX':7.0,'StartX':31.0,'FunctionY':-65.0}
if (OUT/'parameters.json').exists():P.update(json.loads((OUT/'parameters.json').read_text(encoding='utf-8')))
(OUT/'parameters.json').write_text(json.dumps(P,indent=2),encoding='utf-8')
DOC=None;objs={};printables=[];motion=[];progress=[]
def note(s):
 progress.append(s);(OUT/'progress.json').write_text(json.dumps(progress,ensure_ascii=False,indent=2),encoding='utf-8')
def box(w,h,d,x,y,z):return Part.makeBox(w,h,d,V(x-w/2,y-h/2,z))
def cyl(r,h,x,y,z):return Part.makeCylinder(r,h,V(x,y,z))
def rounded(w,h,d,x,y,z,r):
 a=box(w-2*r,h,d,x,y,z).fuse(box(w,h-2*r,d,x,y,z))
 for sx in [-1,1]:
  for sy in [-1,1]:a=a.fuse(cyl(r,d,x+sx*(w/2-r),y+sy*(h/2-r),z))
 return a.removeSplitter()
def union(parts):
 a=parts[0]
 for b in parts[1:]:a=a.fuse(b)
 return a.removeSplitter()
def cut(a,parts):
 for b in parts:a=a.cut(b)
 return a.removeSplitter()
def cross(length,width,depth,x,y,z):return box(length,width,depth,x,y,z).fuse(box(width,length,depth,x,y,z)).removeSplitter()
def part(name,label,shape,group,color=(.7,.7,.72),printing=False,kind='Designed'):
 o=DOC.addObject('Part::Feature',name);o.Label=label;o.Shape=shape
 group.addObject(o);o.addProperty('App::PropertyString','Status','Design');o.Status=kind
 o.addProperty('App::PropertyColor','RenderColor','Design');o.RenderColor=color
 if App.GuiUp:o.ViewObject.ShapeColor=color;o.ViewObject.LineColor=(.17,.18,.21);o.ViewObject.DisplayMode='Flat Lines'
 objs[name]=o
 if printing:printables.append(o)
 return o

def build():
 global DOC
 DOC=App.newDocument('PocketControllerV1');DOC.Label='OusePocket 竖向掌机 V1 · 机械试装'
 groups={}
 for k,label in [('Housing','01 外壳与固定件'),('Host','02 原主机 · 官方外廓/细节估算'),('Boards','03 PCB占位与铜柱'),('Components','04 已选元件尺寸占位'),('Caps','05 操作件'),('Wiring','06 背部GPIO与线束占位'),('Fasteners','07 螺钉占位'),('Labels','08 标识'),('Keepouts','09 活动包络 · 默认隐藏')]:
  g=DOC.addObject('App::DocumentObjectGroup',k);g.Label=label;groups[k]=g
 sheet=DOC.addObject('Spreadsheet::Sheet','Dimensions');sheet.Label='机械尺寸 mm · parameters.json为生成源'
 for i,(k,val) in enumerate(P.items(),2):sheet.set('A'+str(i),k);sheet.set('B'+str(i),str(val)+' mm' if 'Deg' not in k else str(val));sheet.setAlias('B'+str(i),k)
 sheet.set('A1','Parameter');sheet.set('B1','Nominal');sheet.setColumnWidth('A',210);sheet.setColumnWidth('B',100)
 note('Host reference')
 src=App.openDocument(str(ROOT/'hardware/mechanical/ousepocket-host.FCStd'))
 rot=App.Rotation(V(0,0,1),-90).multiply(App.Rotation(V(1,0,0),180))
 tr=App.Placement(V(0,P['HostCenterY'],P['HostFrontZ']-14.1),rot).toMatrix()
 host_parts=[]
 for key in ['FrontFrame','RearShell','SocketHousing','DisplayArea','Camera','Foot1','Foot2','Foot3','Foot4']:
  old=src.getObject(key);s=old.Shape.copy();s.transformShape(tr)
  color={'FrontFrame':(.11,.13,.16),'RearShell':(.21,.24,.28),'DisplayArea':(.06,.16,.21),'SocketHousing':(.04,.04,.05),'Camera':(.025,.025,.03)}.get(key,(.22,.22,.23))
  host_parts.append(part('Host'+key,'主机 / '+old.Label,s,groups['Host'],color,kind='Reference: OEM outline; port/camera details estimated'))
 App.closeDocument(src.Name);App.setActiveDocument(DOC.Name)
 host=Part.makeCompound([o.Shape for o in host_parts])
 W,H,D,t=P['CaseWidth'],P['CaseHeight'],P['CaseDepth'],P['Wall']
 back_inner=-D+t
 abxy={'A':(P['AbxyCenterX']+P['AbxyPitch'],P['AbxyCenterY']),
 'B':(P['AbxyCenterX'],P['AbxyCenterY']-P['AbxyPitch']),
 'X':(P['AbxyCenterX'],P['AbxyCenterY']+P['AbxyPitch']),
 'Y':(P['AbxyCenterX']-P['AbxyPitch'],P['AbxyCenterY'])}
 functions={'Menu':(P['MenuX'],P['FunctionY']),'Start':(P['StartX'],P['FunctionY'])}
 directions={'Up':(P['DpadX'],P['DpadY']+9),'Down':(P['DpadX'],P['DpadY']-9),'Left':(P['DpadX']-9,P['DpadY']),'Right':(P['DpadX']+9,P['DpadY'])}
 switches={**abxy,**functions,**directions}
 case_screws=[(-51,-77),(51,-77),(-51,6),(51,6)]
 retain_screws=[(-51,70.25),(51,70.25),(-51,22),(51,22)]
 main_mounts=[(-44,-31),(44,4),(-44,-74),(44,-74),(0,-40)]
 stick_mounts=[(P['StickX']+dx,P['StickY']+dy) for dx in [-13,13] for dy in [-13,13]]
 note('Front shell and button guidance')
 front=rounded(W,H,D-t,0,0,-D+t,9)
 front=front.cut(rounded(W-2*t,H-2*t,D,0,0,-D-0.1,6.6)) if False else front
 # Open rear cavity, leaving a 2.4 mm front face.
 front=front.cut(rounded(W-2*t,H-2*t,D,0,0,-D-2.4,6.6))
 # Cavity above ends at -2.4 exactly: -30.4+28=-2.4.
 front=front.cut(rounded(93.7,62.3,5,0,P['HostCenterY'],-3,6.2))
 openings=[]
 for n,(x,y) in abxy.items():openings.append(cyl(5.25,6,x,y,-4))
 for n,(x,y) in functions.items():openings.append(cyl(4.25,6,x,y,-4))
 openings+=[cross(28.4,9.4,6,P['DpadX'],P['DpadY'],-4),cyl(P['StickHoleDiameter']/2,8,P['StickX'],P['StickY'],-6)]
 # Full access bands along all four host edges, rather than guessed port holes.
 for x in [-54,54]:openings.append(box(10,62,12.5,x,46,-18))
 openings += [box(94,8,12.5,0,78,-18),box(94,6,12.5,0,14,-18)]
 front=cut(front,openings)
 bosses=[]
 for x,y in case_screws:bosses.append(cyl(2.8,23.2,x,y,-25.6))
 for x,y in retain_screws:bosses.append(cyl(2.8,15.35 if y>50 else 20.8,x,y,-17.75 if y>50 else -23.2))
 front=union([front]+bosses)
 # Button guides: retaining flange slides inside bore, annular stop sets 0.40 travel.
 for n,(x,y) in {**abxy,**functions}.items():
  fr=6.2 if n in abxy else 5.1
  guide=cyl(fr+1.2,1.8,x,y,-4.2).cut(cyl(fr+.25,2.0,x,y,-4.3))
  stop=cyl(fr+1.2,.6,x,y,-4.2).cut(cyl(fr-.65,.8,x,y,-4.3))
  front=union([front,guide,stop])
 front=cut(front,[cyl(.8,26,x,y,-26) for x,y in case_screws+retain_screws])
 shell=part('FrontShell','前壳 · 110×168 / 导向与限位',front,groups['Housing'],(.76,.78,.82),True)
 rear=rounded(W,H,t,0,0,-D,9).cut(rounded(97,64.5,4,0,46,-D-1,6.5))
 # Main board uses nominal Ø4.4×16 M2 threaded standoffs; stick board uses printed posts.
 for x,y in stick_mounts:rear=rear.fuse(cyl(2.6,5.2,x,y,back_inner))
 rear=cut(rear,[cyl(1.15,5,x,y,-29) for x,y in case_screws+main_mounts]+[cyl(.8,9,x,y,-29) for x,y in stick_mounts])
 rear_obj=part('RearCover','后盖 · 上半部开窗 / 摇杆板支柱',rear,groups['Housing'],(.24,.27,.32),True)
 note('PCB placeholders, switches and board mounts')
 pcbtop=P['MainPcbTopZ'];thick=P['PcbThickness']
 pcb=rounded(96,88,thick,0,-34,pcbtop-thick,3)
 pcb=pcb.cut(box(36,38,4,P['StickX'],-7,pcbtop-thick-1))
 pcb=cut(pcb,[cyl(1.1,4,x,y,pcbtop-thick-1) for x,y in main_mounts]+[cyl(1.1,4,P['DpadX'],P['DpadY'],pcbtop-thick-1)])
 mainboard=part('MainPCB','按键PCB占位 · 96×88×1.6 / 顶面Z=-8',pcb,groups['Boards'],(.06,.38,.25),kind='Mechanical placeholder; no copper or electrical footprint')
 joyboard=rounded(32,32,thick,P['StickX'],P['StickY'],P['StickPcbTopZ']-thick,2)
 joyboard=cut(joyboard,[cyl(1.1,4,x,y,P['StickPcbTopZ']-thick-1) for x,y in stick_mounts])
 part('StickPCB','摇杆PCB占位 · 32×32×1.6 / 顶面Z=-18.8',joyboard,groups['Boards'],(.06,.38,.25),kind='Mechanical placeholder')
 for i,(x,y) in enumerate(main_mounts):
  standoff=cyl(2.2,15,x,y,back_inner).cut(cyl(.85,17,x,y,back_inner-1))
  part('MainStandoff'+str(i),'M2圆铜柱 Ø4.4×15 + 1mm调整垫片',standoff,groups['Boards'],(.70,.52,.19),kind='Standard hardware envelope; verify purchased OD')
  shim=cyl(2.6,1,x,y,back_inner+15).cut(cyl(1.2,1.2,x,y,back_inner+14.9))
  part('HeightShim'+str(i),'按键板高度调整垫片 · 1.0mm',shim,groups['Boards'],(.76,.76,.72),True)
 for n,(x,y) in switches.items():
  # Manufacturer 6x6 body height 3.5; actuator Ø3.5, total height 4.3.
  body=box(6,6,3.5,x,y,pcbtop)
  part('SW'+n,'SKHHAJA010 / '+n,body,groups['Components'],(.16,.17,.18),kind='Manufacturer envelope')
  part('Actuator'+n,'开关推杆 / '+n,cyl(1.75,.8,x,y,pcbtop+3.5),groups['Components'],(.12,.12,.13))
  legs=[]
  for dx in [-3.25,3.25]:
   for dy in [-2.25,2.25]:legs.append(box(.7,.45,3.5,x+dx,y+dy,pcbtop-3.5))
  part('Pins'+n,'开关脚占位 / '+n,Part.makeCompound(legs),groups['Components'],(.68,.69,.72),kind='Envelope; PCB land pattern not manufactured')
 # Nominal neutral caps: 0.15 gap to switch, 0.25 electrical travel.
 colors={'A':(.72,.20,.24),'B':(.81,.62,.16),'X':(.16,.40,.72),'Y':(.18,.58,.37)}
 for n,(x,y) in {**abxy,**functions}.items():
  radius=5 if n in abxy else 4
  flange=6.2 if n in abxy else 5.1
  shape=union([cyl(radius,4.0,x,y,-2.4),cyl(flange,.8,x,y,-3.2),cyl(1.4,.35,x,y,-3.55)])
  part('Cap'+n,n+' 按钮帽 · 顶面Z=1.6 / 空隙0.15',shape,groups['Caps'],colors.get(n,(.20,.22,.27)),True)
 note('D-pad spherical pivot and joystick shaft/cap')
 dx,dy=P['DpadX'],P['DpadY']
 plate=cyl(14,.8,dx,dy,-5)
 plate=cut(plate,[cyl(4.55,1.2,x,y,-5.2) for x,y in directions.values()])
 dpad=union([plate,cyl(3,.65,dx,dy,-4.2),cross(27,8,5.15,dx,dy,-3.55)])
 dpad_obj=part('DpadCap','十字键 · 一体摇臂 / 球面支点',dpad,groups['Caps'],(.18,.19,.23),True)
 ball=Part.makeSphere(1.5,V(dx,dy,-6.5))
 pivot=union([cyl(3,.6,dx,dy,-8),cyl(1.5,1.5,dx,dy,-8),ball])
 pivot=pivot.cut(cyl(.8,2.6,dx,dy,-8.1))
 pivot_obj=part('DpadPivot','十字键支点 · 固定在PCB中心孔',pivot,groups['Caps'],(.53,.55,.60),True)
 jx,jy=P['StickX'],P['StickY'];jz=P['StickPcbTopZ']
 # Conservative rectangular envelope makes collision checks stricter than cosmetic detail.
 joystick=box(18.2,21.7,11.2,jx,jy,jz)
 part('JoystickBody','Alps RKJXV122400R · 18.2×21.7×11.2',joystick,groups['Components'],(.42,.44,.47),kind='Conservative manufacturer envelope')
 shaft=cyl(2,18.95-11.2,jx,jy,jz+11.2)
 shaft=shaft.common(box(5,3.0,9,jx,jy,jz+10.8))
 part('JoystickShaft','摇杆轴 · Φ4 / 双平面3.0 / tip Z=0.15',shaft,groups['Components'],(.16,.16,.18),kind='Lever drawing: Ø4, across flats 3.0')
 cap=union([cyl(10,3,jx,jy,6.5),cyl(3.2,10.7,jx,jy,-4.2)])
 bore=cyl(2.05,5.6,jx,jy,-5.21).common(box(5,3.1,5.8,jx,jy,-5.3))
 cap=cap.cut(bore)
 # Two relief slits create a compliant collet; fit coupon still required.
 cap=cap.cut(box(.5,8,3.8,jx,jy,-5.21))
 joycap=part('JoystickCap','摇杆帽 · Φ20 / 双平面4.1×3.1试配',cap,groups['Caps'],(.16,.18,.23),True)
 note('Host retainers and GPIO adapter')
 topbar=rounded(106,7,2.4,0,70.25,-20.15,2)
 topbar=cut(topbar,[cyl(1.15,4,x,70.25,-21) for x in [-51,51]])
 part('UpperRetainer','主机上压条 · M2×8 / 不压玻璃',topbar,groups['Housing'],(.26,.29,.34),True)
 lowerbar=rounded(106,22,2.4,0,25,-25.6,2)
 # Cable pass-through through the retainer and standoffs for the adapter.
 lowerbar=lowerbar.cut(box(13,6,4,0,17,-26))
 lowerbar=cut(lowerbar,[cyl(1.15,4,x,22,-26) for x in [-51,51]])
 for x in [-23,23]:lowerbar=lowerbar.fuse(cyl(2.3,1.5,x,31,-23.2))
 for x in [-36,36]:lowerbar=lowerbar.fuse(box(8,8,5.45,x,21.75,-23.2))
 lowerbar=cut(lowerbar,[cyl(.8,6,x,31,-24) for x in [-23,23]])
 part('LowerRetainer','主机下固定桥 · GPIO板固定座 / 线束过孔',lowerbar,groups['Housing'],(.26,.29,.34),True)
 for i,(x,y) in enumerate([(x,y) for x in [-36,36] for y in [21.75,70.25]]):
  part('HostPad'+str(i),'主机脚垫处 / 1mm软垫',box(8,8,1,x,y,-17.75),groups['Housing'],(.10,.10,.11),kind='Soft pad; verify foot locations')
 adapter=rounded(52,20,1.6,0,24,-21.7,2)
 adapter=cut(adapter,[cyl(1.1,3,x,31,-22) for x in [-23,23]])
 part('GPIOAdapter','背部转接PCB占位 · 52×20×1.6 / 可调插接待实测',adapter,groups['Wiring'],(.07,.38,.25),kind='Nominal adapter; OEM socket location/depth not measured')
 part('MaleHeader','2×16公针塑壳占位 · 2.54mm',box(41.15,4.95,2.5,0,22,-20.1),groups['Wiring'],(.06,.06,.07),kind='Mating connector SKU/depth provisional')
 pins=[]
 for i in range(16):
  for y in [22-1.27,22+1.27]:pins.append(box(.64,.64,4.1,(i-7.5)*2.54,y,-17.6))
 part('MalePins','32个对插端子 · 仅插接示意',Part.makeCompound(pins),groups['Wiring'],(.75,.58,.20),kind='Contact overlap is intentional; alignment unverified')
 # Harness is a clearance envelope, not a rated connector or wire routing specification.
 paths=[(V(0,18,-23),V(0,10,-23)),(V(0,10,-23),V(10,2,-16)),(V(10,2,-16),V(12,2,-11))]
 wires=[]
 for a,b in paths:
  vec=b-a;wires.append(Part.makeCylinder(1.6,vec.Length,a,vec))
 part('CableEnvelope','短线束占位 · 后GPIO → 按键板',Part.makeCompound(wires),groups['Wiring'],(.86,.49,.13),kind='Routing envelope; bend/connector selection not final')
 # Fasteners shown as bounded shank/head envelopes; thread geometry omitted.
 def screw(name,x,y,z,length,forward=True):
  direction=V(0,0,1 if forward else -1)
  shape=Part.makeCylinder(.95,length,V(x,y,z),direction).fuse(Part.makeCylinder(2.0,1.6,V(x,y,z),-direction))
  return part(name,'M2×'+str(length)+' 螺钉占位',shape,groups['Fasteners'],(.62,.64,.68),kind='Standard fastener envelope')
 for i,(x,y) in enumerate(case_screws):screw('CaseScrew'+str(i),x,y,-28,10)
 for i,(x,y) in enumerate(retain_screws):screw('RetainScrew'+str(i),x,y,-20.15 if y>50 else -25.6,8)
 for i,(x,y) in enumerate(main_mounts):screw('BoardScrew'+str(i),x,y,pcbtop,6,False)
 for i,(x,y) in enumerate(stick_mounts):screw('StickScrew'+str(i),x,y,jz,6,False)
 for i,(x,y) in enumerate(main_mounts):screw('StandoffBaseScrew'+str(i),x,y,-28,6)
 for i,x in enumerate([-23,23]):screw('AdapterScrew'+str(i),x,31,-20.1,4,False)
 screw('PivotScrew',dx,dy,-9.6,4)
 # Text labels are independent, shallow solids; do not change button geometry.
 if App.GuiUp:
  try:
   import Draft
   font='C:/Windows/Fonts/arial.ttf'
   for label,(x,y) in {**abxy,**functions}.items():
    s=Draft.make_shapestring(String=label,FontFile=font,Size=3.2 if label in abxy else 2.4,Tracking=0)
    DOC.recompute();bb=s.Shape.BoundBox;s.Placement.Base=V(x-(bb.XMin+bb.XMax)/2,y-(bb.YMin+bb.YMax)/2,1.62 if label in abxy else 2.0)
    if label in functions:s.Placement.Base=V(x-(bb.XMin+bb.XMax)/2,y-9,0.04)
    groups['Labels'].addObject(s);s.ViewObject.ShapeColor=(.10,.11,.13) if label in functions else (.95,.95,.95)
  except Exception:note('Label generation skipped: '+traceback.format_exc())
 note('Geometry and movement checks')
 DOC.recompute()
 checks={'success':True,'physicalFitVerified':False,'units':'mm','parameters':P,'shapes':{},'interferences':[],'movement':{},'notes':['OEM shell outline uses official dimensions; socket/camera details still estimated.','PCB and electronics are mechanical placeholders, not routed boards.','Button gaps and cap socket require a printed tolerance sample.','Joystick pivot location is conservatively checked over 6..11.2 mm above PCB.']}
 for n,o in objs.items():
  valid=o.Shape.isValid() and not o.Shape.isNull();checks['shapes'][n]={'valid':valid,'solids':len(o.Shape.Solids),'volume':round(o.Shape.Volume,4)}
  if not valid:checks['success']=False
 # Intentional screw/pin/actuator contacts are excluded. Check fixed envelopes.
 fixed=[('MainPCB','FrontShell'),('MainPCB','RearCover'),('StickPCB','FrontShell'),('JoystickBody','MainPCB'),('JoystickBody','FrontShell'),('JoystickBody','RearCover'),('GPIOAdapter','RearCover')]
 for n in ['FrontShell','RearCover','UpperRetainer','LowerRetainer']:
  vol=objs[n].Shape.common(host).Volume
  if vol>.05:checks['interferences'].append({'a':n,'b':'Host','mm3':vol})
 for a,b in fixed:
  vol=objs[a].Shape.common(objs[b].Shape).Volume
  if vol>.05:checks['interferences'].append({'a':a,'b':b,'mm3':vol})
 # Round caps: neutral and nominal 0.4mm full press must clear the shell.
 for n in list(abxy)+list(functions):
  for press in [0,.15,.40]:
   sh=objs['Cap'+n].Shape.copy();sh.translate(V(0,0,-press));v=sh.common(shell.Shape).Volume
   if v>.005:checks['interferences'].append({'a':'Cap'+n,'b':'FrontShell','press':press,'mm3':v})
 # Conservative all-direction tilt, including maximum specified center push.
 joymax=0;mins=[]
 for pivot_height in [6.0,8.6,11.2]:
  pivot=V(jx,jy,jz+pivot_height)
  for angle in [0,23]:
   for az in range(0,360,45):
    axis=V(-math.sin(math.radians(az)),math.cos(math.radians(az)),0)
    for press in [0,.9]:
     sh=joycap.Shape.copy();sh.translate(V(0,0,-press));sh.rotate(pivot,axis,angle)
     joymax=max(joymax,sh.common(shell.Shape).Volume,sh.common(joystick).Volume)
 checks['movement']['joystickMaxShellIntersection_mm3']=joymax
 for az in range(0,360,45):
  rad=math.radians(az);d=V(math.cos(rad),math.sin(rad),0)
  arm=9*max(abs(math.cos(rad)),abs(math.sin(rad)))
  sh=dpad_obj.Shape.copy();sh.rotate(V(dx,dy,-6.5),d,math.degrees(math.atan(.40/arm)))
  mins.append(sh.common(shell.Shape).Volume)
 checks['movement']['dpadMaxShellIntersection_mm3']=max(mins)
 checks['movement']['buttonFreeGap_mm']=.15;checks['movement']['switchTravel_mm']=.25
 checks['movement']['buttonStopTravel_mm']=.40
 if joymax>.01 or max(mins)>.01 or checks['interferences']:checks['success']=False
 note('Saving assembly and manufacturing-reference geometry')
 if App.GuiUp:
  Gui.activeDocument().activeView().viewTop();Gui.activeDocument().activeView().fitAll()
 DOC.recompute();DOC.saveAs(str(OUT/'controller-v1.FCStd'))
 Part.export([o for o in objs.values()],str(OUT/'controller-v1-assembly.step'))
 import MeshPart,Mesh
 for o in printables:
  s=o.Shape.copy();b=s.BoundBox;s.translate(V(-(b.XMin+b.XMax)/2,-(b.YMin+b.YMax)/2,-b.ZMin))
  mesh=MeshPart.meshFromShape(Shape=s,LinearDeflection=.08,AngularDeflection=.25,Relative=False)
  mesh.write(str(OUT/(o.Name+'.stl')));Part.export([o],str(OUT/(o.Name+'.step')))
 (OUT/'mechanical-checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
 (OUT/'layout.json').write_text(json.dumps({'coordinateSystem':'Front view: X right, Y up, Z toward user; outer face Z=0','buttons':switches,'mainPcbMountingHoles':main_mounts,'stickPcbMountingHoles':stick_mounts,'caseScrews':case_screws,'retainerScrews':retain_screws,'dpadPivot':[dx,dy]},indent=2),encoding='utf-8')
 note('DONE '+str(checks['success']))
 return DOC,groups

if __name__=='__main__':
 try:build()
 except Exception:(OUT/'build-error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
