// Geometry batches use logical page units. All getters run before the lock;
// every node/range is checked before the first model mutation.
static JSValue ui_layout(JSContext *context, JSValueConst self, int argc,
                         JSValueConst *argv) {
  (void)self;
  instance_t *app = from_js(context);
  typedef struct {
    char id[32];
    int x, y, w, h;
    unsigned index;
  } frame_t;
  frame_t frames[64];
  uint32_t count = 0;
  if (argc != 1 || !JS_IsArray(argv[0]))
    return JS_ThrowTypeError(context, "layout expects frame array");
  JSValue length = JS_GetPropertyStr(context, argv[0], "length");
  int error = JS_ToUint32(context, &count, length);
  JS_FreeValue(context, length);
  if (error || count > 64)
    return JS_ThrowRangeError(context, "At most 64 layout frames");
  const char *keys[] = {"x", "y", "width", "height"};
  for (unsigned i = 0; i < count; ++i) {
    JSValue item = JS_GetPropertyUint32(context, argv[0], i),
            id = JS_GetPropertyStr(context, item, "id");
    bool valid = copy_string(context, id, frames[i].id, sizeof(frames[i].id));
    JS_FreeValue(context, id);
    int *fields[] = {&frames[i].x, &frames[i].y, &frames[i].w, &frames[i].h};
    for (unsigned k = 0; k < 4 && valid; ++k) {
      JSValue v = JS_GetPropertyStr(context, item, keys[k]);
      double n = 0;
      valid = JS_IsNumber(v) && !JS_ToFloat64(context, &n, v) && isfinite(n) &&
              floor(n) == n && n >= (k < 2 ? -16384 : 1) && n <= 16384;
      if (valid)
        *fields[k] = (int)n;
      JS_FreeValue(context, v);
    }
    JS_FreeValue(context, item);
    if (!valid)
      return JS_ThrowTypeError(context, "Invalid layout frame");
    for (unsigned j = 0; j < i; ++j)
      if (!strcmp(frames[i].id, frames[j].id))
        return JS_ThrowTypeError(context, "Duplicate layout id");
  }
  model_lock(app);
  bool valid =
      !app->closed && !app->faulted && app->navigation_stage == NAV_IDLE;
  for (unsigned i = 0; i < count && valid; ++i) {
    unsigned n = 0;
    while (n < app->node_count && strcmp(app->nodes[n].model.id, frames[i].id))
      ++n;
    valid = n < app->node_count;
    frames[i].index = n;
  }
  if (valid)
    for (unsigned i = 0; i < count; ++i) {
      frame_t *f = &frames[i];
      node_model_t *m = &app->nodes[f->index].model;
      if (m->x != f->x || m->y != f->y || m->w != f->w || m->h != f->h ||
          m->fill_w || m->fill_h || m->content_h) {
        m->x = f->x;
        m->y = f->y;
        m->w = f->w;
        m->h = f->h;
        m->fill_w = m->fill_h = m->content_h = false;
        ++m->style_revision;
      }
    }
  model_unlock(app);
  return valid ? JS_TRUE
               : JS_ThrowTypeError(context,
                                   "Unknown layout node or inactive page");
}
