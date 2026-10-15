// SPDX-License-Identifier: Apache-2.0
// Colors: legacy/components/brookesia_system_super/resource/themes/color/*.json
#include "ouse_theme.h"
static lv_style_t styles[OUSE_STYLE_COUNT];
static bool initialized;
static ouse_theme_t current;
void ouse_theme_set(ouse_theme_t theme)
{
    if (!initialized) {
        for (unsigned i=0;i<OUSE_STYLE_COUNT;++i) lv_style_init(&styles[i]);
        initialized=true;
    }
    current=theme;
    bool dark=theme==OUSE_THEME_DARK;
    uint32_t bg=dark ? 0x38393a : 0xf3f3f3, app=dark ? 0x38393a : 0xfafbfc;
    uint32_t surface=dark ? 0x4c494b : 0xffffff, fg=dark ? 0xfafbfc : 0x38393a;
    uint32_t muted=dark ? 0xdddddd : 0x7a7a7a, accent=dark ? 0x4dc6fd : 0x1290d8;
    const uint32_t fills[OUSE_STYLE_COUNT]={bg,app,surface,dark ? surface : 0xf3f3f3,
        dark ? 0x364354 : 0xddf4fe,0,accent,dark ? 0x364354 : 0xdddddd,muted,0x1290d8};
    for (unsigned i=0;i<OUSE_STYLE_COUNT;++i) {
        lv_style_set_text_color(&styles[i],lv_color_hex(i==OUSE_STYLE_MUTED ? muted : i==OUSE_STYLE_ICON ? (dark ? 0xff8e78 : 0xe8362d) : fg));
        if (i!=OUSE_STYLE_MUTED) {
            lv_style_set_bg_color(&styles[i],lv_color_hex(fills[i]));
            lv_style_set_bg_opa(&styles[i],i==OUSE_STYLE_ICON ? LV_OPA_TRANSP : LV_OPA_COVER);
        }
        lv_obj_report_style_change(&styles[i]);
    }
}
ouse_theme_t ouse_theme_get(void) { return current; }
void ouse_theme_apply(lv_obj_t *object, ouse_style_role_t role, lv_style_selector_t selector)
{
    if (!initialized) ouse_theme_set(OUSE_THEME_LIGHT);
    lv_obj_remove_local_style_prop(object,LV_STYLE_TEXT_COLOR,selector);
    if (role!=OUSE_STYLE_MUTED) {
        lv_obj_remove_local_style_prop(object,LV_STYLE_BG_COLOR,selector);
        lv_obj_remove_local_style_prop(object,LV_STYLE_BG_OPA,selector);
    }
    lv_obj_add_style(object,&styles[role],selector);
}
