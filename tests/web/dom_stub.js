'use strict';
/*
 * Minimal DOM, timer and fetch stubs for running components/klink/web/app.js under node.
 * No npm packages: just enough of the browser API for the Program tab, plus an in-memory
 * implementation of the mock server routes used there.
 */
var fs = require('fs');
var path = require('path');

function textNode(text) {
  return { nodeType: 3, textContent: String(text), parentNode: null };
}

function makeContext() {
  var ctx = {
    fillStyle: '', strokeStyle: '', lineWidth: 1, globalAlpha: 1,
    createImageData: function (w, h) {
      return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) };
    },
    putImageData: function () {}, getImageData: function (x, y, w, h) {
      return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) };
    },
    setTransform: function () {}, resetTransform: function () {}, save: function () {}, restore: function () {},
    beginPath: function () {}, closePath: function () {}, moveTo: function () {}, lineTo: function () {},
    arc: function () {}, rect: function () {}, fill: function () {}, stroke: function () {},
    fillRect: function () {}, strokeRect: function () {}, clearRect: function () {},
    translate: function () {}, scale: function () {}, rotate: function () {},
    drawImage: function () {}, measureText: function () { return { width: 0 }; },
    fillText: function () {}, strokeText: function () {}
  };
  return ctx;
}

function El(tag, doc) {
  this.tagName = String(tag).toUpperCase();
  this.nodeName = this.tagName;
  this.ownerDocument = doc;
  this.children = [];
  this.childNodes = [];
  this.parentNode = null;
  this.attrs = {};
  this.listeners = {};
  this._text = '';
  this.hidden = false;
  this.disabled = false;
  this.value = '';
  var style = {};
  style.setProperty = function (k, v) { style[k] = String(v); };
  style.getPropertyValue = function (k) { return style[k] || ''; };
  this.style = style;
  this.clientWidth = 0;
  this.clientHeight = 0;
  this.tabIndex = -1;
  this._left = 0;
  this._top = 0;
}
El.prototype.setAttribute = function (k, v) { this.attrs[k] = String(v); };
El.prototype.getAttribute = function (k) {
  return Object.prototype.hasOwnProperty.call(this.attrs, k) ? this.attrs[k] : null;
};
El.prototype.removeAttribute = function (k) { delete this.attrs[k]; };
Object.defineProperty(El.prototype, 'id', {
  get: function () { return this.attrs.id || ''; },
  set: function (v) { this.attrs.id = String(v); }
});
Object.defineProperty(El.prototype, 'className', {
  get: function () { return this.attrs['class'] || ''; },
  set: function (v) { this.attrs['class'] = String(v); }
});
Object.defineProperty(El.prototype, 'textContent', {
  get: function () {
    var s = this._text || '';
    for (var i = 0; i < this.childNodes.length; i++) s += this.childNodes[i].textContent || '';
    return s;
  },
  set: function (v) {
    for (var i = 0; i < this.childNodes.length; i++) this.childNodes[i].parentNode = null;
    this.childNodes = [];
    this.children = [];
    this._text = String(v);
  }
});
El.prototype.appendChild = function (child) {
  if (child.parentNode) child.parentNode.removeChild(child);
  child.parentNode = this;
  this.childNodes.push(child);
  if (child.nodeType !== 3) this.children.push(child);
  return child;
};
El.prototype.insertBefore = function (child, ref) {
  if (ref == null) return this.appendChild(child);
  if (child.parentNode) child.parentNode.removeChild(child);
  var ci = this.children.indexOf(ref);
  this.children.splice(ci < 0 ? this.children.length : ci, 0, child);
  var ni = this.childNodes.indexOf(ref);
  this.childNodes.splice(ni < 0 ? this.childNodes.length : ni, 0, child);
  child.parentNode = this;
  return child;
};
El.prototype.removeChild = function (child) {
  var i = this.childNodes.indexOf(child);
  if (i >= 0) this.childNodes.splice(i, 1);
  i = this.children.indexOf(child);
  if (i >= 0) this.children.splice(i, 1);
  child.parentNode = null;
  return child;
};
El.prototype.addEventListener = function (type, fn) {
  (this.listeners[type] || (this.listeners[type] = [])).push(fn);
};
El.prototype.removeEventListener = function (type, fn) {
  var l = this.listeners[type];
  if (!l) return;
  var i = l.indexOf(fn);
  if (i >= 0) l.splice(i, 1);
};
El.prototype.dispatch = function (type, props) {
  var ev = {
    type: type, target: this, currentTarget: this, defaultPrevented: false,
    preventDefault: function () { this.defaultPrevented = true; },
    stopPropagation: function () { this._stop = true; }
  };
  if (props) for (var k in props) if (Object.prototype.hasOwnProperty.call(props, k)) ev[k] = props[k];
  var node = this;
  while (node) {
    var l = node.listeners && node.listeners[type];
    if (l) for (var i = 0; i < l.length; i++) {
      ev.currentTarget = node;
      l[i].call(node, ev);
    }
    if (ev._stop) break;
    node = node.parentNode;
  }
  return ev;
};
El.prototype.focus = function () { this.ownerDocument.activeElement = this; };
El.prototype.blur = function () {
  if (this.ownerDocument.activeElement === this) this.ownerDocument.activeElement = this.ownerDocument.body;
};
El.prototype.contains = function (node) {
  while (node) {
    if (node === this) return true;
    node = node.parentNode;
  }
  return false;
};
El.prototype.getBoundingClientRect = function () {
  return { left: this._left, top: this._top, width: this.clientWidth, height: this.clientHeight,
    right: this._left + this.clientWidth, bottom: this._top + this.clientHeight };
};
El.prototype.setPointerCapture = function () {};
El.prototype.releasePointerCapture = function () {};
El.prototype.getContext = function (type) {
  if (String(type) !== '2d') return null;
  if (!this._ctx) this._ctx = makeContext();
  return this._ctx;
};
El.prototype.scrollIntoView = function () { this._scrolled = true; };
El.prototype.matches = matches;
El.prototype.querySelectorAll = function (sel) {
  var out = [];
  walk(this, function (el) { if (matches(el, sel)) out.push(el); });
  return out;
};
El.prototype.querySelector = function (sel) {
  var found = null;
  walk(this, function (el) { if (!found && matches(el, sel)) found = el; });
  return found;
};

