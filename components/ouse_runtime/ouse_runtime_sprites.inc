// Worker-only API. Parse all user values before taking the model lock; commit
// one bounded batch, so rendering never observes half an actor update.
static JSValue sprites_update(JSContext *context, JSValueConst self, int argc,
                              JSValueConst *argv) {
  (void)self;
  instance_t *app = from_js(context);
  typedef struct {
    char id[32];
    int values[6];
    unsigned mask, index;
    bool playing, loop;
    unsigned flags;
  } patch_t;
  patch_t patches[32] = {0};
  uint32_t count = 0;
  if (argc != 1 || !JS_IsArray(argv[0]))
    return JS_ThrowTypeError(context, "Expected sprite update array");
  JSValue size = JS_GetPropertyStr(context, argv[0], "length");
  int error = JS_ToUint32(context, &count, size);
  JS_FreeValue(context, size);
  if (error || count > 32)
    return JS_ThrowRangeError(context, "At most 32 sprites per update");
  const char *keys[] = {"x", "y", "frame", "firstFrame", "frameCount", "fps"};
  for (unsigned i = 0; i < count; ++i) {
    JSValue item = JS_GetPropertyUint32(context, argv[0], i),
            id = JS_GetPropertyStr(context, item, "id");
    bool valid = copy_string(context, id, patches[i].id, sizeof(patches[i].id));
    JS_FreeValue(context, id);
    for (unsigned k = 0; k < 6 && valid; ++k) {
      JSValue v = JS_GetPropertyStr(context, item, keys[k]);
      if (!JS_IsUndefined(v)) {
        double n = 0;
        valid = JS_IsNumber(v) && !JS_ToFloat64(context, &n, v) &&
                isfinite(n) && floor(n) == n && n >= -16384 && n <= 16384;
        if (valid) {
          patches[i].values[k] = (int)n;
          patches[i].mask |= 1u << k;
        }
      }
      JS_FreeValue(context, v);
    }
    for (unsigned k = 0; k < 2 && valid; ++k) {
      JSValue v = JS_GetPropertyStr(context, item, k ? "loop" : "playing");
      if (!JS_IsUndefined(v)) {
        valid = JS_IsBool(v);
        if (valid) {
          patches[i].flags |= 1u << k;
          if (k)
            patches[i].loop = JS_ToBool(context, v);
          else
            patches[i].playing = JS_ToBool(context, v);
        }
      }
      JS_FreeValue(context, v);
    }
    JS_FreeValue(context, item);
    if (!valid)
      return JS_ThrowTypeError(context, "Invalid sprite patch");
    for (unsigned j = 0; j < i; ++j)
      if (!strcmp(patches[j].id, patches[i].id))
        return JS_ThrowTypeError(context, "Duplicate sprite id");
  }
  model_lock(app);
  bool valid =
      !app->closed && !app->faulted && app->navigation_stage == NAV_IDLE;
  for (unsigned i = 0; i < count && valid; ++i) {
    unsigned n = 0;
    while (n < app->node_count && strcmp(app->nodes[n].model.id, patches[i].id))
      ++n;
    valid = n < app->node_count && app->nodes[n].model.type == SPRITE;
    if (!valid)
      break;
    patches[i].index = n;
    node_model_t *m = &app->nodes[n].model;
    const lv_image_dsc_t *atlas = find_image(app, m->image);
    int frames = atlas ? (atlas->header.w / m->sprite_w) *
                             (atlas->header.h / m->sprite_h)
                       : 0;
    int frame = patches[i].mask & 4 ? patches[i].values[2] : m->sprite_frame,
        first = patches[i].mask & 8 ? patches[i].values[3] : m->sprite_first;
    int total = patches[i].mask & 16 ? patches[i].values[4] : m->sprite_count,
        fps = patches[i].mask & 32 ? patches[i].values[5] : m->sprite_fps;
    valid = frame >= 0 && frame < frames && first >= 0 && total > 0 &&
            first + total <= frames && fps >= 1 && fps <= 30;
  }
  if (valid)
    for (unsigned i = 0; i < count; ++i) {
      patch_t *p = &patches[i];
      node_model_t *m = &app->nodes[p->index].model;
      int *fields[] = {&m->x,
                       &m->y,
                       &m->sprite_frame,
                       &m->sprite_first,
                       &m->sprite_count,
                       &m->sprite_fps};
      bool moved = false, changed = false;
      for (unsigned k = 0; k < 6; ++k)
        if (p->mask & (1u << k)) {
          if (*fields[k] != p->values[k]) {
            if (k < 2)
              moved = true;
            else
              changed = true;
            *fields[k] = p->values[k];
          }
        }
      if ((p->flags & 1) && m->sprite_playing != p->playing) {
        m->sprite_playing = p->playing;
        changed = true;
      }
      if ((p->flags & 2) && m->sprite_loop != p->loop) {
        m->sprite_loop = p->loop;
        changed = true;
      }
      if (p->mask & 28u) {
        if (!(p->mask & 4u))
          m->sprite_frame = m->sprite_first;
        ++m->sprite_restart;
      }
      if (moved)
        ++m->style_revision;
      if (changed || (p->mask & 28u))
        ++m->image_revision;
    }
  model_unlock(app);
  return valid ? JS_UNDEFINED
               : JS_ThrowRangeError(
                     context, "Invalid sprite, frame range or inactive page");
}
