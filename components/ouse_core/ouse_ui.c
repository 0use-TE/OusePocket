// SPDX-License-Identifier: MIT
#include "ouse_ui.h"
#include <string.h>
#include <stdlib.h>
#include "ui_strings.h"
#include "ui_font_pool.h"
typedef struct binding {
    struct binding *next;
    lv_obj_t *object;
    unsigned sp;
    char *source, *english;
} binding_t;
static binding_t *bindings;
static unsigned density=160, locale;
static uint32_t revisions[3], generation;
int ouse_ui_dp(int value) { return (value*(int)density+(value<0 ? -80 : 80))/160; }
int ouse_ui_to_dp(int value) { return (value*160+(int)density/2)/(int)density; }
unsigned ouse_ui_density(void) { return density; }
unsigned ouse_ui_locale(void) { return locale; }
unsigned ouse_ui_font_percent(void) { return 100; }
uint32_t ouse_ui_revision(unsigned group) { return group<3 ? revisions[group] : 0; }
uint32_t ouse_ui_generation(void) { return generation; }
void ouse_ui_theme_changed(void) { ++revisions[0]; ++generation; }
void ouse_ui_display_changed(void) { ++revisions[2]; }
const lv_font_t *ouse_ui_font(unsigned sp)
{
    unsigned pixels=(sp*density+80)/160, best=0, distance=~0u;
    for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        unsigned d=sizes[i]>pixels ? sizes[i]-pixels : pixels-sizes[i];
        if (d<distance) { best=i; distance=d; }
    }
    return fonts[best];
}
const char *ouse_ui_tr(const char *source)
{
    for (unsigned i=0;i<sizeof(ui_strings)/sizeof(ui_strings[0]);++i)
        if (!strcmp(source,ui_strings[i].zh) || !strcmp(source,ui_strings[i].en)) return locale ? ui_strings[i].en : ui_strings[i].zh;
    return source;
}
static void deleted(lv_event_t *event)
{
    binding_t *entry=lv_event_get_user_data(event), **link=&bindings;
    while (*link && *link!=entry) link=&(*link)->next;
    if (*link) *link=entry->next;
    free(entry->source); free(entry->english); free(entry);
}
static binding_t *binding(lv_obj_t *object)
{
    for (binding_t *entry=bindings;entry;entry=entry->next) if (entry->object==object) return entry;
    binding_t *entry=calloc(1,sizeof(*entry));
    if (!entry) return NULL;
    entry->object=object; entry->next=bindings; bindings=entry;
    lv_obj_add_event_cb(object,deleted,LV_EVENT_DELETE,entry);
    return entry;
}
void ouse_ui_text(lv_obj_t *label,const char *source)
{
    if (!*source) { ouse_ui_unbind_text(label); lv_label_set_text(label,source); return; }
    binding_t *entry=binding(label);
    if (entry && entry->source && !entry->english && !strcmp(entry->source,source)) return;
    if (entry && (!entry->source || strcmp(entry->source,source))) {
        char *copy=malloc(strlen(source)+1);
        if (copy) { strcpy(copy,source); free(entry->source); entry->source=copy; }
    }
    lv_label_set_text(label,ouse_ui_tr(source));
}
void ouse_ui_unbind_text(lv_obj_t *label)
{
    for (binding_t *entry=bindings;entry;entry=entry->next) if(entry->object==label) {
        free(entry->source); free(entry->english); entry->source=entry->english=NULL; return;
    }
}
void ouse_ui_text_pair(lv_obj_t *label,const char *zh,const char *en)
{
    binding_t *entry=binding(label);
    char *a=malloc(strlen(zh)+1), *b=malloc(strlen(en)+1);
    if (entry && a && b) {
        strcpy(a,zh); strcpy(b,en); free(entry->source); free(entry->english);
        entry->source=a; entry->english=b;
    } else { free(a); free(b); }
    lv_label_set_text(label,locale ? en : zh);
}
void ouse_ui_use_font(lv_obj_t *object,unsigned sp)
{
    binding_t *entry=binding(object); if (entry) entry->sp=sp;
    lv_obj_set_style_text_font(object,ouse_ui_font(sp),0);
}
unsigned ouse_ui_binding_count(void)
{ unsigned count=0; for(binding_t *entry=bindings;entry;entry=entry->next) ++count; return count; }
esp_err_t ouse_ui_set_preferences(unsigned next_locale,unsigned next_font)
{
    if (next_locale>1 || next_font!=100) return ESP_ERR_INVALID_ARG;
    bool language_changed=locale!=next_locale;
    locale=next_locale;
    if (language_changed) ++revisions[1];
    if (language_changed) {
        ++generation;
        for (binding_t *entry=bindings;entry;entry=entry->next) {
            if (language_changed && entry->source)
                lv_label_set_text(entry->object,entry->english ? (locale ? entry->english : entry->source) : ouse_ui_tr(entry->source));
        }
    }
    return ESP_OK;
}
esp_err_t ouse_ui_init(unsigned dpi,unsigned next_locale,unsigned next_font)
{
    if (!dpi) dpi=160;
    if (dpi<80 || dpi>320 || next_locale>1 || (next_font && next_font!=100))
        return ESP_ERR_INVALID_ARG;
    if (density!=dpi) { density=dpi; ++revisions[2]; ++generation; }
    return ouse_ui_set_preferences(next_locale,next_font ? next_font : 100);
}