function walk(node, fn) {
  for (var i = 0; i < node.children.length; i++) {
    fn(node.children[i]);
    walk(node.children[i], fn);
  }
}

function matches(el, sel) {
  sel = String(sel).trim();
  if (!sel) return false;
  if (sel.charAt(0) === '.') {
    return el.className.split(/\s+/).indexOf(sel.slice(1)) >= 0;
  }
  var m = /^\[([^\]=]+)(?:="([^"]*)")?\]$/.exec(sel);
  if (m) {
    var v = el.getAttribute(m[1]);
    if (m[2] === undefined) return v !== null;
    return v === m[2];
  }
  return el.tagName === sel.toUpperCase();
}

function createDocument() {
  var doc = {};
  doc.body = new El('body', doc);
  doc.documentElement = doc.body;
  doc.activeElement = doc.body;
  doc.hidden = false;
  doc.listeners = {};
  doc.created = 0;
  doc.createElement = function (tag) { doc.created++; return new El(tag, doc); };
  doc.createElementNS = function (ns, tag) { doc.created++; return new El(tag, doc); };
  doc.createTextNode = textNode;
  doc.getElementById = function (id) {
    var found = null;
    if (doc.body.id === id) return doc.body;
    walk(doc.body, function (el) { if (!found && el.id === id) found = el; });
    return found;
  };
  doc.querySelectorAll = function (sel) { return doc.body.querySelectorAll(sel); };
  doc.querySelector = function (sel) { return doc.body.querySelector(sel); };
  doc.addEventListener = function (type, fn) {
    (doc.listeners[type] || (doc.listeners[type] = [])).push(fn);
  };
  doc.dispatch = function (type, props) {
    var l = doc.listeners[type] || [];
    var ev = { type: type, target: doc.body, currentTarget: doc, defaultPrevented: false,
      preventDefault: function () { this.defaultPrevented = true; },
      stopPropagation: function () {} };
    if (props) for (var k in props) if (Object.prototype.hasOwnProperty.call(props, k)) ev[k] = props[k];
    for (var i = 0; i < l.length; i++) l[i].call(doc, ev);
    return ev;
  };
  bootstrap(doc);
  return doc;
}

