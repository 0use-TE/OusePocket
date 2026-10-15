// Host-only visual check: real parsed product documents, fonts and LVGL layout/drawing.
// Hardware I/O and runtime mounting are not part of this check.
#pragma once
#include "lvgl.h"
#include <fstream>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>
#include <stdexcept>
#include <cstdint>
namespace layout_check {
using namespace esp_brookesia::gui;
inline void require(bool value,const std::string &message) { if(!value) throw std::runtime_error(message); }
inline lv_coord_t dimension(const Dimension &d) {
 if(d.mode==SizeMode::Fixed) return d.value;
 if(d.mode==SizeMode::Match) return lv_pct(100);
 if(d.mode==SizeMode::Percent) return lv_pct(d.value);
 return LV_SIZE_CONTENT;
}
inline lv_coord_t track(const Dimension &d) {
 if(d.mode==SizeMode::Fixed) return d.value;
 if(d.mode==SizeMode::Match) return LV_GRID_FR(1);
 return LV_GRID_CONTENT;
}
inline lv_grid_align_t grid_align(Align a) {
 if(a==Align::Stretch) return LV_GRID_ALIGN_STRETCH;
 if(a==Align::Center) return LV_GRID_ALIGN_CENTER;
 if(a==Align::End) return LV_GRID_ALIGN_END;
 return LV_GRID_ALIGN_START;
}
inline lv_flex_align_t flex_align(Align a) {
 if(a==Align::Center) return LV_FLEX_ALIGN_CENTER;
 if(a==Align::End) return LV_FLEX_ALIGN_END;
 if(a==Align::SpaceBetween) return LV_FLEX_ALIGN_SPACE_BETWEEN;
 return LV_FLEX_ALIGN_START;
}
inline void merge(Style &out,const Style &s) {
#define FIELD(f) if(s.f) out.f=s.f;
 FIELD(bg_color) FIELD(text_color) FIELD(border_color) FIELD(border_width) FIELD(radius)
 FIELD(padding) FIELD(padding_left) FIELD(padding_right) FIELD(padding_top) FIELD(padding_bottom)
 FIELD(font_size) FIELD(text_align) FIELD(image_recolor) FIELD(image_recolor_opacity)
#undef FIELD
}
inline std::vector<uint8_t> read(const std::filesystem::path &p) {
 std::ifstream f(p,std::ios::binary); require(bool(f),"Missing fixture: "+p.string());
 return {std::istreambuf_iterator<char>(f),{}};
}
inline lv_color_t color(const std::string &s) {
 require(s.empty() || s[0]=='#',"Unresolved fixture color: "+s);
 return lv_color_hex(std::stoul(s.empty()?"0":s.substr(s[0]=='#'?1:0),nullptr,16));
}
struct Scene {
 struct Image { std::vector<uint8_t> bytes; lv_image_dsc_t descriptor{}; };
 ThemeAsset theme; std::string locale; std::filesystem::path project,output;
 std::string settings_title;
 std::map<std::string,ImageAsset> assets;
 std::map<std::string,lv_obj_t*> objects;
 std::vector<std::unique_ptr<Image>> images;
 std::vector<std::vector<int32_t>> tracks;
 std::vector<uint8_t> font_bytes;
 std::map<int,lv_font_t*> fonts;
 lv_obj_t *root=nullptr;
 Scene(ThemeAsset t,std::string l,std::filesystem::path p,std::filesystem::path o):theme(t),locale(l),project(p),output(o) {
  font_bytes=read(project/"components/brookesia_system_super/resource/fonts"/
   (locale=="zh_CN"?"NotoSansSC-Regular.subset.ttf":"Telex-Regular.ttf"));
 }
 ~Scene() { if(root) lv_obj_delete(root); for(auto &[size,font]:fonts) lv_tiny_ttf_destroy(font); }
 const lv_font_t *font(int size) {
  size=std::max(8,size);
  if(!fonts.contains(size)) fonts[size]=lv_tiny_ttf_create_data(font_bytes.data(),font_bytes.size(),size);
  require(fonts[size]!=nullptr,"Font load failed"); return fonts[size];
 }
 lv_color_t resolve_color(std::string s) {
  if(s.starts_with("${color.") && s.ends_with("}")) s=theme.colors.at(s.substr(8,s.size()-9));
  return color(s);
 }
 lv_obj_t *create(const Node &n,lv_obj_t *parent,std::string path="") {
  path+="/"+n.id;
  auto *obj=n.type==NodeType::Label?lv_label_create(parent):n.type==NodeType::Image?lv_image_create(parent):
   n.type==NodeType::Slider?lv_slider_create(parent):n.type==NodeType::ProgressBar?lv_bar_create(parent):lv_obj_create(parent);
  lv_obj_remove_style_all(obj); lv_obj_remove_flag(obj,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_ON_FOCUS));
  if(n.common_props.scrollable) lv_obj_add_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
  if(n.common_props.hidden) lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
  objects[path]=obj;
  Style style;
  for(const auto &name : {std::string("all"),n.type==NodeType::Label?std::string("label"):std::string("container")})
   if(theme.styles.contains(name)) merge(style,theme.styles.at(name).style);
  for(const auto &name:n.style_refs) if(theme.styles.contains(name)) merge(style,theme.styles.at(name).style);
  merge(style,n.style);
  if(style.bg_color && !style.bg_color->empty()) {
   lv_obj_set_style_bg_color(obj,resolve_color(*style.bg_color),0); lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
  } else lv_obj_set_style_bg_opa(obj,LV_OPA_TRANSP,0);
  if(style.text_color && !style.text_color->empty()) lv_obj_set_style_text_color(obj,resolve_color(*style.text_color),0);
  if(style.border_color && !style.border_color->empty()) lv_obj_set_style_border_color(obj,resolve_color(*style.border_color),0);
#define APPLY(f,fn) if(style.f) fn(obj,*style.f,0);
  APPLY(border_width,lv_obj_set_style_border_width) APPLY(radius,lv_obj_set_style_radius)
  APPLY(padding,lv_obj_set_style_pad_all) APPLY(padding_left,lv_obj_set_style_pad_left)
  APPLY(padding_right,lv_obj_set_style_pad_right) APPLY(padding_top,lv_obj_set_style_pad_top)
  APPLY(padding_bottom,lv_obj_set_style_pad_bottom)
#undef APPLY
  lv_obj_set_style_text_font(obj,font(style.font_size.value_or(14)),0);
  if(style.text_align) lv_obj_set_style_text_align(obj,*style.text_align==TextAlign::Center?LV_TEXT_ALIGN_CENTER:
   *style.text_align==TextAlign::Right?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_LEFT,0);
  if(style.image_recolor) lv_obj_set_style_image_recolor(obj,resolve_color(*style.image_recolor),0);
  if(style.image_recolor_opacity) lv_obj_set_style_image_recolor_opa(obj,*style.image_recolor_opacity,0);
  if(n.type==NodeType::Label) {
   auto text=n.label_props.text;
   if(path=="/settings_header/bar/title") text=settings_title;
   if(path.ends_with("/more/value_box/value")) text=locale=="zh_CN"?"语言":"Language";
   if(path.ends_with("/light_mode/label")) text=locale=="zh_CN"?"浅色（重启生效）":"Light (restart required)";
   if(path.ends_with("/dark_mode/label")) text=locale=="zh_CN"?"深色":"Dark";
   if(path=="/overlay/quick_controls/sheet/panel/theme/label") text=locale=="zh_CN"?"主题\n待重启":"Theme\nPending restart";
   if(path=="/overlay/quick_controls/sheet/panel/mute/label") text=locale=="zh_CN"?"取消静音":"Unmute";
   lv_label_set_text(obj,text.c_str());
  }
  if(n.type==NodeType::Image) {
   auto source=n.resolved_image.primary_src;
   int width=n.resolved_image.width,height=n.resolved_image.height;
   auto image_id=n.image_props.src;
   if(image_id.starts_with("quick.") || image_id.starts_with("battery.")) {
    image_id=image_id.substr(0,image_id.rfind('.'))+"."+theme.id;
   }
   if(source.empty() && assets.contains(image_id)) {
    const auto &asset=assets.at(image_id); source=asset.src; width=asset.width; height=asset.height;
   }
   if(!source.empty()) {
    auto image=std::make_unique<Image>();
    image->bytes=read(output/"raw"/(std::filesystem::path(source).filename().string()+".bgra"));
    auto &d=image->descriptor;
    d.header.magic=LV_IMAGE_HEADER_MAGIC; d.header.cf=LV_COLOR_FORMAT_ARGB8888;
    std::ifstream dimensions(output/"raw"/(std::filesystem::path(source).filename().string()+".size"));
    dimensions>>width>>height;
    require(width>0 && height>0,"Invalid fixture image dimensions: "+source);
    d.header.w=width; d.header.h=height; d.header.stride=d.header.w*4;
    d.data_size=static_cast<uint32_t>(image->bytes.size()); d.data=image->bytes.data();
    lv_image_set_src(obj,&d); images.push_back(std::move(image));
    lv_image_set_inner_align(obj,n.image_props.inner_align=="contain"?LV_IMAGE_ALIGN_CONTAIN:LV_IMAGE_ALIGN_DEFAULT);
    lv_image_set_antialias(obj,true);
   }
  }
  if(n.type==NodeType::Slider || n.type==NodeType::ProgressBar) {
   if(n.type==NodeType::Slider) {lv_slider_set_range(obj,n.range_props.min,n.range_props.max);lv_slider_set_value(obj,n.range_props.max,LV_ANIM_OFF);}
   else {lv_bar_set_range(obj,n.range_props.min,n.range_props.max);lv_bar_set_value(obj,n.range_props.max/2,LV_ANIM_OFF);}
   for(const auto &[name,part]:n.part_styles) {
    const auto selector=name=="knob"?LV_PART_KNOB:LV_PART_INDICATOR;
    const auto &s=part.style;
    if(s.bg_color) lv_obj_set_style_bg_color(obj,resolve_color(*s.bg_color),selector);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,selector);
    if(s.radius) lv_obj_set_style_radius(obj,*s.radius,selector);
    if(s.border_width) lv_obj_set_style_border_width(obj,*s.border_width,selector);
    if(s.border_color) lv_obj_set_style_border_color(obj,resolve_color(*s.border_color),selector);
   }
  }
  if(n.layout.type==LayoutType::Flex) {
   const auto flow=n.layout.flex_flow;
   lv_obj_set_flex_flow(obj,flow==FlexFlow::Row?LV_FLEX_FLOW_ROW:flow==FlexFlow::RowWrap?LV_FLEX_FLOW_ROW_WRAP:LV_FLEX_FLOW_COLUMN);
   lv_obj_set_flex_align(obj,flex_align(n.layout.main_align),flex_align(n.layout.cross_align),flex_align(n.layout.cross_align));
   lv_obj_set_style_pad_gap(obj,n.layout.gap,0);
  } else if(n.layout.type==LayoutType::Grid) {
   for(const auto &dims:{n.layout.grid_template_columns,n.layout.grid_template_rows}) {
    std::vector<int32_t> values; for(auto d:dims) values.push_back(track(d));
    values.push_back(LV_GRID_TEMPLATE_LAST); tracks.push_back(std::move(values));
   }
   lv_obj_set_grid_dsc_array(obj,tracks[tracks.size()-2].data(),tracks.back().data());
   lv_obj_set_grid_align(obj,grid_align(n.layout.main_align),grid_align(n.layout.cross_align));
   lv_obj_set_style_pad_gap(obj,n.layout.gap,0);
  }
  const auto &p=n.placement;
  lv_obj_set_size(obj,dimension(p.width),dimension(p.height));
  if(p.mode!=PlacementMode::Flow) {
   lv_obj_add_flag(obj,LV_OBJ_FLAG_IGNORE_LAYOUT); lv_obj_set_pos(obj,p.x.value,p.y.value);
  } else {
   if(lv_obj_get_style_layout(parent,LV_PART_MAIN)==LV_LAYOUT_GRID)
    lv_obj_set_grid_cell(obj,grid_align(p.align_self),p.grid_column,p.grid_column_span,
     grid_align(p.align_self),p.grid_row,p.grid_row_span);
   lv_obj_set_flex_grow(obj,p.flex_grow);
  }
  for(const auto &child:n.children) create(child,obj,path);
  return obj;
 }
 lv_area_t area(std::string path) { lv_area_t a{}; lv_obj_get_coords(objects.at(path),&a); return a; }
 void save(std::string name) {
  std::ofstream geometry(output/(name+".txt"));
  for(const auto &[path,obj]:objects) {
   auto a=area(path);geometry<<path<<" "<<a.x1<<","<<a.y1<<" "<<a.x2<<","<<a.y2<<" hidden="<<lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)<<"\n";
  }
  auto *buffer=lv_snapshot_take(root,LV_COLOR_FORMAT_RGB888); require(buffer!=nullptr,"Snapshot failed");
  std::ofstream f(output/(name+".ppm"),std::ios::binary);
  f<<"P6\n"<<buffer->header.w<<" "<<buffer->header.h<<"\n255\n";
  for(uint32_t y=0;y<buffer->header.h;++y) for(uint32_t x=0;x<buffer->header.w;++x) {
   auto *p=buffer->data+y*buffer->header.stride+x*3; char rgb[]={char(p[2]),char(p[1]),char(p[0])}; f.write(rgb,3);
  }
  lv_draw_buf_destroy(buffer);
 }
};
inline void initialize() {
 static bool initialized=false;
 if(!initialized) {
  lv_init(); auto *display=lv_display_create(320,480); static uint8_t pixels[320*40*2];
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);}); initialized=true;
 }
}
inline void check_app(const Document &doc,const ThemeAsset &theme,const Environment &env,
 const std::string &project,const std::string &output,const std::string &name) {
 initialize();Scene scene(theme,env.language,project,output);
 for(const auto &[key,style]:doc.styles) scene.theme.styles[key]=style;
 for(const auto &asset:doc.images) scene.assets[asset.id]=asset;
 scene.root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(scene.root);lv_obj_set_size(scene.root,320,480);
 for(const auto &node:doc.screens) {auto *obj=scene.create(node,scene.root);lv_obj_set_size(obj,320,480);}
 const std::vector<std::string> pages=name=="music"?std::vector<std::string>{"player","library"}:
  name=="resource-monitor"?std::vector<std::string>{"overview","memory","storage","system"}:std::vector<std::string>{""};
 for(const auto &page:pages) {
  if(!page.empty()) for(const auto &candidate:pages) {
   auto *obj=scene.objects.at("/main/"+candidate);
   if(candidate==page) lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
   else lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_update_layout(scene.root);
  for(const auto &[path,obj]:scene.objects) {
   bool hidden=false;
   for(auto *ancestor=obj;ancestor;ancestor=lv_obj_get_parent(ancestor))
    if(lv_obj_has_flag(ancestor,LV_OBJ_FLAG_HIDDEN)) {hidden=true;break;}
   if(hidden) continue; // Every switchable page is checked in its visible pass.
  lv_area_t a{},parent{};lv_obj_get_coords(obj,&a);lv_obj_get_coords(lv_obj_get_parent(obj),&parent);
  require(a.x1>=0 && a.x2<320,"App content exceeds screen width: "+name+path);
  require(a.x1>=parent.x1 && a.x2<=parent.x2,"App control overflows parent horizontally: "+name+path);
  if(name=="music") {
   require(a.y1>=0 && a.y2<480,"Music control exceeds screen height: "+path);
   require(a.y1>=parent.y1 && a.y2<=parent.y2,"Music control overflows parent vertically: "+path);
  }
  if(lv_obj_check_type(obj,&lv_label_class)) {
   require(a.x1>=parent.x1 && a.x2<=parent.x2,"App label overflows parent: "+name+path);
   require(lv_obj_get_self_height(obj)<=lv_obj_get_height(obj),"App text clipped: "+name+path);
  }
 }
  const auto fixture=name+(page.empty()?"":"_"+page)+"_"+env.theme_id+"_"+env.language+"_"+std::to_string(int(env.density*100));
  scene.save(fixture);
  auto *screen=scene.objects.at("/main");
  if(lv_obj_get_scroll_bottom(screen)>0) {
   lv_obj_scroll_to_y(screen,LV_COORD_MAX,LV_ANIM_OFF);lv_obj_update_layout(scene.root);
   scene.save(fixture+"_bottom");lv_obj_scroll_to_y(screen,0,LV_ANIM_OFF);
  }
 }
}
inline void check(const Document &doc,const ThemeAsset &theme,const Environment &env,
 const std::string &project,const std::string &output,bool shell) {
 initialize();
 for(auto id:shell?std::vector<std::string>{"overlay"}:std::vector<std::string>{"settings_home","display"}) {
  Scene scene(theme,env.language,project,output);
  scene.settings_title=id=="settings_home"?(env.language=="zh_CN"?"设置":"Settings"):(env.language=="zh_CN"?"显示与亮度":"Display & brightness");
  for(const auto &[name,style]:doc.styles) scene.theme.styles[name]=style;
  for(const auto &asset:doc.images) scene.assets[asset.id]=asset;
  scene.root=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(scene.root); lv_obj_set_size(scene.root,320,480);
  for(const auto &node:doc.screens) if(node.id==id || (!shell && node.id=="settings_header")) {
   auto *o=scene.create(node,scene.root); if(node.id!= "settings_header") lv_obj_set_size(o,320,480);
  }
  if(shell) {
   lv_obj_remove_flag(scene.objects.at("/overlay/quick_controls"),LV_OBJ_FLAG_HIDDEN);
   lv_obj_add_flag(scene.objects.at("/overlay/quick_controls/input_shield"),LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_update_layout(scene.root);
  if(!shell) {
   auto header=scene.area("/settings_header/bar"),page=scene.area("/"+id+"/page");
   require(page.y1>header.y2,"Settings content overlaps fixed header");
   if(id=="display") {
    auto card=scene.area("/display/page/theme_modes/light_mode"),preview=scene.area("/display/page/theme_modes/light_mode/preview");
    require(preview.y1>=card.y1 && preview.y2<=card.y2,"Theme preview clipped by fixed card height");
   }
   for(const auto &[path,obj]:scene.objects) if(path.ends_with("/title_box/title") || path.ends_with("/value_box/value") || path=="/settings_header/bar/title") {
    lv_area_t a{},p{}; lv_obj_get_coords(obj,&a); lv_obj_get_coords(lv_obj_get_parent(obj),&p);
    require(a.x1>=p.x1 && a.x2<=p.x2,"Text overflows parent: "+path);
    require(a.y1>=p.y1 && a.y2<=p.y2,"Text clips vertically in its row: "+path);
    require(lv_obj_get_self_height(obj)<=lv_obj_get_height(obj),"Text height clipped: "+path);
   }
   for(const auto &[path,obj]:scene.objects) if(path.ends_with("/title_box") || path.ends_with("/value_box") || path.ends_with("/icon")) {
    lv_area_t a{},p{};lv_obj_get_coords(obj,&a);lv_obj_get_coords(lv_obj_get_parent(obj),&p);
    require(a.y1>=p.y1 && a.y2<=p.y2,"Navigation row clips content vertically: "+path);
   }
  } else {
   auto *icon=scene.objects.at("/overlay/quick_controls/sheet/panel/settings/icon");
   require(lv_image_get_inner_align(icon)==LV_IMAGE_ALIGN_CONTAIN,"Quick icon is cropped instead of fitted");
  }
  scene.save(id+"_"+env.theme_id+"_"+env.language+"_"+std::to_string(int(env.density*100)));
  if(shell) {
   lv_obj_add_flag(scene.objects.at("/overlay/quick_controls"),LV_OBJ_FLAG_HIDDEN);
   lv_obj_update_layout(scene.root);
   const auto battery=scene.area("/overlay/status/status_right/battery");
   require(battery.x1>=0 && battery.x2<320 && battery.y1>=0 && battery.y2<480,"Status battery escapes screen");
   scene.save("status_"+env.theme_id+"_"+env.language+"_"+std::to_string(int(env.density*100)));
  }
 }
}
} // namespace layout_check
