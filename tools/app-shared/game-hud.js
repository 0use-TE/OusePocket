// Lightweight retained HUD over Canvas. Submit only when the displayed state
// changes. Menus remain ordinary UI nodes; the scene has its own display list.
function createGameHud(id) {
    let last='';
    return { invalidate(){last='';}, draw(state,width) {
        const key=JSON.stringify(state)+'|'+width;
        if(key===last)return false;
        const hp=Math.max(0,Math.min(1,state.hp/state.maxHp));
        const mp=Math.max(0,Math.min(1,state.mp/state.maxMp));
        const xp=Math.max(0,Math.min(1,state.xp/state.nextXp));
        const bar=Math.max(60,Math.min(112,width-176));
        const commands=[
            {op:'fillRect',x:0,y:0,width,height:30,color:0x25332e},
            {op:'drawText',x:5,y:0,width:60,height:26,text:'LV '+state.level,color:0xffdfa1},
            {op:'fillRect',x:60,y:4,width:bar,height:6,color:0x452b2a},
            {op:'fillRect',x:60,y:4,width:Math.max(1,Math.round(hp*bar)),height:6,color:0xe46450},
            {op:'fillRect',x:60,y:13,width:bar,height:5,color:0x233642},
            {op:'fillRect',x:60,y:13,width:Math.max(1,Math.round(mp*bar)),height:5,color:0x6ecce9},
            {op:'fillRect',x:60,y:22,width:bar,height:3,color:0x494125},
            {op:'fillRect',x:60,y:22,width:Math.max(1,Math.round(xp*bar)),height:3,color:0xffd46e},
            {op:'drawText',x:bar+80,y:0,width:width-bar-82,height:28,
                text:Math.ceil(state.hp)+'/'+state.maxHp+'  '+state.kills+'/'+state.total,color:0xf4e9c9}
        ];
        Ouse.canvas.submit(id,commands);last=key;return true;
    }};
}
