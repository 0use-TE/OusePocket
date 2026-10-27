// ArrayBuffers avoid per-entity property lookup, temporary objects and GC.
static JSValue game2d_projectiles(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;
    if(argc!=4)return JS_ThrowTypeError(context,"Expected shots, targets, hits ArrayBuffers and dt");
    for(unsigned i=0;i<3;++i)if(!JS_IsArrayBuffer(argv[i]))return JS_ThrowTypeError(context,"Expected ordinary ArrayBuffers");
    // Coercion can execute JS (including buffer detachment); do it first.
    double dt;if(!JS_IsNumber(argv[3]) || JS_ToFloat64(context,&dt,argv[3]))return JS_ThrowTypeError(context,"Expected numeric dt");
    size_t a=0,b=0,c=0;
    uint8_t *shots=JS_GetArrayBuffer(context,&a,argv[0]);if(!shots)return JS_ThrowTypeError(context,"Empty or detached shots buffer");
    uint8_t *targets=JS_GetArrayBuffer(context,&b,argv[1]);if(!targets)return JS_ThrowTypeError(context,"Empty or detached targets buffer");
    uint8_t *hits=JS_GetArrayBuffer(context,&c,argv[2]);if(!hits)return JS_ThrowTypeError(context,"Empty or detached hits buffer");
    if(shots==targets || shots==hits || targets==hits || a%64 || b%32 || c!=a/64*4 ||
       !ouse_game2d_projectiles(shots,a/64,targets,b/32,hits,dt))
        return JS_ThrowRangeError(context,"Invalid game2d buffers, limits or values");
    return JS_UNDEFINED;
}
