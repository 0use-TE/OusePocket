// SPDX-License-Identifier: Apache-2.0
// Palette source: components/ouse_core/theme/colors.json (regenerate with tools/gen-theme-colors.py)
#include "ouse_theme.h"
#include "ouse_ui.h"
#include "theme/ouse_theme_palette.inc"
static lv_style_t styles[OUSE_STYLE_COUNT];
static bool initialized;
static ouse_theme_t current;
ouse_theme_colors_t ouse_theme_colors(void)
{
    return current==OUSE_THEME_DARK ? ouse_theme_palette_dark : ouse_theme_palette_light;
}
void ouse_theme_set(ouse_theme_t theme)
{
    if (!initialized) {
        for (unsigned i=0;i<OUSE_STYLE_COUNT;++i) lv_style_init(&styles[i]);
        initialized=true;
    }
    if (current!=theme) ouse_ui_theme_changed();
    current=theme;
    bool dark=theme==OUSE_THEME_DARK;
    ouse_theme_colors_t colors=ouse_theme_colors();
    uint32_t bg=colors.desktop, app=colors.background, surface=colors.surface,fg=colors.text;
    uint32_t muted=colors.secondary,accent=colors.accent;
    const uint32_t fills[OUSE_STYLE_COUNT]={bg,app,surface,colors.button,
        colors.pressed,0,accent,dark ? 0x364354 : 0xdddddd,muted,0x1290d8,0};
    for (unsigned i=0;i<OUSE_STYLE_COUNT;++i) {
        lv_style_set_text_color(&styles[i],lv_color_hex(i==OUSE_STYLE_MUTED ? muted : i==OUSE_STYLE_ICON ? colors.icon : i==OUSE_STYLE_ACCENT_TEXT ? accent : fg));
        if (i!=OUSE_STYLE_MUTED && i!=OUSE_STYLE_ACCENT_TEXT) {
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
    if (role!=OUSE_STYLE_MUTED && role!=OUSE_STYLE_ACCENT_TEXT) {
        lv_obj_remove_local_style_prop(object,LV_STYLE_BG_COLOR,selector);
        lv_obj_remove_local_style_prop(object,LV_STYLE_BG_OPA,selector);
    }
    lv_obj_add_style(object,&styles[role],selector);
}
