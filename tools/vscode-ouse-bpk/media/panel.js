(function () {
  const vscode = acquireVsCodeApi();
  const root = document.getElementById('root');
  const portSelect = document.getElementById('port');
  const portCustom = document.getElementById('portCustom');
  let lastState = null;
  let openRoot = '';
  let commandsOpen = null;
  let lastAllFound = null;

  function text(tag, className, value) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    node.textContent = value;
    return node;
  }

  function field(label, name, value) {
    const wrap = document.createElement('label');
    wrap.className = 'field';
    wrap.append(text('span', '', label));
    const input = document.createElement('input');
    input.type = 'text';
    input.name = name;
    input.value = value;
    wrap.append(input);
    return wrap;
  }

  function button(label, className, action) {
    const node = document.createElement('button');
    node.type = 'button';
    node.className = className;
    node.dataset.action = action;
    node.textContent = label;
    return node;
  }

  function portLabel(item) {
    const stripped = item.label.replace(new RegExp('\\s*\\(' + item.id + '\\)\\s*$', 'i'), '').trim();
    if (!stripped || stripped.toUpperCase() === item.id) return item.id;
    return item.id + '  ' + stripped;
  }

  function selectedPort() {
    if (portSelect.value === '__other__') return portCustom.value.trim();
    return portSelect.value;
  }

  function renderPorts(state) {
    const typing = document.activeElement === portCustom;
    const saved = (typing ? portCustom.value : (state.port || '')).trim();
    const savedId = saved.toUpperCase();
    const known = new Set((state.ports || []).map((item) => item.id.toUpperCase()));
    const useCustom = savedId !== '' && !known.has(savedId);
    portSelect.replaceChildren();
    portSelect.append(new Option('选择串口', ''));
    for (const item of state.ports || []) {
      portSelect.append(new Option(portLabel(item), item.id));
    }
    portSelect.append(new Option('其他…', '__other__'));
    portSelect.value = useCustom ? '__other__' : (known.has(savedId) ? savedId : '');
    portCustom.hidden = !useCustom;
    if (!typing) portCustom.value = useCustom ? saved : '';
    portSelect.disabled = Boolean(state.busy);
    portCustom.disabled = Boolean(state.busy);
  }

  function render(state) {
    lastState = state;
    const allFound = (state.commands || []).every((command) => command.found);
    if (commandsOpen === null || lastAllFound !== allFound) {
      commandsOpen = !allFound;
      lastAllFound = allFound;
    }
    root.replaceChildren();
    if (state.notice) {
      root.append(text('div', state.notice.tone === 'error' ? 'notice error' : 'notice', state.notice.text));
    }

    const fold = document.createElement('button');
    fold.type = 'button';
    fold.id = 'commandsToggle';
    fold.className = 'fold';
    if (!commandsOpen && allFound) fold.textContent = '▸  命令  已就绪';
    else if (!commandsOpen) {
      fold.textContent = '▸  命令  未就绪';
      fold.classList.add('bad');
    } else fold.textContent = '▾  命令';
    root.append(fold);

    if (commandsOpen) {
      for (const command of state.commands) {
        const card = document.createElement('section');
        card.className = command.found ? 'command' : 'command missing';
        const header = document.createElement('header');
        header.append(text('strong', command.found ? '' : 'bad', command.label));
        const pick = button('选择', 'pick', 'pick');
        pick.dataset.id = command.id;
        header.append(pick);
        header.append(text('span', command.found ? 'ok' : 'bad', command.found ? '已找到' : '未找到'));
        card.append(header);
        card.append(text('div', 'detail', command.detail));
        if (!command.found && command.hint) card.append(text('div', 'hint', command.hint));
        root.append(card);
      }
    }

    root.append(text('h2', '', '应用'));
    if (!state.apps.length) {
      root.append(text('p', 'detail', '工作区里没有 JavaScript BPK。'));
    }
    for (const app of state.apps) {
      const open = openRoot === app.root;
      const card = document.createElement('section');
      card.className = open ? 'card open' : 'card';
      card.dataset.root = app.root;
      card.dataset.nameEn = app.nameEn;
      card.dataset.nameZh = app.nameZh;
      card.dataset.version = app.version;
      card.dataset.visible = app.visible ? '1' : '0';

      const summary = document.createElement('div');
      summary.className = 'summary';
      const grow = document.createElement('div');
      grow.className = 'grow';
      grow.append(text('strong', 'name', app.nameZh || app.nameEn || app.id));
      grow.append(text('span', 'meta', app.version));
      summary.append(grow, button('安装', 'primary', 'install'));
      card.append(summary);

      if (open) {
        const body = document.createElement('div');
        body.className = 'body';
        body.append(text('div', 'detail id', app.id));
        body.append(text('div', 'meta', (app.kind === 'toolkit' ? 'Toolkit' : '轻量打包') + '  ·  ' + app.relativeRoot));
        body.append(field('中文名', 'nameZh', app.nameZh));
        body.append(field('英文名', 'nameEn', app.nameEn));
        body.append(field('版本', 'version', app.version));
        const visible = document.createElement('label');
        visible.className = 'field check';
        const box = document.createElement('input');
        box.type = 'checkbox';
        box.name = 'visible';
        box.checked = app.visible;
        visible.append(box, text('span', '', '在桌面显示'));
        body.append(visible);
        if (app.services.length) {
          body.append(text('div', 'meta', '服务：' + app.services.map((item) => item.name + (item.version ? ' ' + item.version : '')).join('，')));
        }
        body.append(text('div', 'meta', app.distPath ? '已有包：' + app.distPath : '还没有 bpk'));
        const actions = document.createElement('div');
        actions.className = 'actions';
        actions.append(button('保存', '', 'save'), button('打包', '', 'pack'), button('预览', '', 'simulate'));
        body.append(actions);
        card.append(body);
      }
      root.append(card);
    }

    for (const node of root.querySelectorAll('button, input')) node.disabled = state.busy;
    renderPorts(state);
  }

  function draft(card) {
    const open = card.classList.contains('open');
    const fields = open ? {
      nameEn: card.querySelector('[name=nameEn]').value,
      nameZh: card.querySelector('[name=nameZh]').value,
      version: card.querySelector('[name=version]').value,
      visible: card.querySelector('[name=visible]').checked,
      edit: true,
    } : {
      nameEn: card.dataset.nameEn,
      nameZh: card.dataset.nameZh,
      version: card.dataset.version,
      visible: card.dataset.visible === '1',
      edit: false,
    };
    return { root: card.dataset.root, port: selectedPort(), ...fields };
  }

  portSelect.addEventListener('change', () => {
    const custom = portSelect.value === '__other__';
    portCustom.hidden = !custom;
    if (custom) {
      portCustom.focus();
      return;
    }
    vscode.postMessage({ type: 'setPort', port: portSelect.value });
  });
  portCustom.addEventListener('change', () => {
    vscode.postMessage({ type: 'setPort', port: portCustom.value.trim() });
  });
  root.addEventListener('click', (event) => {
    const target = event.target;
    if (!(target instanceof HTMLElement)) return;
    const action = target.closest('button');
    if (action && action.dataset.action === 'pick') {
      vscode.postMessage({ type: 'pickCommand', id: action.dataset.id });
      return;
    }
    if (action && action.dataset.action) {
      const card = action.closest('.card');
      if (!card) return;
      vscode.postMessage({ type: action.dataset.action, ...draft(card) });
      return;
    }
    if (target.closest('#commandsToggle')) {
      commandsOpen = !commandsOpen;
      if (lastState) render(lastState);
      return;
    }
    const grow = target.closest('.grow');
    if (!grow) return;
    const card = grow.closest('.card');
    openRoot = card && openRoot === card.dataset.root ? '' : (card ? card.dataset.root : '');
    if (lastState) render(lastState);
  });

  window.addEventListener('message', (event) => {
    if (event.data && event.data.type === 'state') render(event.data);
  });
  vscode.postMessage({ type: 'ready' });
}());
