(function () {
  const vscode = acquireVsCodeApi();
  const root = document.getElementById('root');
  const deviceToggle = document.getElementById('deviceToggle');
  const deviceBody = document.getElementById('deviceBody');
  const portSelect = document.getElementById('port');
  const baudSelect = document.getElementById('baud');
  const portCustom = document.getElementById('portCustom');
  const baudCustom = document.getElementById('baudCustom');
  const BAUD_PRESETS = ['9600', '19200', '38400', '57600', '115200', '230400', '460800', '921600', '1000000', '1500000', '2000000', '3000000'];
  let lastState = null;
  let openRoot = '';
  let deviceOpen = false;
  let commandsOpen = null;
  let lastAllFound = null;
  const sectionOpen = {};

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
    input.spellcheck = false;
    wrap.append(input);
    return wrap;
  }

  function iconField(value) {
    const wrap = document.createElement('label');
    wrap.className = 'field';
    wrap.append(text('span', '', '图标'));
    const select = document.createElement('select');
    select.name = 'icon';
    const choices = [['icon.bin', 'icon.bin · 预转换图片'], ['icon.png', 'icon.png · PNG 图片（Pillow）']];
    for (const [id, label] of choices) {
      select.append(new Option(label, id));
    }
    if (!choices.some((item) => item[0] === value)) select.append(new Option(value || '未配置', value));
    select.value = value;
    wrap.append(select);
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

  function check(name, label, checked) {
    const wrap = document.createElement('label');
    wrap.className = 'field check';
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.name = name;
    box.checked = checked;
    wrap.append(box, text('span', '', label));
    return wrap;
  }

  function section(cardRoot, key, title, build) {
    const open = sectionOpen[cardRoot]?.[key] !== false;
    if (!sectionOpen[cardRoot]) sectionOpen[cardRoot] = {};
    if (sectionOpen[cardRoot][key] === undefined) sectionOpen[cardRoot][key] = key === 'app';
    const shown = sectionOpen[cardRoot][key];
    const block = document.createElement('div');
    block.className = 'section';
    const toggle = document.createElement('button');
    toggle.type = 'button';
    toggle.className = 'section-fold';
    toggle.dataset.section = key;
    toggle.textContent = (shown ? '▾  ' : '▸  ') + title;
    block.append(toggle);
    if (shown) {
      const body = document.createElement('div');
      body.className = 'section-body';
      build(body);
      block.append(body);
    }
    return block;
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

  function selectedBaud() {
    if (baudSelect.value === '__other__') return Number(baudCustom.value.trim());
    return Number(baudSelect.value);
  }

  function renderBaud(state) {
    const typing = document.activeElement === baudCustom;
    const saved = String(typing ? baudCustom.value : (state.baud || '921600')).trim();
    const useCustom = saved !== '' && !BAUD_PRESETS.includes(saved);
    baudSelect.replaceChildren();
    for (const rate of BAUD_PRESETS) baudSelect.append(new Option(rate, rate));
    baudSelect.append(new Option('自定义…', '__other__'));
    baudSelect.value = useCustom ? '__other__' : (BAUD_PRESETS.includes(saved) ? saved : '921600');
    baudCustom.hidden = !useCustom;
    if (!typing) baudCustom.value = useCustom ? saved : '';
    baudSelect.disabled = Boolean(state.busy);
    baudCustom.disabled = Boolean(state.busy);
  }

  function renderDevice(state) {
    const port = (state.port || '').trim() || '未选串口';
    deviceToggle.textContent = (deviceOpen ? '▾  ' : '▸  ') + '设备  ' + port;
    deviceBody.hidden = !deviceOpen;
    renderPorts(state);
    renderBaud(state);
    document.getElementById('sync').disabled = Boolean(state.busy);
    document.getElementById('device').disabled = Boolean(state.busy);
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
    if (!commandsOpen && allFound) fold.textContent = '▸  环境  已就绪';
    else if (!commandsOpen) {
      fold.textContent = '▸  环境  未就绪';
      fold.classList.add('bad');
    } else fold.textContent = '▾  环境';
    root.append(fold);
    if (commandsOpen) {
      for (const command of state.commands) {
        const card = document.createElement('section');
        card.className = command.found ? 'command' : 'command missing';
        const header = document.createElement('header');
        header.append(text('strong', command.found ? '' : 'bad', command.label));
        if (command.id === 'python') {
          const pick = button('选择', 'pick', 'pick');
          pick.dataset.id = command.id;
          header.append(pick);
        }
        header.append(text('span', command.found ? 'ok' : 'bad', command.found ? '已找到' : '未找到'));
        card.append(header);
        card.append(text('div', 'detail', command.detail));
        if (!command.found && command.hint) card.append(text('div', 'hint', command.hint));
        root.append(card);
      }
    }

    const heading = document.createElement('div');
    heading.className = 'apps-heading';
    heading.append(text('h2', '', '应用'));
    if (state.mirror) heading.append(text('span', 'meta', '画面占用串口'));
    root.append(heading);
    if (!state.apps.length) {
      root.append(text('p', 'empty', '没有可安装的应用。'));
    }
    const installed = new Set(state.installed || []);
    const alive = new Set(state.alive || []);
    for (const app of state.apps) {
      const open = openRoot === app.root;
      const onDevice = state.synced && installed.has(app.id);
      const isActive = state.synced && state.active === app.id;
      const isAlive = state.synced && alive.has(app.id);
      const card = document.createElement('section');
      card.className = open ? 'card open' : 'card';
      card.dataset.root = app.root;
      card.dataset.id = app.id;
      card.dataset.name = app.name;
      card.dataset.icon = app.icon;
      card.dataset.background = app.background ? '1' : '0';
      card.dataset.notifications = app.notifications ? '1' : '0';

      const summary = document.createElement('div');
      summary.className = 'summary';
      const grow = document.createElement('div');
      grow.className = 'grow';
      const title = document.createElement('div');
      title.className = 'title-row';
      title.append(text('strong', 'name', app.name || app.id || '未命名'));
      if (isActive) title.append(text('span', 'badge live', '前台'));
      else if (isAlive) title.append(text('span', 'badge bg', '后台'));
      else if (onDevice) title.append(text('span', 'badge on', '已安装'));
      grow.append(title);
      grow.append(text('div', 'id', app.id || '无 ID'));
      const mainAction = onDevice
        ? button('卸载', 'danger', 'remove')
        : button('安装', 'primary', 'install');
      summary.append(
        grow,
        button('预览', 'ghost preview', 'preview'),
        mainAction,
        text('span', 'chevron', open ? '▾' : '▸'),
      );
      card.append(summary);

      if (open) {
        const body = document.createElement('div');
        body.className = 'body';
        body.append(section(app.root, 'app', '应用', (slot) => {
          slot.append(field('ID', 'id', app.id));
          slot.append(field('名称', 'name', app.name));
          slot.append(field('中文', 'nameZh', app.names?.zh_CN || ''));
          slot.append(field('English', 'nameEn', app.names?.en || ''));
          slot.append(iconField(app.icon));
          slot.append(check('openAfter', '安装后打开', true));
        }));
        body.append(section(app.root, 'perm', '权限', (slot) => {
          slot.append(check('background', '后台', Boolean(app.background)));
          slot.append(check('notifications', '通知', Boolean(app.notifications)));
        }));
        const actions = document.createElement('div');
        actions.className = 'actions';
        actions.append(button('保存清单', 'ghost', 'save'), button('打包', 'ghost', 'pack'));
        body.append(actions);
        card.append(body);
      }
      root.append(card);
    }

    for (const node of root.querySelectorAll('button, input, select')) node.disabled = state.busy;
    renderDevice(state);
  }

  function draft(card) {
    const open = card.classList.contains('open');
    const fields = open ? {
      id: card.querySelector('[name=id]')?.value ?? card.dataset.id,
      name: card.querySelector('[name=name]')?.value ?? card.dataset.name,
      nameZh: card.querySelector('[name=nameZh]')?.value,
      nameEn: card.querySelector('[name=nameEn]')?.value,
      icon: card.querySelector('[name=icon]')?.value ?? card.dataset.icon,
      background: Boolean(card.querySelector('[name=background]')?.checked),
      notifications: Boolean(card.querySelector('[name=notifications]')?.checked),
      openAfter: card.querySelector('[name=openAfter]') ? card.querySelector('[name=openAfter]').checked : true,
      edit: true,
    } : {
      id: card.dataset.id,
      name: card.dataset.name,
      icon: card.dataset.icon,
      background: card.dataset.background === '1',
      notifications: card.dataset.notifications === '1',
      openAfter: true,
      edit: false,
    };
    return { root: card.dataset.root, port: selectedPort(), baud: selectedBaud(), ...fields };
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
  document.getElementById('sync').addEventListener('click', () => {
    vscode.postMessage({ type: 'sync', port: selectedPort(), baud: selectedBaud() });
  });
  document.getElementById('device').addEventListener('click', () => {
    vscode.postMessage({ type: 'device', port: selectedPort(), baud: selectedBaud() });
  });
  baudSelect.addEventListener('change', () => {
    const custom = baudSelect.value === '__other__';
    baudCustom.hidden = !custom;
    if (custom) {
      baudCustom.focus();
      return;
    }
    vscode.postMessage({ type: 'setBaud', baud: Number(baudSelect.value) });
  });
  baudCustom.addEventListener('change', () => {
    vscode.postMessage({ type: 'setBaud', baud: Number(baudCustom.value.trim()) });
  });
  deviceToggle.addEventListener('click', () => {
    deviceOpen = !deviceOpen;
    if (lastState) render(lastState);
  });
  root.addEventListener('click', (event) => {
    const target = event.target;
    if (!(target instanceof HTMLElement)) return;
    const sectionFold = target.closest('.section-fold');
    if (sectionFold) {
      const card = sectionFold.closest('.card');
      const key = sectionFold.dataset.section;
      if (!card || !key) return;
      if (!sectionOpen[card.dataset.root]) sectionOpen[card.dataset.root] = {};
      sectionOpen[card.dataset.root][key] = !sectionOpen[card.dataset.root][key];
      if (lastState) render(lastState);
      return;
    }
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
    if (target.closest('button')) return;
    const summary = target.closest('.summary');
    if (!summary) return;
    const card = summary.closest('.card');
    openRoot = card && openRoot === card.dataset.root ? '' : (card ? card.dataset.root : '');
    if (lastState) render(lastState);
  });

  window.addEventListener('message', (event) => {
    if (event.data && event.data.type === 'state') render(event.data);
  });
  vscode.postMessage({ type: 'ready' });
}());
