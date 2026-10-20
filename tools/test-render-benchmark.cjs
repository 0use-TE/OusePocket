// Run the delivered app scripts, including their actual startup/lifecycle paths.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');
const root = path.resolve(__dirname, '..');
function load(mode) {
    const timers = new Map();
    const frames = [];
    let sequence = 0, current = [];
    const context = vm.createContext({
        console: { log() {} },
        Ouse: {
            display: { get: () => ({orientation:'landscape',contentWidthDp:480,contentHeightDp:256}), onChange() {} },
            ui: { layout() {}, show() {}, hide() {}, setText() {} },
            timers: { setInterval(fn, ms) { assert.strictEqual(ms, 50);timers.set(++sequence,fn);return sequence; }, clear(id) { timers.delete(id); } },
            canvas: { submit(id, commands) {
                assert.strictEqual(id,'world');
                current.push(...commands.map((c,i) => ({ id:'e'+i, x:c.x, y:c.y, frame:c.sourceX/16+c.sourceY/16*2 })));
            } },
            sprites: { update(patches) { assert(patches.length<=32);current.push(...patches); } }
        }
    });
    vm.runInContext(fs.readFileSync(path.join(root,'apps','benchmark-'+mode,'app.js'),'utf8'),context);
    assert.strictEqual(timers.size,1,'script must start on initial load');
    context.app.onPause();assert.strictEqual(timers.size,0);
    context.app.onResume();assert.strictEqual(timers.size,1);
    for(let i=0;i<800;i++) {
        current=[];[...timers.values()][0]();
        assert.strictEqual(current.length,[8,32,64,96][Math.floor(i/200)]);
        frames.push(JSON.parse(JSON.stringify(current)));
    }
    context.app.onClose();assert.strictEqual(timers.size,0);
    return frames;
}
assert.deepStrictEqual(load('canvas'),load('json'));
console.log('PASS actual benchmark app startup, pause/resume/close and matching entities across all 800 frames');
