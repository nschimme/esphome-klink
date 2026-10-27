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
  var locationCard = add('div', 'location-card', null, panels.settings);
  add('p', 'location-line', null, locationCard);
  add('p', 'location-note', null, locationCard);
  add('div', 'outputs', null, panels.settings);
  add('div', 'acclimations', null, panels.settings);
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
  var location = opts.location === undefined
    ? { lat: 41.88, lon: -87.63, source: 'Home Assistant' }
    : opts.location;
  groups.forEach(function (g) {
    if (!g.defaults) g.defaults = cloneSlots(g.slots);
    if (!g.effects) g.effects = [];
    if (g.effect === undefined) g.effect = null;
    if (g.effect_speed == null) g.effect_speed = 0;
    if (g.effect_minutes == null) g.effect_minutes = 2;
    /* Bus groups have output options; light programs (like the firmware) do not. */
    if (g.kind !== 'light' && g.output === undefined) {
      g.output = { curve: 0, seconds: 0, soft_start: 30 };
    }
    if (g.output && g.output.soft_start === undefined) g.output.soft_start = 30;
    /* Every program can acclimate; off means full light. */
    if (g.acclimation === undefined) {
      g.acclimation = { on: false, start: 30, days: 14, share: 100 };
    }
    /* Only a bus group has the sun and moon sources. */
    if (g.kind !== 'light') {
      if (g.source == null) g.source = 0;
      if (!g.sky) g.sky = { peak: 60, hours: 0, glow: 100, ramp: 150, amber: 0, shift: 0, moon: 5, phase: 1 };
    }
  });
  var lamps = opts.lamps || [];
  var requests = [];
  /* The page's fake clock drives effect expiry too, so a test can tick through a whole effect. */
  var currentNow = opts.now || function () { return Date.now(); };

  function findGroup(gid) {
    for (var i = 0; i < groups.length; i++) if (groups[i].gid === gid) return groups[i];
    return null;
  }

  function effectOf(g) {
    if (!g.effect) return null;
    var remaining = -1;
    if (g.effect.seconds > 0) {
      remaining = g.effect.seconds - Math.floor((currentNow() - g.effect.start) / 1000);
      if (remaining <= 0) { g.effect = null; return null; }
    }
    return { name: g.effect.name, remaining: remaining };
  }

  /* Illustrative sun and moon days, like the mock: a plausible curve, not real astronomy. */
  function pad2(v) { return ('0' + v).slice(-2); }
  function todayIso() {
    var d = new Date();
    return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate());
  }
  function generatedFor(g, source, date) {
    if (typeof opts.generated === 'function') {
      var custom = opts.generated(g.gid, source, date);
      if (custom) return custom;
    }
    if (source === 1) {
      return {
        points: [
          { minute: 434, ch: [0, 0, 0, 0, 0], enabled: true },
          { minute: 760, ch: [200, 120, 0, 0, 0], enabled: true },
          { minute: 1076, ch: [0, 0, 0, 0, 0], enabled: true }
        ],
        summary: { sunrise: 434, noon: 760, sunset: 1076, length: 642, always_up: false }
      };
    }
    return {
      points: [
        { minute: 1145, ch: [0, 100, 0, 0, 0], enabled: true },
        { minute: 66, ch: [12, 100, 0, 0, 0], enabled: true },
        { minute: 428, ch: [0, 100, 0, 0, 0], enabled: true }
      ],
      summary: { rise: 1145, set: 428, illumination: 0.98 }
    };
  }
  function groupGenerated(g) {
    if (Array.isArray(g.generated)) return cloneSlots(g.generated);
    return generatedFor(g, g.source, todayIso()).points;
  }

  function stateJson() {
    return {
      groups: groups.map(function (g) {
        var item = {
          gid: g.gid, kind: g.kind || 'bus', name: g.name, model: g.model, mode: g.mode || 'program',
          status: 'Program', labels: g.labels.slice(), max: g.max.slice(),
          effects: (g.effects || []).slice(), effect: effectOf(g),
          effect_speed: g.effect_speed, effect_minutes: g.effect_minutes,
          slots: cloneSlots(g.slots), defaults: g.defaults ? cloneSlots(g.defaults) : undefined, lamps: lamps.filter(function (l) {
            return l.gid === g.gid;
          }).map(function (l) { return l.kid; }),
          now_minute: g.now_minute == null ? 600 : g.now_minute,
          selected: g.selected || 0,
          acclimation: {
            on: !!g.acclimation.on, start: g.acclimation.start | 0,
            days: g.acclimation.days | 0, share: g.acclimation.share | 0
          }
        };
        if (g.output) {
          item.output = { curve: g.output.curve | 0,
            seconds: Number(g.output.seconds) || 0,
            soft_start: g.output.soft_start | 0 };
        }
        if ((g.kind || 'bus') !== 'light') {
          item.source = g.source;
          item.sources = (g.model === 'tuna_blue' || g.model === 'light_rgb' || g.model === 'light_rgbw') ? [0, 1, 2] : [0, 1];
          item.source_note = g.source !== 0 && location === null ? 'Set a location' : (g.source_note || '');
          item.sky = {
            peak: g.sky.peak | 0, hours: g.sky.hours | 0, glow: g.sky.glow | 0,
            ramp: g.sky.ramp | 0, amber: g.sky.amber | 0, shift: g.sky.shift | 0, moon: g.sky.moon | 0,
            phase: g.sky.phase | 0
          };
          if (g.source !== 0 && location) item.generated = groupGenerated(g);
        }
        return item;
      }),
      lamps: lamps.map(function (l) {
        return { kid: l.kid, type: l.type, gid: l.gid, name: l.name || '' };
      }),
      location: location ? { lat: location.lat, lon: location.lon, source: location.source } : null,
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

  function parseMinutes(text) {
    if (text == null || text === '') return null;
    var value = Number(text);
    if (isNaN(value) || value < 0 || value > 5) return null;
    if (Math.abs(value * 2 - Math.round(value * 2)) > 1e-6) return null;
    return value;
  }

  var SPEED_NAMES = { fast: 0, moderate: 1, slow: 2 };

  function handleEffect(q) {
    var g = findGroup(parseUint(q.get('gid'), 164));
    if (!g) throw new HttpError(404, 'unknown group');
    if (!q.has('name')) throw new HttpError(400, 'missing parameter: name');
    var name = q.get('name');
    if ((g.effects || []).indexOf(name) < 0) throw new HttpError(404, 'unknown effect');
    var speed = g.effect_speed;
    if (q.has('speed')) {
      speed = SPEED_NAMES[q.get('speed')];
      if (speed === undefined) throw new HttpError(400, 'invalid parameter: speed');
    }
    var minutes = g.effect_minutes;
    if (q.has('minutes')) {
      minutes = parseMinutes(q.get('minutes'));
      if (minutes === null) throw new HttpError(400, 'invalid parameter: minutes');
    }
    g.effect_speed = speed;
    g.effect_minutes = minutes;
    g.effect = { name: name, start: currentNow(), seconds: minutes === 0 ? 0 : Math.round(minutes * 60) };
  }

  function handleEffectStop(q) {
    var g = findGroup(parseUint(q.get('gid'), 164));
    if (!g) throw new HttpError(404, 'unknown group');
    g.effect = null;
  }

  /* Same validation as the firmware: any subset, curve 0..1, seconds 0..10. */
  function handleOutput(q) {
    var g = findGroup(parseUint(q.get('gid'), 164));
    if (!g) throw new HttpError(404, 'unknown group');
    if (!g.output) throw new HttpError(400, 'this program has no output options');
    var values = { curve: g.output.curve, seconds: g.output.seconds, soft_start: g.output.soft_start };
    if (q.has('curve')) {
      var curve = parseUint(q.get('curve'), 1);
      if (curve === null) throw new HttpError(400, 'invalid parameter: curve');
      values.curve = curve;
    }
    if (q.has('seconds')) {
      var text = q.get('seconds');
      var seconds = text === '' ? NaN : Number(text);
      if (!isFinite(seconds) || seconds < 0 || seconds > 10) throw new HttpError(400, 'invalid parameter: seconds');
      values.seconds = seconds;
    }
    if (q.has('soft_start')) {
      var soft = parseUint(q.get('soft_start'), 120);
      if (soft === null) throw new HttpError(400, 'invalid parameter: soft_start');
      values.soft_start = soft;
    }
    g.output = values;
  }

  /* Same validation as the firmware: any subset, start 10..90, days 1..90. */
  function handleAcclimation(q) {
    var gid = parseUint(q.get('gid'), 164);
    var g = findGroup(gid);
    if (!g) throw new HttpError(404, 'unknown group');
    var a = g.acclimation;
    var on = a.on;
    if (q.has('on')) {
      var text = q.get('on');
      if (text !== '0' && text !== '1') throw new HttpError(400, 'invalid parameter: on');
      on = text === '1';
    }
    var start = a.start;
    if (q.has('start')) {
      start = parseUint(q.get('start'), 90);
      if (start === null || start < 10) throw new HttpError(400, 'invalid parameter: start');
    }
    var days = a.days;
    if (q.has('days')) {
      days = parseUint(q.get('days'), 90);
      if (days === null || days < 1) throw new HttpError(400, 'invalid parameter: days');
    }
    a.on = on;
    a.start = start;
    a.days = days;
    a.share = on ? start : 100;
  }

  function skyGroup(gidText) {
    var gid = parseUint(gidText, 164);
    if (gid === null) throw new HttpError(400, 'invalid parameter: gid');
    var g = findGroup(gid);
    if (!g) throw new HttpError(404, 'unknown group');
    if ((g.kind || 'bus') === 'light') throw new HttpError(400, 'this program has no sun or moon source');
    return g;
  }
  function parseSource(text) {
    if (text === 'points') return 0;
    if (text === 'sun') return 1;
    if (text === 'moon') return 2;
    throw new HttpError(400, 'invalid parameter: source');
  }
  function handleSource(q) {
    var g = skyGroup(q.get('gid'));
    if (!q.has('source')) throw new HttpError(400, 'invalid parameter: source');
    g.source = parseSource(q.get('source'));
  }
  function handleSky(q) {
    var g = skyGroup(q.get('gid'));
    var s = g.sky;
    function num(name, max) {
      var v = parseUint(q.get(name), max);
      if (v === null) throw new HttpError(400, 'invalid parameter: ' + name);
      return v;
    }
    if (q.has('peak')) s.peak = num('peak', 100);
    if (q.has('hours')) {
      var h = num('hours', 16);
      if (h !== 0 && h < 4) throw new HttpError(400, 'invalid parameter: hours');
      s.hours = h;
    }
    if (q.has('glow')) s.glow = num('glow', 100);
    /* The device keeps the ramp in whole tens of minutes, so it rounds a posted value down. */
    if (q.has('ramp')) s.ramp = Math.floor(num('ramp', 240) / 10) * 10;
    if (q.has('amber')) s.amber = num('amber', 10);
    if (q.has('moon')) s.moon = num('moon', 100);
    if (q.has('phase')) s.phase = num('phase', 1);
    if (q.has('shift')) {
      var text = q.get('shift');
      if (!/^[+-]?\d+$/.test(text)) throw new HttpError(400, 'invalid parameter: shift');
      var shift = parseInt(text, 10);
      if (shift < -120 || shift > 120) throw new HttpError(400, 'invalid parameter: shift');
      s.shift = shift;
    }
  }
  function fitPoints(points, count) {
    /* The firmware keeps the slot count and fills slots the day does not reach with off points. */
    var out;
    if (points.length <= count) {
      out = cloneSlots(points);
    } else {
      out = [];
      for (var i = 0; i < count; i++) {
        var index = count > 1 ? Math.round(i * (points.length - 1) / (count - 1)) : 0;
        out.push(cloneSlots([points[index]])[0]);
      }
    }
    while (out.length < count) out.push({ minute: 0, ch: [0, 0, 0, 0, 0], enabled: false });
    return out;
  }
  function handleCopy(q) {
    var g = skyGroup(q.get('gid'));
    if (g.source === 0 || location === null) throw new HttpError(409, 'no generated day to copy');
    g.slots = fitPoints(groupGenerated(g), g.slots.length);
    g.source = 0;
  }
  var genSeen = {};
  function handleGenerated(q) {
    var g = skyGroup(q.get('gid'));
    var source = q.has('source') ? parseSource(q.get('source')) : 0;
    if (source === 0) throw new HttpError(400, 'invalid parameter: source');
    var date = q.get('date') || '';
    if (!/^\d{4}-\d{2}-\d{2}$/.test(date)) throw new HttpError(400, 'invalid parameter: date');
    if (location === null) throw new HttpError(409, 'no location set');
    var key = g.gid + '/' + source + '/' + date;
    if (!genSeen[key]) {
      genSeen[key] = true;
      return { pending: true };
    }
    return generatedFor(g, source, date);
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
          throw new HttpError(400, 'no color model for this group');
        }
        var rgb = (u.searchParams.get('rgb') || '').toLowerCase().replace(/^#/, '');
        if (!/^[0-9a-f]{6}$/.test(rgb)) throw new HttpError(400, 'invalid parameter: rgb');
        return Promise.resolve(response(200, snapJson(rgb)));
      }
      if (u.pathname === '/klink/point') { handlePoint(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/effect') { handleEffect(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/effect/stop') { handleEffectStop(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/output') { handleOutput(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/acclimation') { handleAcclimation(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/source') { handleSource(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/sky') { handleSky(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/source/copy') { handleCopy(u.searchParams); return Promise.resolve(response(200, { ok: true })); }
      if (u.pathname === '/klink/generated') return Promise.resolve(response(200, handleGenerated(u.searchParams)));
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
    effectRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/effect'; });
    },
    stopRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/effect/stop'; });
    },
    outputRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/output'; });
    },
    acclimationRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/acclimation'; });
    },
    sourceRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/source'; });
    },
    skyRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/sky'; });
    },
    copyRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/source/copy'; });
    },
    generatedRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/generated'; });
    },
    previewRequests: function () {
      return requests.filter(function (r) { return r.path === '/klink/preview'; });
    },
    now: function (fn) {
      if (typeof fn === 'function') currentNow = fn;
      return currentNow();
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
  ',refugium:' + JSON.stringify(new Array(17).fill('#304050')) +
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
  if (server.now) server.now(function () { return clock.t; });
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