function bootstrap(doc) {
  var body = doc.body;
  function add(tag, id, attrs, parent) {
    var el = doc.createElement(tag);
    if (id) el.id = id;
    if (attrs) for (var k in attrs) if (Object.prototype.hasOwnProperty.call(attrs, k)) el.setAttribute(k, attrs[k]);
    (parent || body).appendChild(el);
    return el;
  }
  add('div', 'toasts');
  add('span', 'bus-pill');
  add('div', 'offline');
  var banner = add('div', 'writes-banner');
  add('p', 'writes-text', null, banner);
  add('button', 'writes-on');
  add('button', 'writes-toggle');
  add('button', 'retry-btn');
  add('button', 'scan-btn');
  add('span', 'bus-text');
  add('span', 'bus-count');
  add('div', 'groups');
  var search = add('input', 'lamp-search');
  search.type = 'search';
  add('div', 'lamps');
  add('p', 'lamps-hint');
  var panels = {};
  var tablist = add('div', 'tablist', { role: 'tablist' });
  ['dashboard', 'program', 'settings'].forEach(function (name) {
    add('button', 'tab-' + name + '-btn', { role: 'tab', 'data-tab': name }, tablist);
    panels[name] = add('section', 'tab-' + name, { role: 'tabpanel' });
  });
  add('div', 'prog-root', null, panels.program);
}

function Clock() {
  this.t = 0;
  this.seq = 1;
  this.timers = new Map();
}
Clock.prototype.setTimeout = function (fn, ms) {
  var id = this.seq++;
  this.timers.set(id, { fn: fn, at: this.t + (ms || 0), every: 0 });
  return id;
};
Clock.prototype.clearTimeout = function (id) { this.timers.delete(id); };
Clock.prototype.setInterval = function (fn, ms) {
  var id = this.seq++;
  this.timers.set(id, { fn: fn, at: this.t + (ms || 1), every: ms || 1 });
  return id;
};
Clock.prototype.tick = function (ms) {
  var end = this.t + ms;
  for (;;) {
    var best = null, bestId = null;
    this.timers.forEach(function (t, id) {
      if (t.at <= end && (!best || t.at < best.at)) { best = t; bestId = id; }
    });
    if (!best) break;
    this.t = best.at;
    if (best.every) best.at += best.every;
    else this.timers.delete(bestId);
    best.fn();
  }
  this.t = end;
};

function cloneSlots(slots) {
  return slots.map(function (s) {
    return { minute: s.minute, ch: s.ch.slice(), enabled: !!s.enabled };
  });
}

function HttpError(code, message) {
  this.code = code;
  this.message = message;
}

function parseUint(text, max) {
  if (text == null || !/^[0-9]+$/.test(text)) return null;
  var v = parseInt(text, 10);
  return v <= max ? v : null;
}

