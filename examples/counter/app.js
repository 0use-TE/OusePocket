"use strict";
let count = 0;

function render() {
    Ouse.ui.setText("value", String(count));
}

Ouse.ui.on("increment", "click", () => {
    ++count;
    render();
});

Ouse.ui.on("reset", "click", () => {
    count = 0;
    render();
});

Ouse.locale.onChange(render);
globalThis.app = { onResume() { render(); } };
render();
