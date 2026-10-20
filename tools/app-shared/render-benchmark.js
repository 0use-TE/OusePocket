// Shared by both apps AND the native host benchmark. No random or elapsed-time
// dependency: the same frame/count/dimensions always produces the same scene.
function benchmarkScene(count, frame, width, height) {
    const actors = [];
    for (let i = 0; i < count; i++) {
        const periodX = 2 * (width - 16), periodY = 2 * (height - 16);
        let x = (i * 47 + frame * (1 + i % 3)) % periodX;
        let y = (i * 31 + frame * (1 + i % 2)) % periodY;
        if (x > width - 16) x = periodX - x;
        if (y > height - 16) y = periodY - y;
        actors.push({ id: 'e' + i, x, y, frame: (i + Math.floor(frame / 4)) % 4 });
    }
    return actors;
}
function benchmarkSubmit(mode, actors) {
    if (mode === 'canvas') {
        Ouse.canvas.submit('world', actors.map(a => ({
            op: 'drawImage', src: 'actors', x: a.x, y: a.y, width: 16, height: 16,
            sourceX: a.frame % 2 * 16, sourceY: Math.floor(a.frame / 2) * 16,
            sourceWidth: 16, sourceHeight: 16
        })));
    } else {
        for (let i = 0; i < actors.length; i += 32)
            Ouse.sprites.update(actors.slice(i, i + 32));
    }
}
