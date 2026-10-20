// Include this file before app code when packing. One submit per completed frame.
function createCanvas(id) {
    let commands = [];
    const canvas = {
        beginFrame() { commands = []; return canvas; },
        endFrame() { Ouse.canvas.submit(id, commands); commands = []; },
        clear() { Ouse.canvas.submit(id, []); commands = []; }
    };
    for (const op of ['fillRect', 'strokeRect', 'drawLine', 'drawArc',
                      'drawImage', 'drawText', 'fillTriangle']) {
        canvas[op] = function (options) {
            if (commands.length >= 256) throw new RangeError('At most 256 canvas commands');
            commands.push(Object.assign({}, options, { op }));
            return canvas;
        };
    }
    return canvas;
}
