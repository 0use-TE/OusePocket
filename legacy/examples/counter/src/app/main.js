// Change these two values for the hot-update exercise.
const STEP = 1;
const VERSION = '1.0.0';
let count = 0;
let gui = null;

function callGui(name, params) {
  return brookesia.call_function(gui, name, JSON.stringify(params));
}

function render() {
  callGui('SetText', { Path: '/main/value', Text: String(count) });
  callGui('SetText', {
    Path: '/main/version', Text: 'v' + VERSION + ' | step ' + STEP
  });
}

globalThis.brookesia_app = {
  on_start() {
    count = 0; // State is intentionally reset when reopened or updated.
    gui = brookesia.start_service('SystemGui');
    callGui('SubscribeAction', { Action: 'counter.increment' });
    callGui('SubscribeAction', { Action: 'counter.reset' });
    render();
    return true;
  },
  on_action(action) {
    if (action === 'counter.increment') count += STEP;
    else if (action === 'counter.reset') count = 0;
    else return true;
    render();
    return true;
  },
  on_stop() {
    if (gui !== null) brookesia.stop_service(gui);
    gui = null;
    return true;
  }
};
