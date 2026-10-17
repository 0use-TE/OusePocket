(function () {
  const vscode = acquireVsCodeApi();
  const screen = document.getElementById('screen');
  const phone = document.getElementById('phone');
  const home = document.getElementById('home');
  const fault = document.getElementById('fault');
  const log = document.getElementById('log');
  const title = document.getElementById('title');
  const themeButton = document.getElementById('theme');
  const localeButton = document.getElementById('locale');
  const restartButton = document.getElementById('restart');
  const resumeButton = document.getElementById('resume');

  const SYSTEM_LOCALES = ['zh_CN', 'en'];
  const LOCALE_NAMES = {
    zh_CN: '简体中文', zh_TW: '繁體中文', zh_HK: '繁體中文（香港）',
    en: 'English', en_US: 'English (US)', en_GB: 'English (UK)',
    ja: '日本語', ko: '한국어', fr: 'Français', de: 'Deutsch',
    es: 'Español', pt: 'Português', ru: 'Русский', ar: 'العربية',
  };

  // Filled from components/ouse_core/theme/colors.json via extension (media/theme-colors.json copy).
  let LIGHT = null;
  let DARK = null;
  const THEME_TOKENS = ['background', 'desktop', 'surface', 'text', 'secondary', 'accent', 'button', 'pressed', 'icon', 'border', 'disabled', 'error'];

  const widgets = new Map();
  const unbound = new Set();
  const listeners = { theme: null, locale: null, display: null, power: null, audio: null, wifi: null };
  let translations = {};
  let locale = 'zh_CN';
  let mode = 'light';
  let fontPercent = 100;
  let brightness = 100;
  let asleep = false;
  let paused = false;
  let homes = false;
  let timerSeq = 1;
  const timers = [];
  let power = { available: true, valid: true, present: true, external: false, charging: false, full: false, asleep: false, percent: 72, millivolts: 3900, sleepSeconds: 0, error: 0 };
  let audio = { available: true, volume: 40, muted: false, state: 'idle', peak: 0, recordedMs: 0, positionMs: 0, error: 0, requestId: 0, completedId: 0 };
  let wifi = { available: true, enabled: false, ready: true, scanning: false, connecting: false, connected: false, ssid: '', ip: '', rssi: 0, reason: 0, error: 0, requestId: 0, completedId: 0, networks: [] };
  const started = Date.now();
  let pageDoc = null;
  let pageDefs = [];
  let navStack = [];
  let currentPageId = 'main';
  let navHandler = null;
  let lifecycleHandler = null;
  let notificationHandler = null;
  let backgroundActive = false;
  let backgroundReason = '';
  let backgroundSince = 0;
  let previewInstance = 0;
  let previewTaskId = 0;
  let taskHandler = null;
  const previewTasks = [];
  let backgroundAllowed = false;
  let notificationsAllowed = false;
  let networkAllowed = false;
  let alarmsAllowed = false;
  let mediaAllowed = false;
  const previewAlarms = new Map();
  let assetEntries = [];
  let assetData = Object.create(null);
  const privateStore = new Map();


  function utf8Length(value) {
    return new TextEncoder().encode(value).length;
  }

  function normalizePalette(raw) {
    if (!raw || typeof raw !== 'object' || Array.isArray(raw) || Object.keys(raw).length !== THEME_TOKENS.length ||
        THEME_TOKENS.some((token) => !Object.prototype.hasOwnProperty.call(raw, token) ||
          typeof raw[token] !== 'string' || !/^#[0-9a-fA-F]{6}$/.test(raw[token]))) return null;
    return Object.fromEntries(THEME_TOKENS.map((token) => [token === 'secondary' ? 'textSecondary' : token, raw[token].toLowerCase()]));
  }

  function applyThemeColors(message) {
    if (!message || typeof message !== 'object' || Array.isArray(message) ||
        Object.keys(message).sort().join(',') !== 'api,dark,light,tokens' || message.api !== 1 ||
        !Array.isArray(message.tokens) || message.tokens.length !== THEME_TOKENS.length ||
        message.tokens.some((token, index) => token !== THEME_TOKENS[index])) return false;
    const light = normalizePalette(message.light);
    const dark = normalizePalette(message.dark);
    if (!light || !dark) return false;
    LIGHT = light;
    DARK = dark;
    paint();
    return true;
  }

  function colors() {
    if (!LIGHT || !DARK) {
      throw new Error('主题色未加载（缺少 theme-colors.json）');
    }
    return mode === 'dark' ? DARK : LIGHT;
  }

  function resolveColor(value) {
    if (typeof value !== 'string') return value;
    const palette = colors();
    const map = {
      $background: palette.background,
      $desktop: palette.desktop,
      $surface: palette.surface,
      $text: palette.text,
      $secondary: palette.textSecondary,
      $accent: palette.accent,
      $button: palette.button,
      $pressed: palette.pressed,
      $border: palette.border,
      $disabled: palette.disabled,
      $error: palette.error,
      $icon: palette.icon,
    };
    return map[value] || value;
  }

  function applyNodeStyle(el, caption, style, item) {
    if (!el) return;
    style = style || {};
    if (Object.prototype.hasOwnProperty.call(style, 'backgroundColor')) {
      if (style.backgroundColor == null) el.style.backgroundColor = '';
      else {
        const bg = resolveColor(style.backgroundColor);
        el.style.backgroundColor = bg;
        if (style.backgroundColor === '$accent' && style.borderWidth === undefined) {
          el.style.borderColor = bg;
        }
      }
    }
    if (Object.prototype.hasOwnProperty.call(style, 'textColor')) {
      if (style.textColor == null) {
        el.style.color = '';
        if (caption) caption.style.color = '';
      } else {
        const color = resolveColor(style.textColor);
        el.style.color = color;
        if (caption) caption.style.color = color;
      }
    }
    if (Object.prototype.hasOwnProperty.call(style, 'radius')) {
      el.style.borderRadius = typeof style.radius === 'number' ? style.radius + 'px' : '';
    }
    if (style.textAlign) {
      el.style.textAlign = style.textAlign;
      if (caption) caption.style.textAlign = style.textAlign;
    }
    if (Object.prototype.hasOwnProperty.call(style, 'borderWidth') || Object.prototype.hasOwnProperty.call(style, 'borderColor')) {
      const width = style.borderWidth;
      if (width == null && !Object.prototype.hasOwnProperty.call(style, 'borderWidth')) {
        /* keep */
      } else if (typeof width === 'number' && width > 0) {
        el.style.borderStyle = 'solid';
        el.style.borderWidth = width + 'px';
        el.style.borderColor = resolveColor(style.borderColor || '$border');
        el.style.boxSizing = 'border-box';
      } else if (width === 0 || width == null) {
        el.style.borderStyle = '';
        el.style.borderWidth = '';
        el.style.borderColor = '';
      }
    }
    if (Object.prototype.hasOwnProperty.call(style, 'opacity')) {
      if (style.opacity == null) el.style.opacity = '';
      else {
        const op = Number(style.opacity);
        el.style.opacity = String(op > 1 ? Math.min(1, op / 255) : op);
      }
    }
    if (Object.prototype.hasOwnProperty.call(style, 'flexGrow') && typeof style.flexGrow === 'number') {
      el.style.flexGrow = String(style.flexGrow);
    }
    if (style.accentColor && (el.classList.contains('slider') || el.classList.contains('progressBar') || el.classList.contains('switch') || el.classList.contains('checkbox') || el.classList.contains('arc'))) {
      el.style.accentColor = resolveColor(style.accentColor);
      if (item && item.fill) item.fill.style.backgroundColor = resolveColor(style.accentColor);
    }
    const hidden = style.hidden === true || (item && item.forcedHidden);
    if (item && (item.type === 'dialog' || item.type === 'bottomSheet')) {
      /* modal visibility owned by show/hide */
    } else if (Object.prototype.hasOwnProperty.call(style, 'hidden') || (item && item.forcedHidden)) {
      el.hidden = Boolean(hidden);
    }
    const disabled = style.disabled === true || (item && item.forcedDisabled);
    if (Object.prototype.hasOwnProperty.call(style, 'disabled') || (item && 'forcedDisabled' in item)) {
      el.classList.toggle('is-disabled', Boolean(disabled));
      const controls = [];
      if (el.matches && el.matches('input,button,select,textarea')) controls.push(el);
      el.querySelectorAll('input,button,select,textarea').forEach((node) => controls.push(node));
      for (const control of controls) control.disabled = Boolean(disabled);
    }
  }

  function refreshItemStyle(item) {
    if (!item) return;
    const base = Object.assign({}, item.style || {});
    const states = base.states && typeof base.states === 'object' ? base.states : null;
    delete base.states;
    let merged = base;
    if (states) {
      if ((item.forcedDisabled || base.disabled) && states.disabled && typeof states.disabled === 'object') {
        merged = Object.assign({}, merged, states.disabled);
      }
      const checked = item.type === 'switch' ? item.el.checked
        : item.type === 'checkbox' ? Boolean(item.input && item.input.checked) : false;
      if (checked && states.checked && typeof states.checked === 'object') {
        merged = Object.assign({}, merged, states.checked);
      }
    }
    applyNodeStyle(item.el, item.caption, merged, item);
  }

  function paint() {
    const palette = colors();
    phone.style.setProperty('--bg', palette.background);
    phone.style.setProperty('--surface', palette.surface);
    phone.style.setProperty('--text', palette.text);
    phone.style.setProperty('--muted', palette.textSecondary);
    phone.style.setProperty('--accent', palette.accent);
    phone.style.setProperty('--button', palette.button);
    phone.style.setProperty('--pressed', palette.pressed);
    phone.style.setProperty('--border', palette.border);
    phone.style.setProperty('--error', palette.error);
    for (const item of widgets.values()) refreshItemStyle(item);
    if (asleep) screen.style.filter = 'brightness(0.15)';
    else if (brightness < 100) screen.style.filter = 'brightness(' + Math.max(0.35, brightness / 100) + ')';
    else screen.style.filter = '';
    themeButton.value = mode;
    if (localeButton.value !== locale) localeButton.value = locale;
  }

  function localeLabel(code) {
    return LOCALE_NAMES[code] || code;
  }

  function validLocale(code) {
    return typeof code === 'string' && /^[A-Za-z]{2,3}([_-][A-Za-z0-9]{2,8})*$/.test(code);
  }

  function refreshLocales() {
    const codes = [];
    const seen = new Set();
    function add(code) {
      if (!validLocale(code) || seen.has(code)) return;
      seen.add(code);
      codes.push(code);
    }
    for (const code of SYSTEM_LOCALES) add(code);
    for (const code of Object.keys(translations)) add(code);
    add(locale);
    localeButton.replaceChildren();
    for (const code of codes) {
      const option = document.createElement('option');
      option.value = code;
      option.textContent = localeLabel(code);
      localeButton.append(option);
    }
    localeButton.value = locale;
  }

  function translate(key) {
    const table = translations[locale] || {};
    return Object.prototype.hasOwnProperty.call(table, key) ? table[key] : key;
  }

  function scaled(sp) {
    return (Number(sp) * fontPercent / 100) + 'px';
  }

  function applyFonts() {
    for (const item of widgets.values()) {
      if (item.sp) {
        const target = item.caption || item.el;
        target.style.fontSize = scaled(item.sp);
      }
    }
  }

  function applyBoundText() {
    for (const item of widgets.values()) {
      if (!item.textKey || unbound.has(item.id)) continue;
      const target = item.caption || item.el;
      target.textContent = translate(item.textKey);
    }
  }

  function notify(name, value) {
    const handler = listeners[name];
    if (typeof handler !== 'function' || paused) return;
    try { handler(value); } catch (error) { showFault(error); }
  }

  function themeSnapshot() {
    const palette = colors();
    return {
      mode: mode,
      colors: {
        background: palette.background,
        desktop: palette.desktop,
        surface: palette.surface,
        text: palette.text,
        textSecondary: palette.textSecondary,
        accent: palette.accent,
        button: palette.button,
        pressed: palette.pressed,
        icon: palette.icon,
        border: palette.border,
        disabled: palette.disabled,
        error: palette.error,
      },
    };
  }

  function displaySnapshot() {
    return {
      widthPx: 320, heightPx: 480, widthDp: 320, heightDp: 480,
      contentWidthDp: 320, contentHeightDp: 416, densityDpi: 160, density: 1,
      fontPercent: fontPercent, fontScale: fontPercent / 100, brightness: brightness,
    };
  }

  function showFault(error) {
    fault.hidden = false;
    fault.textContent = error instanceof Error ? error.message : String(error);
  }

  function clearFault() {
    fault.hidden = true;
    fault.textContent = '';
  }

  function box(el, node) {
    if (node.width === 'fill') el.classList.add('fill-w');
    else if (typeof node.width === 'number') el.style.width = node.width + 'px';
    if (node.height === 'fill') el.classList.add('fill-h');
    else if (node.height === 'content') el.classList.add('content-h');
    else if (typeof node.height === 'number') el.style.height = node.height + 'px';
    if (typeof node.fontSize === 'number') el.dataset.sp = String(node.fontSize);
  }

  const KNOWN_TYPES = new Set([
    'container', 'card', 'list', 'listItem', 'toolbar', 'divider',
    'label', 'image', 'button', 'slider', 'textInput', 'switch', 'checkbox', 'dropdown',
    'progressBar', 'spinner', 'arc', 'dialog', 'bottomSheet',
  ]);

  function isGroup(type) {
    return type === 'container' || type === 'card' || type === 'list' || type === 'listItem'
      || type === 'toolbar' || type === 'dialog' || type === 'bottomSheet';
  }

  function isModal(type) {
    return type === 'dialog' || type === 'bottomSheet';
  }

  function setProgressVisual(item, value) {
    const min = item.min;
    const max = item.max;
    const number = Number(value);
    item.value = number;
    const pct = max === min ? 0 : Math.max(0, Math.min(100, ((number - min) / (max - min)) * 100));
    if (item.fill) item.fill.style.width = pct + '%';
    if (item.type === 'arc' && item.el) {
      item.el.style.setProperty('--pct', String(pct));
      if (item.caption && item.showValue !== false) item.caption.textContent = String(Math.round(number));
    }
  }

  function hideModal(id, reason) {
    const item = widgets.get(id);
    if (!item || !isModal(item.type) || item.el.hidden) return;
    item.el.hidden = true;
    item.visible = false;
    if (reason) dispatch(id, 'dismiss', reason);
  }

  function showModal(id) {
    const item = widget(id);
    if (!isModal(item.type)) {
      item.el.hidden = false;
      item.visible = true;
      if (item.style) item.style.hidden = false;
      return;
    }
    for (const other of widgets.values()) {
      if (isModal(other.type) && other.id !== id) {
        other.el.hidden = true;
        other.visible = false;
      }
    }
    item.el.hidden = false;
    item.visible = true;
  }

  function renderNode(node, parent) {
    if (!node || typeof node !== 'object') return;
    let el;
    let caption = null;
    let subtitle = null;
    let childParent = null;
    let mountParent = parent;
    const type = node.type || 'label';
    if (!KNOWN_TYPES.has(type)) {
      el = document.createElement('div');
      el.className = 'w unknown';
      el.textContent = '未知控件 ' + type;
    } else if (type === 'button') {
      el = document.createElement('div');
      el.className = 'w button';
      caption = document.createElement('span');
      el.append(caption);
      el.addEventListener('click', () => dispatch(node.id, 'click'));
      const bw = typeof node.width === 'number' ? node.width : 0;
      const bh = typeof node.height === 'number' ? node.height : 0;
      const radius = node.style && typeof node.style.radius === 'number' ? node.style.radius : 0;
      if (bw > 0 && bw === bh && radius >= bw / 2 - 0.5) el.classList.add('is-circle');
    } else if (type === 'slider') {
      el = document.createElement('input');
      el.type = 'range';
      el.className = 'w slider';
      el.min = String(node.min ?? 0);
      el.max = String(node.max ?? 100);
      el.value = String(node.value ?? 0);
      el.addEventListener('input', () => dispatch(node.id, 'change', Number(el.value)));
    } else if (type === 'switch') {
      el = document.createElement('input');
      el.type = 'checkbox';
      el.className = 'w switch';
      el.checked = Boolean(node.value);
      el.addEventListener('change', () => dispatch(node.id, 'change', el.checked));
    } else if (type === 'checkbox') {
      el = document.createElement('label');
      el.className = 'w checkbox';
      const input = document.createElement('input');
      input.type = 'checkbox';
      input.checked = Boolean(node.value);
      caption = document.createElement('span');
      caption.className = 'checkbox-label';
      el.append(input, caption);
      input.addEventListener('change', () => dispatch(node.id, 'change', input.checked));
    } else if (type === 'dropdown') {
      el = document.createElement('select');
      el.className = 'w dropdown';
      const options = Array.isArray(node.options) ? node.options : [];
      for (let i = 0; i < options.length; i++) {
        const opt = options[i];
        if (opt && typeof opt === 'object') {
          el.append(new Option(String(opt.label != null ? opt.label : opt.value != null ? opt.value : i), String(i)));
        } else {
          el.append(new Option(String(opt), String(i)));
        }
      }
      const selected = Number(node.value);
      el.value = String(Number.isFinite(selected) ? selected : 0);
      el.addEventListener('change', () => dispatch(node.id, 'change', Number(el.value)));
    } else if (type === 'textInput') {
      el = document.createElement('input');
      const inputType = node.inputType || 'text';
      el.type = inputType === 'password' ? 'password' : inputType === 'number' ? 'number' : 'text';
      el.className = 'w textInput';
      el.placeholder = node.placeholder || '';
      el.value = node.value != null ? String(node.value) : (node.text != null ? String(node.text) : '');
      el.addEventListener('input', () => dispatch(node.id, 'change', el.value));
      el.addEventListener('focus', () => dispatch(node.id, 'focus'));
      el.addEventListener('blur', () => dispatch(node.id, 'blur'));
      el.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') dispatch(node.id, 'submit', el.value);
      });
    } else if (type === 'progressBar') {
      el = document.createElement('div');
      el.className = 'w progressBar';
      const fill = document.createElement('div');
      fill.className = 'progressBar-fill';
      el.append(fill);
    } else if (type === 'spinner') {
      el = document.createElement('div');
      el.className = 'w spinner';
    } else if (type === 'arc') {
      el = document.createElement('div');
      el.className = 'w arc';
      caption = document.createElement('span');
      caption.className = 'arc-label';
      el.append(caption);
    } else if (type === 'image') {
      el = document.createElement('div');
      el.className = 'w image';
      const img = document.createElement('img');
      img.alt = node.id || 'image';
      img.hidden = true;
      el.append(img);
    } else if (type === 'divider') {
      el = document.createElement('div');
      el.className = 'w divider';
    } else if (type === 'listItem') {
      el = document.createElement('div');
      el.className = 'w listItem';
      const textCol = document.createElement('div');
      textCol.className = 'listItem-text';
      caption = document.createElement('div');
      caption.className = 'listItem-title';
      subtitle = document.createElement('div');
      subtitle.className = 'listItem-subtitle';
      textCol.append(caption, subtitle);
      const trailing = document.createElement('div');
      trailing.className = 'listItem-trailing';
      el.append(textCol, trailing);
      childParent = trailing;
      el.addEventListener('click', (event) => {
        if (event.target.closest('input,select,textarea,button')) return;
        dispatch(node.id, 'click');
      });
    } else if (isModal(type)) {
      el = document.createElement('div');
      el.className = 'w modal-root ' + type;
      el.hidden = true;
      const backdrop = document.createElement('div');
      backdrop.className = 'modal-backdrop';
      backdrop.addEventListener('click', () => hideModal(node.id, 'backdrop'));
      const panel = document.createElement('div');
      panel.className = 'modal-panel';
      caption = document.createElement('div');
      caption.className = type + '-title';
      panel.append(caption);
      el.append(backdrop, panel);
      childParent = panel;
      mountParent = screen;
    } else {
      el = document.createElement('div');
      el.className = 'w ' + type;
      if (type === 'toolbar' || type === 'card' || type === 'label') {
        caption = document.createElement(type === 'label' ? 'span' : 'div');
        caption.className = type === 'label' ? '' : type + '-title';
        el.append(caption);
      }
    }
    el.dataset.id = node.id;
    box(el, node);
    if (isGroup(type)) {
      if (type === 'listItem') {
        el.classList.add('row');
      } else if (isModal(type)) {
        const panel = childParent;
        panel.classList.add(node.layout === 'row' ? 'row' : 'column');
        if (node.padding !== undefined) panel.style.padding = (node.padding || 0) + 'px';
        if (node.gap !== undefined) panel.style.gap = (node.gap || 0) + 'px';
      } else {
        el.classList.add(node.layout === 'row' ? 'row' : 'column');
        if (node.scroll || type === 'list') el.classList.add('scroll');
        if (node.padding !== undefined) el.style.padding = (node.padding || 0) + 'px';
        if (node.gap !== undefined) el.style.gap = (node.gap || 0) + 'px';
      }
    }
    if (node.style && typeof node.style.flexGrow === 'number') el.style.flexGrow = String(node.style.flexGrow);
    const item = {
      id: node.id, el: el, caption: caption, subtitle: subtitle, textKey: node.textKey || '',
      subtitleKey: node.subtitleKey || '', sp: node.fontSize || 0,
      handlers: Object.create(null), type: type, style: node.style ? Object.assign({}, node.style) : {},
      min: Number(node.min ?? 0), max: Number(node.max ?? 100), value: Number(node.value ?? 0),
      showValue: node.showValue, visible: !isModal(type), forcedHidden: false, forcedDisabled: false,
      input: null, fill: null, img: null, assetId: node.src || node.asset || '',
    };
    if (type === 'checkbox') item.input = el.querySelector('input');
    if (type === 'progressBar') item.fill = el.querySelector('.progressBar-fill');
    if (type === 'image') {
      item.img = el.querySelector('img');
      const placeholder = document.createElement('span');
      placeholder.className = 'image-placeholder';
      placeholder.textContent = item.assetId || 'image';
      el.append(placeholder);
      item.placeholder = placeholder;
    }
    if (type === 'switch' || type === 'checkbox') {
      const input = item.input || el;
      input.addEventListener('change', () => refreshItemStyle(item));
    }
    if (type === 'progressBar' || type === 'arc') setProgressVisual(item, item.value);
    widgets.set(node.id, item);
    if (item.sp) {
      const size = scaled(item.sp);
      el.style.fontSize = size;
      if (caption) caption.style.fontSize = size;
    }
    if (node.textKey && caption) caption.textContent = translate(node.textKey);
    else if (node.text && caption) caption.textContent = node.text;
    else if (node.text && type === 'label') el.textContent = node.text;
    if (subtitle) {
      if (node.subtitleKey) subtitle.textContent = translate(node.subtitleKey);
      else subtitle.textContent = node.subtitle || '';
      if (!subtitle.textContent) subtitle.hidden = true;
    }
    refreshItemStyle(item);
    mountParent.append(el);
    if (isGroup(type) && Array.isArray(node.children)) {
      const kidsHost = childParent || el;
      for (const child of node.children) renderNode(child, kidsHost);
    }
  }

  function widget(id) {
    const item = widgets.get(id);
    if (!item) throw new Error('未知控件 ' + id);
    return item;
  }

  function dispatch(id, event, value) {
    if (paused || homes) return;
    const item = widgets.get(id);
    if (!item || !item.handlers) return;
    const handler = item.handlers[event];
    if (typeof handler !== 'function') return;
    try { handler(value); } catch (error) { showFault(error); }
  }

  function stopNative(job) {
    if (job.native) {
      if (job.interval) clearInterval(job.native);
      else clearTimeout(job.native);
      job.native = 0;
    }
  }

  function arm(job) {
    stopNative(job);
    if (job.interval) {
      job.native = setInterval(() => {
        if (paused) return;
        try { job.fn(); } catch (error) { showFault(error); }
      }, job.period);
    } else {
      job.native = setTimeout(() => {
        const index = timers.indexOf(job);
        if (index >= 0) timers.splice(index, 1);
        if (paused) return;
        try { job.fn(); } catch (error) { showFault(error); }
      }, job.period);
    }
  }

  function pauseTimers() {
    for (const job of timers) stopNative(job);
  }

  function resumeTimers() {
    for (const job of timers) arm(job);
  }

  function requireInterval(ms) {
    if (typeof ms !== 'number' || ms < 100 || ms > 60000) throw new Error('定时器间隔须为 100～60000 ms');
    if (timers.length >= 8) throw new Error('每个应用最多 8 个定时器');
  }

  function assetUrl(id) {
    if (!id) return '';
    if (/^(data:|https?:|blob:)/i.test(id)) return id;
    const hit = assetData[id];
    return hit || '';
  }

  function findPageDef(pageId) {
    return pageDefs.find((page) => page && page.id === pageId) || null;
  }

  function mountPage(pageId, action, previous) {
    const def = findPageDef(pageId);
    if (!def || !def.root) throw new Error('未知页面 ' + pageId);
    widgets.clear();
    unbound.clear();
    screen.replaceChildren();
    currentPageId = pageId;
    renderNode(def.root, screen);
    paint();
    if (typeof navHandler === 'function') {
      try {
        navHandler({
          page: currentPageId,
          previous: previous || '',
          action: action || 'open',
          depth: navStack.length,
          canGoBack: navStack.length > 1,
        });
      } catch (error) { showFault(error); }
    }
  }

  function navigationGet() {
    return {
      page: currentPageId,
      depth: Math.max(1, navStack.length),
      canGoBack: navStack.length > 1,
      pending: false,
    };
  }

  const Ouse = {
    apiVersion: 1,
    ui: {
      setText(id, text) {
        if (typeof text !== 'string' || utf8Length(text) > 256) throw new Error('文字须为不超过 256 字节的字符串');
        const item = widget(id);
        const ok = item.type === 'label' || item.type === 'button' || item.type === 'listItem'
          || item.type === 'toolbar' || item.type === 'card' || item.type === 'checkbox'
          || item.type === 'dialog' || item.type === 'bottomSheet';
        if (!ok) throw new Error('setText 只能用于文字、按钮、列表项或工具栏');
        unbound.add(id);
        if (item.type === 'label' && !item.caption) item.el.textContent = text;
        else (item.caption || item.el).textContent = text;
      },
      getText(id) {
        const item = widget(id);
        if (item.type === 'textInput') return String(item.el.value || '');
        if (item.caption) return String(item.caption.textContent || '');
        return String(item.el.textContent || '');
      },
      on(id, event, fn) {
        const item = widget(id);
        const names = ['click', 'change', 'focus', 'blur', 'submit', 'dismiss'];
        if (!names.includes(event) || (fn !== null && typeof fn !== 'function')) {
          throw new Error('on(id, event, function|null): invalid binding');
        }
        item.handlers[event] = fn;
      },
      getValue(id) {
        const item = widget(id);
        if (item.type === 'slider' || item.type === 'dropdown') return Number(item.el.value);
        if (item.type === 'switch') return Boolean(item.el.checked);
        if (item.type === 'checkbox') return Boolean((item.input || item.el).checked);
        if (item.type === 'textInput') return String(item.el.value || '');
        if (item.type === 'progressBar' || item.type === 'arc') return Number(item.value);
        throw new Error('getValue 需要滑块、下拉、开关、输入框、进度或圆弧');
      },
      setValue(id, value) {
        const item = widget(id);
        if (item.type === 'slider') {
          const number = Number(value);
          if (number < Number(item.el.min) || number > Number(item.el.max)) throw new Error('滑块数值超出范围');
          item.el.value = String(number);
          return;
        }
        if (item.type === 'dropdown') {
          item.el.value = String(Number(value));
          return;
        }
        if (item.type === 'switch') {
          item.el.checked = Boolean(value);
          refreshItemStyle(item);
          return;
        }
        if (item.type === 'checkbox') {
          (item.input || item.el).checked = Boolean(value);
          refreshItemStyle(item);
          return;
        }
        if (item.type === 'textInput') {
          item.el.value = value == null ? '' : String(value);
          return;
        }
        if (item.type === 'progressBar' || item.type === 'arc') {
          const number = Number(value);
          if (number < item.min || number > item.max) throw new Error('数值超出范围');
          setProgressVisual(item, number);
          return;
        }
        throw new Error('setValue 需要滑块、下拉、开关、输入框、进度或圆弧');
      },
      setImage(id, assetId) {
        const item = widget(id);
        if (item.type !== 'image') throw new Error('setImage 需要 image 控件');
        item.assetId = assetId == null ? '' : String(assetId);
        if (item.img) {
          const url = assetUrl(item.assetId);
          if (url) {
            item.img.src = url;
            item.img.hidden = false;
            if (item.placeholder) item.placeholder.hidden = true;
          } else {
            item.img.hidden = true;
            if (item.placeholder) {
              item.placeholder.hidden = false;
              item.placeholder.textContent = item.assetId || 'image';
            }
          }
        }
      },
      setStyle(id, patch) {
        const item = widget(id);
        if (!patch || typeof patch !== 'object' || Array.isArray(patch)) throw new Error('setStyle 需要样式对象');
        item.style = Object.assign({}, item.style || {}, patch);
        if (patch.states && typeof patch.states === 'object') {
          item.style.states = Object.assign({}, (item.style.states || {}), patch.states);
          for (const key of Object.keys(patch.states)) {
            if (patch.states[key] == null && item.style.states) delete item.style.states[key];
          }
        }
        refreshItemStyle(item);
      },
      show(id) { showModal(id); },
      hide(id) {
        const item = widget(id);
        if (isModal(item.type)) {
          item.el.hidden = true;
          item.visible = false;
          return;
        }
        item.forcedHidden = true;
        item.style = Object.assign({}, item.style, { hidden: true });
        refreshItemStyle(item);
      },
      isVisible(id) {
        const item = widget(id);
        if (isModal(item.type)) return !item.el.hidden && item.visible !== false;
        if (item.el.hidden || item.forcedHidden || (item.style && item.style.hidden)) return false;
        let node = item.el.parentElement;
        while (node) {
          if (node.hidden) return false;
          node = node.parentElement;
        }
        return true;
      },
      setEnabled(id, enabled) {
        const item = widget(id);
        item.forcedDisabled = !enabled;
        item.style = Object.assign({}, item.style, { disabled: !enabled });
        refreshItemStyle(item);
      },
      focus(id) {
        const item = widget(id);
        const target = item.input || (item.type === 'textInput' || item.type === 'dropdown' || item.type === 'slider' ? item.el : null);
        if (target && target.focus) target.focus();
      },
      blur(id) {
        const item = widget(id);
        const target = item.input || item.el;
        if (target && target.blur) target.blur();
      },
    },
    theme: {
      get: themeSnapshot,
      set(next) {
        if (next !== 'light' && next !== 'dark') throw new Error('主题只能是 light 或 dark');
        if (mode !== next) {
          mode = next;
          paint();
          notify('theme', themeSnapshot());
        }
        return true;
      },
      onChange(fn) { listeners.theme = fn || null; },
    },
    locale: {
      get() { return locale; },
      set(next) {
        if (!validLocale(next)) throw new Error('语言代码无效');
        if (locale !== next) {
          locale = next;
          refreshLocales();
          applyBoundText();
          paint();
          notify('locale', locale);
        }
        return true;
      },
      t: translate,
      onChange(fn) { listeners.locale = fn || null; },
    },
    display: {
      get: displaySnapshot,
      setFontPercent(percent) {
        if (percent !== 100 && percent !== 115 && percent !== 130) throw new Error('字号只能是 100、115 或 130');
        if (fontPercent !== percent) {
          fontPercent = percent;
          applyFonts();
          notify('display', displaySnapshot());
        }
        return true;
      },
      setBrightness(percent, _save) {
        if (typeof percent !== 'number' || percent < 10 || percent > 100) throw new Error('亮度须为 10～100');
        brightness = percent;
        paint();
        notify('display', displaySnapshot());
        return true;
      },
      onChange(fn) { listeners.display = fn || null; },
    },
    system: {
      info() {
        return {
          internalFree: 180000, internalTotal: 320000, psramFree: 24000000, psramTotal: 32000000,
          flashBytes: 16 * 1024 * 1024, cpuMHz: 400, uptimeMs: Date.now() - started,
          resetReason: 1, coreVersion: '0.10.0', idfVersion: 'preview',
        };
      },
      storage() {
        return {
          ready: true, revision: 1,
          totalBytes: 4 * 1024 * 1024, usedBytes: 900 * 1024, freeBytes: 3.1 * 1024 * 1024,
          packageBytes: 120 * 1024, privateBytes: 8 * 1024, packageCount: 2, privateApps: 1,
        };
      },
    },
    navigation: {
      push(pageId) {
        if (!findPageDef(pageId)) throw new Error('Unknown page id');
        if (pageId === currentPageId) return false;
        if (navStack.length >= 8) return false;
        const previous = currentPageId;
        navStack.push(pageId);
        mountPage(pageId, 'push', previous);
        return true;
      },
      back() {
        if (navStack.length <= 1) return false;
        const previous = currentPageId;
        navStack.pop();
        const pageId = navStack[navStack.length - 1];
        mountPage(pageId, 'back', previous);
        return true;
      },
      replace(pageId) {
        if (!findPageDef(pageId)) throw new Error('Unknown page id');
        if (pageId === currentPageId) return false;
        const previous = currentPageId;
        navStack[navStack.length - 1] = pageId;
        mountPage(pageId, 'replace', previous);
        return true;
      },
      get: navigationGet,
      onChange(fn) {
        if (fn !== null && typeof fn !== 'function') throw new Error('onChange expects function or null');
        navHandler = fn;
      },
    },
    background: {
      begin(reason = 'application') {
        if (typeof reason !== 'string' || !reason || utf8Length(reason) > 63) throw new Error('Invalid background reason');
        if (!backgroundAllowed) return false;
        if (!backgroundActive) backgroundSince = Date.now() - started;
        backgroundReason = reason;
        backgroundActive = true;
        if (homes && paused) { paused = false; resumeTimers(); }
        schedulePreviewDelivery();
        if (typeof lifecycleHandler === 'function') lifecycleHandler(Ouse.lifecycle.get());
        return true;
      },
      end() {
        backgroundActive = false;
        backgroundReason = '';
        backgroundSince = 0;
        if (homes) { paused = true; pauseTimers(); }
        if (typeof lifecycleHandler === 'function') lifecycleHandler(Ouse.lifecycle.get());
        return false;
      },
      isActive() { return backgroundActive; },
      get() { return Ouse.lifecycle.get(); },
    },
    lifecycle: {
      get() {
        const visible = !homes;
        const runnable = visible || backgroundActive;
        return {
          state: visible ? 'foreground' : runnable ? 'background' : 'suspended',
          visible: visible,
          runnable: runnable,
          backgroundAllowed: backgroundAllowed,
          backgroundActive: backgroundActive,
          instanceId: previewInstance, alive: true,
          reason: backgroundActive ? backgroundReason : '',
          sinceMs: backgroundActive ? backgroundSince : 0,
        };
      },
      onChange(fn) {
        if (fn !== null && typeof fn !== 'function') throw new Error('onChange expects function or null');
        lifecycleHandler = fn;
      },
    },
    notifications: {
      publish(request) {
        if (!notificationsAllowed) return false;
        if (!request || typeof request !== 'object' || !request.id) return false;
        const list = privateStore.get('__notifications__') || [];
        const next = {
          id: String(request.id),
          title: String(request.title || ''),
          body: String(request.body || ''),
          payload: request.payload != null ? String(request.payload) : '',
          ongoing: Boolean(request.ongoing),
        };
        const index = list.findIndex((item) => item.id === next.id);
        if (index >= 0) list[index] = next;
        else {
          if (list.length >= 8) return false;
          list.push(next);
        }
        privateStore.set('__notifications__', list);
        try { if (log) log.textContent = ((log.textContent ? log.textContent + '\n' : '') + '[notice] ' + next.id).split('\n').slice(-8).join('\n'); } catch (_) {}
        return true;
      },
      update(request) { return Ouse.notifications.publish(request); },
      cancel(id) {
        if (!notificationsAllowed) return false;
        const list = privateStore.get('__notifications__') || [];
        const next = list.filter((item) => item.id !== String(id));
        privateStore.set('__notifications__', next);
        return next.length !== list.length;
      },
      list() {
        if (!notificationsAllowed) return [];
        return (privateStore.get('__notifications__') || []).map((item) => Object.assign({}, item));
      },
      onOpen(fn) {
        if (fn !== null && typeof fn !== 'function') throw new Error('onOpen expects function or null');
        notificationHandler = fn;
      },
    },
    storage: {
      get(key) { return Promise.resolve(privateStore.has(key) ? privateStore.get(key) : null); },
      set(key, value) { privateStore.set(key, value); return Promise.resolve(true); },
      remove(key) { privateStore.delete(key); return Promise.resolve(true); },
      keys() { return Promise.resolve([...privateStore.keys()].filter((key) => !key.startsWith('__'))); },
      clear() {
        for (const key of [...privateStore.keys()]) {
          if (!key.startsWith('__')) privateStore.delete(key);
        }
        return Promise.resolve(true);
      },
      info() {
        return Promise.resolve({
          ready: true, keyCount: [...privateStore.keys()].filter((key) => !key.startsWith('__')).length,
          usedBytes: 1024, quotaBytes: 64 * 1024,
        });
      },
    },
    assets: {
      list() { return assetEntries.slice(); },
      readText(id) {
        const value = assetData[id];
        if (typeof value === 'string' && !value.startsWith('data:')) return value;
        if (typeof value === 'string' && value.startsWith('data:text')) {
          try { return decodeURIComponent(value.split(',')[1] || ''); } catch (_) { return ''; }
        }
        return '';
      },
      readBytes(id) {
        const value = assetData[id];
        if (!value) return new Uint8Array();
        return new Uint8Array();
      },
    },
    timers: {
      setInterval(fn, ms) {
        requireInterval(ms);
        if (typeof fn !== 'function') throw new Error('定时器需要函数');
        const job = { id: timerSeq++, fn: fn, period: ms, interval: true, native: 0 };
        timers.push(job);
        if (!paused) arm(job);
        return job.id;
      },
      setTimeout(fn, ms) {
        requireInterval(ms);
        if (typeof fn !== 'function') throw new Error('定时器需要函数');
        const job = { id: timerSeq++, fn: fn, period: ms, interval: false, native: 0 };
        timers.push(job);
        if (!paused) arm(job);
        return job.id;
      },
      clear(id) {
        const index = timers.findIndex((job) => job.id === id);
        if (index < 0) return;
        stopNative(timers[index]);
        timers.splice(index, 1);
      },
    },
    home() {
      if (homes) return;
      homes = true;
      paused = !backgroundActive;
      if (paused) pauseTimers();
      const app = globalThis.app;
      if (app && typeof app.onPause === 'function') {
        try { app.onPause(); } catch (error) { showFault(error); }
      }
      home.hidden = false;
    },
    power: {
      get() { return Object.assign({}, power, { asleep: asleep }); },
      setTimeout(seconds) {
        if (![0, 15, 30, 60, 120, 300].includes(seconds)) throw new Error('熄屏时间无效');
        power.sleepSeconds = seconds;
        notify('power', Ouse.power.get());
        return true;
      },
      sleep() { asleep = true; power.asleep = true; paint(); notify('power', Ouse.power.get()); return true; },
      wake() { asleep = false; power.asleep = false; paint(); notify('power', Ouse.power.get()); return true; },
      onChange(fn) { listeners.power = fn || null; },
    },
    audio: {
      get() { return Object.assign({}, audio); },
      setVolume(value) {
        if (typeof value !== 'number' || value < 0 || value > 100) throw new Error('音量须为 0～100');
        audio.volume = value;
        notify('audio', Ouse.audio.get());
        return true;
      },
      setMuted(muted) { audio.muted = Boolean(muted); notify('audio', Ouse.audio.get()); return true; },
      testTone() { audio.state = 'idle'; audio.requestId += 1; audio.completedId = audio.requestId; notify('audio', Ouse.audio.get()); return true; },
      record(seconds) {
        if (seconds < 1 || seconds > 3) throw new Error('录音时长须为 1～3 秒');
        audio.recordedMs = seconds * 1000;
        audio.state = 'idle';
        audio.requestId += 1;
        audio.completedId = audio.requestId;
        notify('audio', Ouse.audio.get());
        return true;
      },
      playRecording() {
        if (!audio.recordedMs) return false;
        audio.state = 'idle';
        audio.requestId += 1;
        audio.completedId = audio.requestId;
        notify('audio', Ouse.audio.get());
        return true;
      },
      stop() { audio.state = 'idle'; notify('audio', Ouse.audio.get()); return true; },
      onChange(fn) { listeners.audio = fn || null; },
    },
    wifi: {
      get() { return Object.assign({}, wifi, { networks: wifi.networks.map((item) => Object.assign({}, item)) }); },
      setEnabled(enabled) {
        wifi.enabled = Boolean(enabled);
        if (!wifi.enabled) { wifi.connected = false; wifi.ssid = ''; wifi.ip = ''; }
        notify('wifi', Ouse.wifi.get());
        return true;
      },
      scan() {
        wifi.scanning = false;
        wifi.networks = [{ ssid: 'Ouse', rssi: -46, channel: 6, secured: true }];
        wifi.requestId += 1;
        wifi.completedId = wifi.requestId;
        notify('wifi', Ouse.wifi.get());
        return true;
      },
      connect(ssid, password) {
        if (typeof ssid !== 'string' || !ssid) throw new Error('需要网络名称');
        wifi.enabled = true;
        wifi.connected = true;
        wifi.ssid = ssid;
        wifi.ip = '192.168.1.20';
        wifi.rssi = -52;
        wifi.requestId += 1;
        wifi.completedId = wifi.requestId;
        notify('wifi', Ouse.wifi.get());
        return password === undefined || typeof password === 'string';
      },
      disconnect() { wifi.connected = false; wifi.ip = ''; notify('wifi', Ouse.wifi.get()); return true; },
      forget() { wifi.connected = false; wifi.ssid = ''; wifi.ip = ''; notify('wifi', Ouse.wifi.get()); return true; },
      onChange(fn) { listeners.wifi = fn || null; },
    },
  };

  // Browser preview only: these records are simulations, never hardware or
  // FreeRTOS measurements. Completion delivery follows the preview JS policy.
  function previewTaskSnapshot(record) { return JSON.parse(JSON.stringify(record.snapshot)); }
  function previewTaskList(afterId = 0, limit = 16) {
    if (!Number.isInteger(afterId) || afterId < 0 || afterId > 0xffffffff || !Number.isInteger(limit) || limit < 1 || limit > 16) throw new Error('Invalid task page');
    return previewTasks.filter(record => record.snapshot.id > afterId).slice(0, limit).map(previewTaskSnapshot);
  }
  function schedulePreviewDelivery() {
    const instance = previewInstance;
    queueMicrotask(() => {
      if (instance !== previewInstance || paused) return;
      for (const record of previewTasks.slice()) {
        if (!record.pending) continue;
        record.pending = false;
        const deliver = record.deliver;
        record.deliver = null;
        if (deliver) deliver();
        if (typeof taskHandler === 'function') {
          try { taskHandler(previewTaskSnapshot(record)); } catch (error) { showFault(error); return; }
        }
        if (paused || instance !== previewInstance) return;
      }
    });
  }
  function newPreviewTask(kind, operation, execute, settle) {
    if (previewTasks.filter(record => record.pending || record.snapshot.state === 'queued').length >= 8) return false;
    if (previewTasks.length >= 96) {
      const index = previewTasks.findIndex(record => !record.pending && record.snapshot.state !== 'queued');
      if (index < 0) return false;
      previewTasks.splice(index, 1);
    }
    if (kind === 'audio') for (const record of previewTasks) {
      if (record.snapshot.kind === 'audio' && record.snapshot.state === 'queued') {
        record.snapshot.cancelRequested = true;record.snapshot.cancelReason = 'superseded';
      }
    }
    const instance = previewInstance, id = ++previewTaskId;
    const record = { pending: false, deliver: null, snapshot: {
      id, instanceId: instance, appId: 'preview.app', revision: id, kind, operation,
      state: 'queued', closePolicy: kind === 'audio' || kind === 'network' ? 'stop' : 'finish',
      cancelRequested: false, cancellable: true, detached: false, cancelReason: '',
      error: 'ESP_OK', errorCode: 0, createdMs: Date.now() - started,
      startedMs: 0, finishedMs: 0, result: null, simulated: true,
    } };
    previewTasks.push(record);
    queueMicrotask(() => {
      if (instance !== previewInstance) return;
      let value, failure;
      const task = record.snapshot;
      if (task.cancelRequested) task.state = 'cancelled';
      else {
        task.state = 'running';task.cancellable = false;task.startedMs = Date.now() - started;
        try { value = execute(id);task.state = 'succeeded'; }
        catch (error) { failure = error;task.state = 'failed';task.error = String(error.message || error);task.errorCode = -1; }
      }
      task.cancellable = false;task.finishedMs = Date.now() - started;
      task.result = { ok: task.state === 'succeeded' };
      if (kind === 'audio') Object.assign(task.result, {recordedMs: audio.recordedMs, positionMs: audio.positionMs});
      if (kind === 'wifi') Object.assign(task.result, {enabled: wifi.enabled, connected: wifi.connected, networks: wifi.networks.length, reason: wifi.reason, ssid: wifi.ssid, ip: wifi.ip});
      if (kind === 'storage' && !failure && task.state === 'succeeded') task.result.jsonBytes = utf8Length(JSON.stringify(value));
      if (kind === 'network' && value) Object.assign(task.result, {status: value.status, bodyBytes: value.bodyBytes});
      if (kind === 'alarm' && value) task.result.alarmId = value;
      record.pending = true;
      if (settle) record.deliver = () => settle(value, failure || (task.state === 'cancelled' ? new Error('Cancelled') : null));
      schedulePreviewDelivery();
    });
    return id;
  }
  Ouse.tasks = {
    list: previewTaskList,
    get(id) { const record = previewTasks.find(item => item.snapshot.id === id);return record ? previewTaskSnapshot(record) : null; },
    cancel(id) {
      const record = previewTasks.find(item => item.snapshot.id === id);
      if (!record) return false;
      if (record.snapshot.state !== 'queued') return record.snapshot.state !== 'running';
      record.snapshot.cancelRequested = true;record.snapshot.cancelReason = 'application';return true;
    },
    onComplete(fn) { if (fn !== null && typeof fn !== 'function') throw new Error('Expected function or null');taskHandler = fn; },
  };
  Ouse.system.tasks = previewTaskList;
  Ouse.system.diagnostics = () => ({
    simulated: true, sampledMs: Date.now() - started, budgetClock: 'preview-simulated',
    instances: 1, contexts: 1, apps: [], jsHeapBytes: 0,
    backgroundJs: homes && backgroundActive ? 1 : 0, runnableJs: paused ? 0 : 1,
    nativeTasksActive: previewTasks.filter(record => ['queued','running','cancelling'].includes(record.snapshot.state)).length,
    taskRecords: previewTasks.length, taskCapacity: 96, taskActiveLimit: 64, taskOwnerLimit: 8,
    taskEventsPending: previewTasks.filter(record => record.pending).length,
    timers: timers.length, foregroundBudgetUs: 12000, backgroundBudgetUs: 4000,
  });
  Ouse.diagnostics = { get() { return Object.assign({simulated: true, appId: 'preview.app'}, Ouse.lifecycle.get()); } };
  Ouse.clock = {get() {return {utcMs: Date.now(), valid: true, syncedThisBoot: false, lastSyncUptimeMs: 0, source: 'preview-simulated'};} };
  Ouse.system.cpu = () => ({method: 'preview-simulated', simulated: true, periodMs: 1000, samplerStackBytesPerCore: 2048,
    cores: [0, 1].map(() => ({available: false, valid: false, busyPercent: null, windowMs: 0, sampledUptimeMs: 0}))});
  Ouse.system.capabilities = () => ({simulated: true, coreVersion: 'development',
    cpuMetrics: false,
    network: {available: true, requestBodyMax: 8192, responseBodyMax: 32768, pendingPerInstance: 2, headersMax: 12, redirects: false},
    alarms: {available: true, persistent: false, perAppMax: 4, totalMax: 24, powerOffWake: false, automaticDst: false},
    media: {available: false, formats: [], outputRateHz: 16000, outputChannels: 1, bufferBytes: 8192, seek: false},
    granted: {network: networkAllowed, alarms: alarmsAllowed, media: mediaAllowed, background: backgroundAllowed, notifications: notificationsAllowed},
  });
  // Native decoding and codec focus need the device. Keep discovery honest;
  // do not claim a successful playback from a browser that produces no audio.
  Ouse.media = {play() {return false;}, pause() {return false;}, resume() {return false;}, stop() {return false;}, get() {return null;} };
  Ouse.net = {request(options) {
    let valid = networkAllowed && options && typeof options === 'object' && typeof options.url === 'string' &&
      utf8Length(options.url) <= 512 && /^https?:\/\//.test(options.url) && !/[\s\x00-\x1f\x7f@#\\]/.test(options.url) &&
      ['GET','HEAD','POST','PUT','PATCH','DELETE'].includes(options.method || 'GET') &&
      (options.body === undefined || (typeof options.body === 'string' && utf8Length(options.body) <= 8192)) &&
      ['text','bytes'].includes(options.responseType || 'text');
    const timeout = options && options.timeoutMs !== undefined ? options.timeoutMs : 10000;
    valid = valid && Number.isInteger(timeout) && timeout >= 1000 && timeout <= 30000;
    if (!valid) return Promise.reject(new Error('Invalid or unauthorized preview request'));
    if (previewTasks.filter(r => r.snapshot.kind === 'network' && (r.pending || r.snapshot.state === 'queued')).length >= 2) return Promise.reject(new Error('Network busy'));
    let settle;
    const promise = new Promise((resolve, reject) => {settle = (value, error) => error ? reject(error) : resolve(value);});
    const id = newPreviewTask('network', options.method || 'GET', () => {
      // Deliberately no fetch(): a preview must not silently send credentials or
      // contact a URL entered for the device. The response declares simulation.
      const text = options.method === 'HEAD' ? '' : '{"simulated":true}';
      return {status: 200, ok: true, bodyBytes: utf8Length(text), headers: [['Content-Type','application/json']],
        body: options.responseType === 'bytes' ? new TextEncoder().encode(text).buffer : text, simulated: true};
    }, settle);
    if (!id) settle(null, new Error('Task capacity reached'));
    else Object.defineProperty(promise, 'taskId', {value: id, enumerable: true});
    return promise;
  }};
  function previewAlarmCommand(action, id, options) {
    if (!alarmsAllowed) return false;
    return newPreviewTask('alarm', action, () => {
      const old = previewAlarms.get(id), now = Date.now();
      if (action === 'set') {
        if (!old && previewAlarms.size >= 4) throw new Error('Alarm limit reached');
        if (!options || options.weekdays) throw new Error('Preview only simulates one-shot alarms');
        if (!Number.isFinite(options.atMs) || options.atMs <= now) throw new Error('Future time required');
        previewAlarms.set(id, {...options, id, nextMs: Math.floor(options.atMs / 1000) * 1000, lastMs: 0, enabled: true, ringing: false, missed: false});
      } else {
        if (!old) throw new Error('Alarm not found');
        if (action === 'remove') previewAlarms.delete(id);
        else if (action === 'dismiss') old.ringing = false;
        else if (action === 'snooze') {old.nextMs = now + options * 60000;old.enabled = true;old.ringing = false;}
      }
      return id;
    });
  }
  Ouse.alarms = {
    set(options) {return previewAlarmCommand('set', options && options.id, options);},
    remove(id) {return previewAlarmCommand('remove', id);},
    dismiss(id) {return previewAlarmCommand('dismiss', id);},
    snooze(id, minutes) {if (!Number.isInteger(minutes) || minutes < 1 || minutes > 60) throw new Error('Minutes: 1..60');return previewAlarmCommand('snooze', id, minutes);},
    list() {
      if (!alarmsAllowed) return [];
      const now = Date.now();
      for (const alarm of previewAlarms.values()) if (alarm.enabled && alarm.nextMs <= now) {
        alarm.lastMs = alarm.nextMs;alarm.enabled = false;alarm.missed = now - alarm.nextMs > 120000;alarm.ringing = !alarm.missed;alarm.nextMs = 0;
      }
      return [...previewAlarms.values()].map(a => ({...a, simulated: true}));
    },
  };
  for (const [kind, methods] of [['audio', ['testTone','record','playRecording','stop']], ['wifi', ['setEnabled','scan','connect','disconnect','forget']]]) {
    for (const operation of methods) {
      const original = Ouse[kind][operation];
      Ouse[kind][operation] = (...args) => {
        if (kind === 'wifi' && previewTasks.some(record => record.snapshot.kind === kind && record.snapshot.state === 'queued')) return false;
        return newPreviewTask(kind, operation, id => {
          const result = original(...args);
          if (!result) throw new Error('Preview operation rejected');
          const state = kind === 'audio' ? audio : wifi;
          state.requestId = id;state.completedId = id;notify(kind, Ouse[kind].get());
        });
      };
    }
  }
  for (const operation of ['get','set','remove','keys','clear','info']) {
    Ouse.storage[operation] = (...args) => {
      let resolve, reject;
      const promise = new Promise((yes, no) => { resolve = yes;reject = no; });
      const id = newPreviewTask('storage', operation, () => {
        // Original preview methods return already resolved Promises. Their
        // actual values are computed below to preserve deferred delivery.
        if (operation === 'get') return privateStore.has(args[0]) ? JSON.parse(JSON.stringify(privateStore.get(args[0]))) : null;
        if (operation === 'set') {privateStore.set(args[0], JSON.parse(JSON.stringify(args[1])));return true;}
        if (operation === 'remove') return privateStore.delete(args[0]);
        if (operation === 'keys') return [...privateStore.keys()].filter(key => !key.startsWith('__'));
        if (operation === 'clear') {const keys=[...privateStore.keys()].filter(key => !key.startsWith('__'));keys.forEach(key => privateStore.delete(key));return keys.length;}
        return {usedBytes: 1024,quotaBytes: 65536,keyCount: [...privateStore.keys()].filter(key => !key.startsWith('__')).length,keyLimit: 32,valueLimit: 8192};
      }, (value, error) => error ? reject(error) : resolve(value));
      if (!id) reject(Object.assign(new Error('Task capacity'), {code: 'BUSY'}));
      else Object.defineProperty(promise, 'taskId', {value: id, enumerable: true});
      return promise;
    };
  }

  const previewConsole = {
    log(text) {
      const line = '[' + (title.textContent || 'app') + '] ' + String(text);
      log.textContent = (log.textContent ? log.textContent + '\n' : '') + line;
      const lines = log.textContent.split('\n');
      log.textContent = lines.slice(-8).join('\n');
    },
  };

  function resetRuntime() {
    previewInstance += 1;
    taskHandler = null;previewTasks.length = 0;
    previewAlarms.clear();
    backgroundReason = '';backgroundSince = 0;
    widgets.clear();
    unbound.clear();
    for (const key of Object.keys(listeners)) listeners[key] = null;
    for (const job of timers) stopNative(job);
    timers.length = 0;
    paused = false;
    homes = false;
    home.hidden = true;
    clearFault();
    screen.replaceChildren();
    log.textContent = '';
  }

  function resume() {
    if (!homes) return;
    homes = false;
    paused = false;
    home.hidden = true;
    resumeTimers();
    schedulePreviewDelivery();
    const app = globalThis.app;
    if (app && typeof app.onResume === 'function') {
      try { app.onResume(); } catch (error) { showFault(error); }
    }
  }

  function load(message) {
    resetRuntime();
    title.textContent = message.title || '预览';
    globalThis.app = undefined;
    pageDoc = null;
    pageDefs = [];
    navStack = [];
    currentPageId = 'main';
    navHandler = null;
    lifecycleHandler = null;
    notificationHandler = null;
    backgroundActive = false;
    backgroundAllowed = Boolean(message.capabilities && message.capabilities.background);
    notificationsAllowed = Boolean(message.capabilities && message.capabilities.notifications);
    networkAllowed = Boolean(message.capabilities && message.capabilities.network);
    alarmsAllowed = Boolean(message.capabilities && message.capabilities.alarms);
    mediaAllowed = Boolean(message.capabilities && message.capabilities.media);
    assetEntries = Array.isArray(message.assets) ? message.assets.slice() : [];
    assetData = message.assetData && typeof message.assetData === 'object' ? Object.assign({}, message.assetData) : Object.create(null);
    privateStore.clear();
    if (message.themeColors && !applyThemeColors(message.themeColors)) {
      showFault(new Error('主题色无效（theme-colors.json）'));
      return;
    }
    if (!LIGHT || !DARK) {
      showFault(new Error('主题色未加载（缺少 theme-colors.json）'));
      return;
    }
    let page;
    try {
      page = JSON.parse(message.page);
    } catch (error) {
      showFault(error);
      return;
    }
    pageDoc = page;
    translations = page.translations && typeof page.translations === 'object' ? page.translations : {};
    refreshLocales();
    if (Array.isArray(page.pages) && page.pages.length) {
      pageDefs = page.pages.map((entry) => ({ id: entry.id, root: entry.root }));
      currentPageId = page.initialPage || pageDefs[0].id;
    } else if (page.root) {
      pageDefs = [{ id: 'main', root: page.root }];
      currentPageId = 'main';
    } else {
      showFault(new Error('page.json 需要 root 或 pages'));
      return;
    }
    if (!findPageDef(currentPageId)) {
      showFault(new Error('initialPage 无效'));
      return;
    }
    navStack = [currentPageId];
    try {
      const def = findPageDef(currentPageId);
      renderNode(def.root, screen);
      paint();
    } catch (error) {
      showFault(error);
      return;
    }
    try {
      const run = new Function('Ouse', 'console', message.script);
      run(Ouse, previewConsole);
      if (typeof navHandler === 'function') {
        navHandler({
          page: currentPageId,
          previous: '',
          action: 'open',
          depth: 1,
          canGoBack: false,
        });
      }
    } catch (error) {
      showFault(error);
    }
  }

  themeButton.addEventListener('change', () => Ouse.theme.set(themeButton.value));
  localeButton.addEventListener('change', () => Ouse.locale.set(localeButton.value));
  restartButton.addEventListener('click', () => vscode.postMessage({ type: 'restart' }));
  resumeButton.addEventListener('click', resume);
  window.addEventListener('unhandledrejection', (event) => {
    showFault(event.reason);
  });
  window.addEventListener('message', (event) => {
    if (!event.data) return;
    if (event.data.type === 'themeColors') {
      const { type, ...themeColors } = event.data;
      if (!applyThemeColors(themeColors)) showFault(new Error('主题色无效（theme-colors.json）'));
      return;
    }
    if (event.data.type === 'app') load(event.data);
    else if (event.data.type === 'fault') showFault(new Error(event.data.message || '重启失败'));
  });
  const stage = document.querySelector('.stage');
  const toolbar = document.querySelector('.toolbar');
  const scaleMode = document.getElementById('scaleMode');
  const saved = vscode.getState() || {};
  let original = saved.scaleMode === 'native' || Boolean(saved.original);
  if (scaleMode) scaleMode.value = original ? 'native' : 'fit';
  function fitScale() {
    if (!stage) return;
    let scale = 1;
    if (!original) {
      const pad = 48;
      const gap = 28;
      const used = (toolbar ? toolbar.offsetHeight : 0)
        + (log && log.offsetHeight ? log.offsetHeight + 14 : 0)
        + gap;
      const availW = Math.max(160, document.documentElement.clientWidth - pad);
      const availH = Math.max(200, document.documentElement.clientHeight - pad - used);
      scale = Math.max(0.5, Math.min(2.5, availW / 320, availH / 416));
    }
    stage.style.setProperty('--scale', String(scale));
  }
  if (scaleMode) {
    scaleMode.addEventListener('change', () => {
      original = scaleMode.value === 'native';
      vscode.setState(Object.assign({}, vscode.getState() || {}, { scaleMode: scaleMode.value }));
      fitScale();
    });
  }
  window.addEventListener('resize', fitScale);
  if (typeof ResizeObserver !== 'undefined') {
    new ResizeObserver(fitScale).observe(document.documentElement);
    if (log) new ResizeObserver(fitScale).observe(log);
  }
  fitScale();

  refreshLocales();
  // The host sends the palette after this handshake. The first paint belongs
  // to applyThemeColors(), once the received colors have been validated.
  vscode.postMessage({ type: 'ready' });
}());
