// Shared Bluetooth setup flow. Source: tools/app-shared/bluetooth-guide.js.
function createLinkGuide(kind,onLost){
 let asked="",wasReady=false,caption="",shown=null;
 function button(value,visible){if(caption!==value){caption=value;Ouse.ui.setText("connect",value);}if(shown!==visible){shown=visible;visible?Ouse.ui.show("connect"):Ouse.ui.hide("connect");}}
 function missing(){for(const name of ["bluetooth","hid"]){const state=Ouse.permissions.get(name);if(state!=="granted")return {name,state};}return null;}
 function unavailable(message,action){if(wasReady&&onLost)onLost();wasReady=false;status(message);button(action,true);return false;}
 function poll(){
  const permission=missing();
  if(permission){session=0;if(permission.state==="ask"&&asked!==permission.name){asked=permission.name;Ouse.permissions.request(permission.name);}return unavailable(permission.state==="denied"?"权限未开启":"等待授权",permission.state==="denied"?"权限设置":"授权");}
  const link=Ouse.bluetooth.get();
  if(session&&(!link||link.session!==session)){session=0;if(onLost)onLost();}
  if(!link||!link.enabled)return unavailable("蓝牙已关闭","开启蓝牙");
  if(!link.ready)return unavailable("蓝牙启动中","蓝牙设置");
  if(!link.connected)return unavailable("尚未连接设备","连接蓝牙");
  if(!session)session=Ouse.bluetooth.open("hid")||0;
  if(!session)return unavailable(link.closing?"连接释放中":link.busy?"连接被占用":"连接暂不可用","重试连接");
  if(!link[kind])return unavailable("等待输入服务","蓝牙设置");
  wasReady=true;status("已连接");button("连接蓝牙",false);return true;
 }
 function action(){const permission=missing();if(permission){if(permission.state==="ask"){asked=permission.name;Ouse.permissions.request(permission.name);poll();}else Ouse.system.openSettings("permissions");return;}const link=Ouse.bluetooth.get();if(!link||!link.enabled||!link.ready||!link.connected||session)Ouse.system.openSettings("bluetooth");else poll();}
 Ouse.ui.on("connect","click",action);
 return {poll,arm(){asked="";poll();}};
}