function makeServer(opts) {
  opts = opts || {};
  var labels = ['int', 'clr', null, 'r', 'am'];
  var max = [255, 255, 100, 100, 100];
  var group = opts.group || {
    gid: 2, name: 'Tuna Sun', model: 'tuna_sun', labels: labels, max: max,
    mode: 'program', now_minute: 600,
    slots: [
      { minute: 480, ch: [30, 60, 0, 0, 0], enabled: true },
      { minute: 630, ch: [150, 120, 0, 5, 15], enabled: true },
      { minute: 780, ch: [210, 170, 0, 20, 35], enabled: true },
      { minute: 960, ch: [170, 140, 0, 10, 25], enabled: true },
      { minute: 1140, ch: [70, 70, 0, 0, 10], enabled: true },
      { minute: 1290, ch: [8, 12, 0, 0, 0], enabled: true },
      { minute: 0, ch: [0, 0, 0, 0, 0], enabled: false },
      { minute: 0, ch: [0, 0, 0, 0, 0], enabled: false }
    ]
  };
  var groups = opts.groups || [group];
  var writes = opts.writes !== false;
  groups.forEach(function (g) {
    if (!g.defaults) g.defaults = cloneSlots(g.slots);
  });
  var lamps = opts.lamps || [];
  var requests = [];

  function stateJson() {
    return {
      groups: groups.map(function (g) {
        return {
          gid: g.gid, kind: g.kind || 'bus', name: g.name, model: g.model, mode: g.mode || 'program',
          status: 'Program', labels: g.labels.slice(), max: g.max.slice(),
          slots: cloneSlots(g.slots), defaults: g.defaults ? cloneSlots(g.defaults) : undefined, lamps: lamps.filter(function (l) {
            return l.gid === g.gid;
          }).map(function (l) { return l.kid; }),
          now_minute: g.now_minute == null ? 600 : g.now_minute,
          selected: g.selected || 0
        };
      }),
      lamps: lamps.map(function (l) {
        return { kid: l.kid, type: l.type, gid: l.gid, name: l.name || '' };
      }),
      bus: { state: 'Controlling', writes: writes, known_lamps: lamps.length }
    };
  }

  function handlePoint(q) {
    var gid = parseUint(q.get('gid'), 164);
    if (gid !== group.gid) throw new HttpError(404, 'unknown group');
    var slot = parseUint(q.get('slot'), group.slots.length);
    if (!slot || slot < 1) throw new HttpError(400, 'invalid parameter: slot');
    var p = group.slots[slot - 1];
    if (q.has('minute')) {
      var m = parseUint(q.get('minute'), 1439);
      if (m === null) throw new HttpError(400, 'invalid parameter: minute');
      p.minute = m;
    }
    for (var c = 0; c < 5; c++) {
      var name = 'c' + (c + 1);
      if (!q.has(name)) continue;
      if (group.labels[c] === null) throw new HttpError(400, 'this model has no channel ' + name);
      var v = parseUint(q.get(name), group.max[c]);
      if (v === null) throw new HttpError(400, 'invalid parameter: ' + name);
      p.ch[c] = v;
    }
    if (q.has('enabled')) {
      var e = q.get('enabled');
      if (e !== '0' && e !== '1') throw new HttpError(400, 'invalid parameter: enabled');
      p.enabled = e === '1';
    }
  }

  /* Canned /klink/snap answer: deterministic settings the tests can assert against. */
  function snapJson(rgb) {
    var set = { clr: 100 };
    if (group.model === 'tuna_sun') { set.r = 20; set.am = 10; }
    else if (group.model === 'tuna_blue') { set.vio = 30; set.r = 20; set.grn = 10; }
    return { set: set, rgb: '403020', target: rgb, exact: false };
  }

  function response(code, body) {
    var text = JSON.stringify(body);
    return {
      ok: code >= 200 && code < 300, status: code,
      text: function () { return Promise.resolve(text); }
    };
  }

  function fetch(url, opts) {
    var u = new URL(url, 'http://mock.local');
    var params = {};
    u.searchParams.forEach(function (v, k) { params[k] = v; });
    requests.push({ path: u.pathname, params: params, method: (opts && opts.method) || 'GET' });
    try {
      if (u.pathname === '/klink/state') return Promise.resolve(response(200, stateJson()));
      if (u.pathname === '/klink/snap') {
        var gid = parseUint(u.searchParams.get('gid'), 164);
        if (gid !== group.gid) throw new HttpError(404, 'unknown group');
        if (group.model !== 'tuna_sun' && group.model !== 'tuna_blue') {
          throw new HttpError(400, 'no colour model for this group');
        }
        var rgb = (u.searchParams.get('rgb') || '').toLowerCase().replace(/^#/, '');
        if (!/^[0-9a-f]{6}$/.test(rgb)) throw new HttpError(400, 'invalid parameter: rgb');
        return Promise.resolve(response(200, snapJson(rgb)));
      }
      if (u.pathname === '/klink/point') { handlePoint(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/preview/done') return Promise.resolve(response(200, { ok: true }));
      if (u.pathname === '/klink/preview') return Promise.resolve(response(200, { ok: true }));
      throw new HttpError(404, 'not found');
    } catch (e) {
      return Promise.resolve(response(e.code || 500, { error: e.message }));
    }
  }

  return {
    group: group,
    groups: groups,
    lamps: lamps,
    requests: requests,
    fetch: fetch,
    pointRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/point'; });
    },
    snapRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/snap'; });
    },
    resetRequests: function () { requests.length = 0; }
  };
}

var APP = path.join(__dirname, '..', '..', 'components', 'klink', 'web', 'app.js');
var KCOL = 'var KCOL={sun:' + JSON.stringify(new Array(17).fill('#102030')) +
  ',blue:' + JSON.stringify(new Array(17).fill('#203040')) +
  ',ov:{r:"#ff0000",am:"#ffaa00",vio:"#8800ff",grn:"#00ff00",g:"#00ff00",b:"#0000ff",ct:"#ffb46b",w:"#eeeeee"}};';

function flush() {
  return new Promise(function (resolve) { setImmediate(resolve); });
}

async function settle(n) {
  n = n || 4;
  for (var i = 0; i < n; i++) await flush();
}

function loadPage(server) {
  var clock = new Clock();
  var doc = createDocument();
  var stored = {};
  var localStorage = {
    getItem: function (k) { return Object.prototype.hasOwnProperty.call(stored, k) ? stored[k] : null; },
    setItem: function (k, v) { stored[k] = String(v); }
  };
  var window = { addEventListener: function () {}, console: console, devicePixelRatio: 1 };
  var location = { hash: '' };
  var src = fs.readFileSync(APP, 'utf8').replace('/*KCOL*/', KCOL);
  var run = new Function('document', 'window', 'location', 'localStorage', 'fetch',
    'setTimeout', 'clearTimeout', 'setInterval', 'console', src);
  run(doc, window, location, localStorage, server.fetch,
    clock.setTimeout.bind(clock), clock.clearTimeout.bind(clock),
    clock.setInterval.bind(clock), console);

  var page = {
    doc: doc,
    clock: clock,
    server: server,
    settle: settle,
    progRoot: function () { return doc.getElementById('prog-root'); },
    groupsEl: function () { return doc.getElementById('groups'); },
    lampsEl: function () { return doc.getElementById('lamps'); },
    searchEl: function () { return doc.getElementById('lamp-search'); },
    storage: function (key) { return stored[key]; },
    refresh: async function () { doc.dispatch('visibilitychange'); await settle(); },
    chart: function () { return doc.getElementById('prog-chart'); },
    wheelCard: function () { return page.progRoot() ? page.progRoot().querySelector('.pick') : null; },
    wheel: function () { return page.progRoot() ? page.progRoot().querySelector('.wheel') : null; },
    hexInput: function () {
      var row = page.progRoot() ? page.progRoot().querySelector('.whexrow') : null;
      if (!row) return null;
      var found = null;
      walk(row, function (el) { if (!found && el.tagName === 'INPUT') found = el; });
      return found;
    },
    snapButton: function () {
      var row = page.progRoot() ? page.progRoot().querySelector('.whexrow') : null;
      if (!row) return null;
      var found = null;
      walk(row, function (el) { if (!found && el.tagName === 'BUTTON') found = el; });
      return found;
    },
    svg: function () {
      var c = page.chart();
      return c ? c.children[0] : null;
    },
    setSize: async function (w, h) {
      var svg = page.svg();
      if (svg) { svg.clientWidth = w; svg.clientHeight = h; }
      doc.dispatch('visibilitychange');
      await settle();
    },
    button: function (text) {
      var found = null;
      walk(page.progRoot() || doc.body, function (el) {
        if (!found && el.tagName === 'BUTTON' && el.textContent.indexOf(text) >= 0) found = el;
      });
      return found;
    },
    click: function (el) { if (el) el.dispatch('click', {}); },
    pointer: function (el, type, props) { return el.dispatch(type, props || {}); },
    toastText: function () { return doc.getElementById('toasts').textContent; }
  };
  return page;
}

module.exports = {
  loadPage: loadPage,
  makeServer: makeServer,
  settle: settle,
  flush: flush,
  cloneSlots: cloneSlots
};
