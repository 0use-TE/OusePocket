(function () {
  const vscode = acquireVsCodeApi();
  const screen = document.getElementById('screen');
  const stage = document.getElementById('stage');
  const status = document.getElementById('status');
  const view = document.getElementById('view');
  const toolbar = document.querySelector('.toolbar');
  const scaleMode = document.getElementById('scaleMode');
  const linkButton = document.getElementById('link');
  const actionButtons = Array.from(document.querySelectorAll('button')).filter((button) => button !== linkButton);
  const saved = vscode.getState() || {};
  let width = 320;
  let height = 480;
  let busy = false;
  let connected = false;
  let original = saved.scaleMode === 'native';
  let drag = null;

  scaleMode.value = original ? 'native' : 'fit';
  setConnected(false, '正在连接…');

  function persist() {
    vscode.setState({ scaleMode: original ? 'native' : 'fit' });
  }

  function setBusy(value) {
    busy = value;
    for (const button of actionButtons) button.disabled = value || !connected;
    scaleMode.disabled = value;
    linkButton.disabled = value;
  }

  function setConnected(value, text) {
    connected = value;
    linkButton.textContent = connected ? '断开' : '连接';
    view.hidden = !connected;
    if (!connected) {
      screen.removeAttribute('src');
      stage.style.width = '';
      stage.style.height = '';
    }
    if (text != null) {
      status.textContent = text;
      status.classList.toggle('error', false);
    }
    for (const button of actionButtons) button.disabled = busy || !connected;
  }

  function fitScale() {
    if (!stage || !connected) return;
    let scale = 1;
    if (!original) {
      const pad = 32;
      const used = (toolbar ? toolbar.offsetHeight : 0)
        + (status ? status.offsetHeight + 10 : 0)
        + 20;
      const availW = Math.max(160, document.documentElement.clientWidth - pad);
      const availH = Math.max(200, document.documentElement.clientHeight - pad - used);
      scale = Math.max(0.5, Math.min(2.5, availW / width, availH / height));
    }
    stage.style.width = Math.round(width * scale) + 'px';
    stage.style.height = Math.round(height * scale) + 'px';
  }

  function pointFromEvent(event) {
    const rect = screen.getBoundingClientRect();
    if (!rect.width || !rect.height) return null;
    return {
      x: Math.round((event.clientX - rect.left) * width / rect.width),
      y: Math.round((event.clientY - rect.top) * height / rect.height),
    };
  }

  document.getElementById('refresh').addEventListener('click', () => {
    if (!connected) return;
    vscode.postMessage({ type: 'refresh' });
  });

  linkButton.addEventListener('click', () => {
    if (connected) vscode.postMessage({ type: 'disconnect' });
    else vscode.postMessage({ type: 'connect' });
  });

  scaleMode.addEventListener('change', () => {
    original = scaleMode.value === 'native';
    persist();
    fitScale();
  });

  for (const button of actionButtons) {
    if (!button.dataset.gesture) continue;
    button.addEventListener('click', () => {
      if (!connected) return;
      vscode.postMessage({ type: 'gesture', gesture: button.dataset.gesture });
    });
  }

  screen.addEventListener('pointerdown', (event) => {
    if (busy || !connected) return;
    const point = pointFromEvent(event);
    if (!point) return;
    drag = { id: event.pointerId, start: point, last: point, moved: false };
    try { screen.setPointerCapture(event.pointerId); } catch (_) {}
  });

  screen.addEventListener('pointermove', (event) => {
    if (!drag || drag.id !== event.pointerId) return;
    const point = pointFromEvent(event);
    if (!point) return;
    drag.last = point;
    const dx = point.x - drag.start.x;
    const dy = point.y - drag.start.y;
    if (Math.abs(dx) + Math.abs(dy) >= 8) drag.moved = true;
  });

  function endPointer(event, cancelled) {
    if (!drag || drag.id !== event.pointerId) return;
    const start = drag.start;
    const end = drag.last;
    const moved = drag.moved;
    drag = null;
    try { screen.releasePointerCapture(event.pointerId); } catch (_) {}
    if (busy || !connected || cancelled) return;
    if (!moved) {
      vscode.postMessage({ type: 'tap', x: start.x, y: start.y });
      return;
    }
    vscode.postMessage({
      type: 'swipe',
      x: start.x,
      y: start.y,
      endX: end.x,
      endY: end.y,
    });
  }

  screen.addEventListener('pointerup', (event) => endPointer(event, false));
  screen.addEventListener('pointercancel', (event) => endPointer(event, true));
  // Avoid image drag / click ghosting; gestures go through pointer events.
  screen.addEventListener('click', (event) => event.preventDefault());
  screen.addEventListener('dragstart', (event) => event.preventDefault());
  screen.addEventListener('wheel', (event) => {
    if (busy || !connected) return;
    if (Math.abs(event.deltaY) < Math.abs(event.deltaX) || Math.abs(event.deltaY) < 1) return;
    event.preventDefault();
    vscode.postMessage({ type: 'wheel', deltaY: event.deltaY });
  }, { passive: false });

  window.addEventListener('resize', fitScale);
  if (typeof ResizeObserver !== 'undefined') {
    new ResizeObserver(fitScale).observe(document.documentElement);
  }

  window.addEventListener('message', (event) => {
    const message = event.data;
    if (!message) return;
    if (message.type === 'busy') setBusy(Boolean(message.value));
    else if (message.type === 'disconnected') {
      setConnected(false, '已断开');
      setBusy(false);
    } else if (message.type === 'status') {
      status.textContent = message.text || '';
      status.classList.toggle('error', Boolean(message.error));
      if (connected) fitScale();
    } else if (message.type === 'screen') {
      setConnected(true);
      width = message.width || width;
      height = message.height || height;
      if (!message.image) {
        status.textContent = '没有画面数据';
        status.classList.add('error');
        return;
      }
      const url = 'data:image/jpeg;base64,' + message.image;
      // Cache-bust so the webview always swaps frames after open/navigation.
      screen.onload = () => fitScale();
      screen.onerror = () => {
        status.textContent = '画面解码失败，点刷新重试';
        status.classList.add('error');
      };
      screen.src = url;
      fitScale();
    }
  });

  fitScale();
  vscode.postMessage({ type: 'ready' });
}());
