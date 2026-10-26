#!/usr/bin/env node
'use strict';
/*
 * Program tab scenarios for the esphome-klink page, run against the assembled app script with
 * a tiny DOM/timer/fetch stub (tests/web/dom_stub.js). No npm packages, no browser, no ports.
 *
 *     node tests/web/run.js
 */
var stub = require('./dom_stub');

var failures = 0;
function ok(name) { console.log('ok   ' + name); }
function fail(name, msg) {
  failures++;
  console.log('FAIL ' + name + (msg ? ': ' + msg : ''));
}
function check(name, cond, msg) { if (cond) ok(name); else fail(name, msg); }
function eq(name, actual, expected) {
  var a = JSON.stringify(actual), e = JSON.stringify(expected);
  check(name, a === e, 'expected ' + e + ' got ' + a);
}
function slots(server) { return stub.cloneSlots(server.group.slots); }

var Q = { l: 32, r: 10, t: 10, b: 18, w: 1000, h: 300 };
function xFor(m) { return Q.l + m / 1440 * (Q.w - Q.l - Q.r); }
function yFor(pct) { return Q.t + (100 - pct) / 100 * (Q.h - Q.t - Q.b); }
function cv(v) { return Math.round(v * 100 / 255); }

function buttonExact(page, text) {
  var found = null;
  var root = page.progRoot();
  (function walk(node) {
    for (var i = 0; i < node.children.length; i++) {
      var el = node.children[i];
      if (!found && el.tagName === 'BUTTON' && el.textContent.trim() === text) found = el;
      walk(el);
    }
  })(root);
  return found;
}
function numberInputs(page) {
  return page.progRoot().querySelectorAll('input').filter(function (el) { return el.type === 'number'; });
}

async function newPage(server) {
  var p = stub.loadPage(server);
  await stub.settle();
  await p.setSize(Q.w, Q.h);
  return p;
}

function cardText(el) { return el.textContent.replace(/\s+/g, ' ').trim(); }

function makeGroup(gid, name, model, labels) {
  return {
    gid: gid, name: name, model: model,
    labels: labels || ['int', 'clr', null, 'r', 'am'],
    max: [255, 255, 100, 100, 100], mode: 'program', now_minute: 600,
    slots: [
      { minute: 480, ch: [10, 10, 0, 0, 0], enabled: true },
      { minute: 720, ch: [100, 100, 0, 0, 0], enabled: true },
      { minute: 1200, ch: [5, 5, 0, 0, 0], enabled: true }
    ]
  };
}

/* Dashboard card helpers: cards have no ids, so they are found by their heading text. */
function dashCard(page, title) {
  var cards = page.groupsEl().children.filter(function (el) { return el.className === 'card'; });
  return cards.filter(function (el) { return cardText(el).indexOf(title) >= 0; })[0];
}
function fxRow(card) { return card ? card.querySelector('.fx') : null; }
function fxButtons(card) { return fxRow(card).querySelectorAll('.fx-btn'); }
function fxNamed(card, label) {
  var found = null;
  fxButtons(card).forEach(function (b) { if (!found && b.textContent.trim() === label) found = b; });
  return found;
}
function fxSpeed(card, label) {
  var found = null;
  fxRow(card).querySelector('.fxseg').children.forEach(function (b) {
    if (!found && b.textContent.trim() === label) found = b;
  });
  return found;
}
function fxDuration(card) { return fxRow(card).querySelector('select'); }

function makeLight(gid, name, model, labels) {
  var g = makeGroup(gid, name, model, labels);
  g.kind = 'light';
  g.slots = [
    { minute: 480, ch: [20, 0, 100, 60, 20], enabled: true },
    { minute: 720, ch: [140, 0, 100, 80, 50], enabled: true },
    { minute: 1200, ch: [5, 0, 100, 40, 10], enabled: true },
    { minute: 0, ch: [0, 0, 0, 0, 0], enabled: false }
  ];
  return g;
}

function picker(page) { return page.progRoot().children[0]; }
function pickerSelect(page) {
  var p = picker(page), i;
  for (i = 0; i < p.children.length; i++) if (p.children[i].tagName === 'SELECT') return p.children[i];
  return null;
}

function place(page, minute) {
  page.pointer(page.svg(), 'pointerdown', { button: 0, pointerId: 1, clientX: xFor(minute), clientY: 120 });
}

async function scenarioPlace() {
  var name = 'place point';
  var server = stub.makeServer();
  var page = await newPage(server);
  var add = buttonExact(page, '+ Add point');
  check(name + ': add button present', !!add);
  page.click(add);
  check(name + ': armed instruction', add.textContent.indexOf('Tap the chart where the point should go') >= 0);
  server.resetRequests();
  place(page, 720);
  await stub.settle();
  var pts = server.pointRequests();
  check(name + ': exactly one point request', pts.length === 1, 'got ' + pts.length);
  var slot = server.group.slots[6];
  eq(name + ': first free slot used', { minute: slot.minute, enabled: slot.enabled }, { minute: 720, enabled: true });
  eq(name + ': interpolated values', slot.ch, [186, 150, 0, 14, 27]);
  if (pts[0]) {
    eq(name + ': request payload', {
      gid: pts[0].params.gid, slot: pts[0].params.slot, minute: pts[0].params.minute,
      c1: pts[0].params.c1, c2: pts[0].params.c2, c4: pts[0].params.c4, c5: pts[0].params.c5,
      c3: pts[0].params.c3, enabled: pts[0].params.enabled
    }, { gid: '2', slot: '7', minute: '720', c1: '186', c2: '150', c4: '14', c5: '27', c3: undefined, enabled: '1' });
  }
  check(name + ': disarmed after placing', add.textContent.trim() === '+ Add point');
  check(name + ': chart scrollIntoView', page.chart()._scrolled === true);
}

async function scenarioRefused() {
  var name = 'refuse too close';
  var server = stub.makeServer();
  var page = await newPage(server);
  var add = buttonExact(page, '+ Add point');
  page.click(add);
  server.resetRequests();
  place(page, 631);
  await stub.settle();
  check(name + ': no request', server.pointRequests().length === 0, 'got ' + server.pointRequests().length);
  check(name + ': toast names the time', page.toastText().indexOf('There is already a point at 10:30') >= 0, page.toastText());
  check(name + ': stays armed', add.textContent.indexOf('Tap the chart') >= 0);
  page.chart().dispatch('keydown', { key: 'Escape' });
  check(name + ': Esc disarms', add.textContent.trim() === '+ Add point');
}

async function scenarioAllUsed() {
  var name = 'all slots used';
  var server = stub.makeServer();
  server.group.slots.forEach(function (s) { s.enabled = true; });
  var page = await newPage(server);
  var add = buttonExact(page, '+ Add point');
  check(name + ': add disabled', !!add && add.disabled === true);
  check(name + ': title hints', !!add && add.getAttribute('title').indexOf('All 8 points are used') >= 0);
  check(name + ': visible hint', page.progRoot().textContent.indexOf('All 8 points are used. Remove one to add another.') >= 0);
}

async function scenarioDragUndo() {
  var name = 'drag undo/redo';
  var server = stub.makeServer();
  var page = await newPage(server);
  var pre = slots(server);
  var svg = page.svg();
  server.resetRequests();
  page.pointer(svg, 'pointerdown', { button: 0, pointerId: 7, clientX: xFor(480), clientY: yFor(cv(30)) });
  page.pointer(svg, 'pointermove', { pointerId: 7, clientX: xFor(540), clientY: yFor(cv(60)) });
  page.pointer(svg, 'pointerup', { pointerId: 7 });
  page.clock.tick(120);
  await stub.settle();
  var post = slots(server);
  check(name + ': drag moved the point', post[0].minute === 540, 'minute ' + post[0].minute);
  check(name + ': drag used one request', server.pointRequests().length === 1, server.pointRequests().length);
  check(name + ': snapshot differs', JSON.stringify(pre) !== JSON.stringify(post));
  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': undo restores snapshot', slots(server), pre);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);
  server.resetRequests();
  page.click(buttonExact(page, 'Redo'));
  await stub.settle();
  eq(name + ': redo restores drag', slots(server), post);
  check(name + ': redo one request', server.pointRequests().length === 1, server.pointRequests().length);
}

async function scenarioAddUndo() {
  var name = 'add undo/redo';
  var server = stub.makeServer();
  var page = await newPage(server);
  var pre = slots(server);
  server.resetRequests();
  page.click(buttonExact(page, '+ Add point'));
  place(page, 720);
  await stub.settle();
  var post = slots(server);
  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': undo removes the point', slots(server), pre);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);
  server.resetRequests();
  page.click(buttonExact(page, 'Redo'));
  await stub.settle();
  eq(name + ': redo adds it back', slots(server), post);
  check(name + ': redo one request', server.pointRequests().length === 1, server.pointRequests().length);
}

async function scenarioRemoveUndo() {
  var name = 'remove undo';
  var server = stub.makeServer();
  var page = await newPage(server);
  var pre = slots(server);
  server.resetRequests();
  page.click(buttonExact(page, 'Remove point'));
  await stub.settle();
  check(name + ': point disabled', server.group.slots[0].enabled === false);
  check(name + ': remove one request', server.pointRequests().length === 1, server.pointRequests().length);
  check(name + ': toast offers Undo', page.toastText().indexOf('Undo') >= 0, page.toastText());
  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': undo restores the point', slots(server), pre);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);
}

async function scenarioCoalesce() {
  var name = 'coalesce slider edits';
  var server = stub.makeServer();
  var page = await newPage(server);
  var num = numberInputs(page)[0];
  check(name + ': number input found', !!num);
  var before = server.group.slots[0].ch[0];
  server.resetRequests();
  [50, 60, 70].forEach(function (v) {
    num.value = String(v);
    num.dispatch('change', {});
  });
  page.clock.tick(120);
  await stub.settle();
  check(name + ': last value sent', server.group.slots[0].ch[0] === Math.round(70 * 255 / 100),
    'got ' + server.group.slots[0].ch[0]);
  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  check(name + ': one undo restores the start', server.group.slots[0].ch[0] === before,
    'got ' + server.group.slots[0].ch[0] + ' expected ' + before);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);
  check(name + ': no earlier history entry', buttonExact(page, 'Undo').disabled === true);
}

async function scenarioRevert() {
  var name = 'revert all';
  var server = stub.makeServer();
  var page = await newPage(server);
  var base = slots(server);
  page.click(buttonExact(page, '+ Add point'));
  place(page, 720);
  await stub.settle();
  var edited = slots(server);
  var revert = buttonExact(page, 'Revert all');
  check(name + ': revert offered', !!revert && revert.hidden === false);
  page.click(revert);
  var yes = buttonExact(page, 'Revert');
  check(name + ': inline confirm', !!yes);
  server.resetRequests();
  page.click(yes);
  await stub.settle();
  eq(name + ': baseline restored', slots(server), base);
  check(name + ': revert one request', server.pointRequests().length === 1, server.pointRequests().length);
  check(name + ': revert hidden again', buttonExact(page, 'Revert all').hidden === true);
  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': revert is undoable', slots(server), edited);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);
}

async function scenarioDefaultPreset() {
  var name = 'default preset';
  var server = stub.makeServer();
  var page = await newPage(server);
  var def = buttonExact(page, 'Default');
  check(name + ': Default present', !!def);
  var chips = def && def.parentNode;
  eq(name + ': Default is the first preset', chips && chips.children.map(function (b) { return b.textContent.trim(); }),
    ['Default', 'Daytime plateau', 'Always on', 'Moonlight']);
  check(name + ': Sunrise-sunset is gone', buttonExact(page, 'Sunrise-sunset') === null);
  check(name + ': disabled while the program is the default', def && def.disabled === true);
  check(name + ': disabled title', !!def && def.getAttribute('title') === 'Already the default program');

  var defaults = stub.cloneSlots(server.group.defaults);
  server.resetRequests();
  page.click(buttonExact(page, 'Remove point'));
  await stub.settle();
  check(name + ': edited away from the default', server.group.slots[0].enabled === false);
  def = buttonExact(page, 'Default');
  check(name + ': enabled after an edit', !!def && def.disabled === false);

  server.resetRequests();
  page.click(def);
  var txt = cardText(page.progRoot());
  check(name + ': confirm row asks', txt.indexOf('Replace the whole program with "Default"?') >= 0, txt);
  page.click(buttonExact(page, 'Yes'));
  await stub.settle();
  eq(name + ': defaults restored exactly', slots(server), defaults);
  check(name + ': one request for the one changed slot', server.pointRequests().length === 1,
    'got ' + server.pointRequests().length);
  check(name + ': toast names the reset', page.toastText().indexOf('Program reset to default') >= 0, page.toastText());
  check(name + ': toast offers Undo', page.toastText().indexOf('Undo') >= 0, page.toastText());
  check(name + ': disabled again', buttonExact(page, 'Default').disabled === true);

  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': undo brings the edit back', slots(server)[0], { minute: 480, ch: [30, 60, 0, 0, 0], enabled: false });
  check(name + ': undo is one request', server.pointRequests().length === 1, server.pointRequests().length);

  var bare = stub.makeServer();
  delete bare.group.defaults;
  var barePage = await newPage(bare);
  check(name + ': hidden without defaults', buttonExact(barePage, 'Default').hidden === true);
}

async function scenarioOrphans() {
  var name = 'orphan groups';
  var server = stub.makeServer({
    lamps: [
      { kid: 1, type: 1, gid: 2, name: 'A' },
      { kid: 2, type: 1, gid: 11, name: 'B' },
      { kid: 3, type: 2, gid: 11, name: 'C' }
    ]
  });
  var page = await newPage(server);
  var orphans = page.groupsEl().children.filter(function (el) { return el.className === 'card orphan'; });
  check(name + ': one orphan card', orphans.length === 1, 'got ' + orphans.length);
  var card = orphans[0];
  check(name + ': names the group', cardText(card).indexOf('Group 11') >= 0, cardText(card));
  check(name + ': lamp count', cardText(card).indexOf('2 lamps here are not driven by this controller') >= 0, cardText(card));
  check(name + ': README hint', cardText(card).indexOf('group templates') >= 0);
  var chips = card.querySelectorAll('.chip');
  check(name + ': one chip per lamp', chips.length === 2, 'got ' + chips.length);
  server.resetRequests();
  page.click(chips[0]);
  var ids = server.requests.filter(function (r) { return r.path === '/klink/identify'; });
  check(name + ': chip flashes the lamp', ids.length === 1 && ids[0].params.kid === '2', JSON.stringify(ids));
  server.resetRequests();
  page.click(card.children[2].children[0]);
  ids = server.requests.filter(function (r) { return r.path === '/klink/identify'; });
  check(name + ': find flashes the group', ids.length === 1 && ids[0].params.gid === '11', JSON.stringify(ids));
}

async function scenarioPicker() {
  var name = 'group picker';
  var gs = [
    makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']),
    makeGroup(2, 'Tuna Sun', 'tuna_sun'),
    makeGroup(3, 'Reef 3', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']),
    makeGroup(4, 'Reef 4', 'tuna_sun')
  ];
  var server = stub.makeServer({ group: gs[1], groups: gs });
  var page = await newPage(server);
  var sel = pickerSelect(page);
  check(name + ': 4 groups use a select', !!sel);
  check(name + ': labelled Lamp group', !!sel && sel.getAttribute('aria-label') === 'Lamp group');
  check(name + ': select shows the first group', !!sel && sel.value === '1', sel && sel.value);
  if (sel) {
    sel.value = '3';
    sel.dispatch('change', {});
    await stub.settle();
    eq(name + ': change persists the group', page.storage('klink.prog.gid'), '3');
    check(name + ': panel follows the group', page.progRoot().textContent.indexOf('VIO') >= 0);
  }
  /* Shrinking to three groups switches back to buttons, then back again without errors. */
  var extra = gs.pop();
  await page.refresh();
  check(name + ': 3 groups use buttons', pickerSelect(page) === null);
  check(name + ': button picker has groups', picker(page).children.length === 3, picker(page).children.length);
  gs.push(extra);
  await page.refresh();
  check(name + ': back to a select', !!pickerSelect(page));
}

async function scenarioManyLamps() {
  var name = 'settings many lamps';
  var lamps = [], i;
  for (i = 0; i < 32; i++) {
    var gid = i < 10 ? 2 : i < 22 ? 11 : 20;
    lamps.push({ kid: 18000100 + (31 - i), type: 1 + (i % 2), gid: gid, name: i % 4 === 0 ? 'Reef ' + i : '' });
  }
  var server = stub.makeServer({ lamps: lamps });
  var page = await newPage(server);
  var sections = page.lampsEl().children;
  eq(name + ': three group sections', sections.length, 3);
  eq(name + ': sections sorted by group', sections.map(function (s) { return s.children[0].textContent; }),
    ['Group 2 · 10 lamps', 'Group 11 · 12 lamps', 'Group 20 · 10 lamps']);
  var rows = page.lampsEl().querySelectorAll('[data-kid]');
  eq(name + ': 32 rows', rows.length, 32);
  var expected = lamps.slice().sort(function (a, b) { return a.gid - b.gid || a.kid - b.kid; })
    .map(function (l) { return String(l.kid); });
  eq(name + ': rows sorted by group then kid', rows.map(function (r) { return r.getAttribute('data-kid'); }), expected);
  check(name + ': compact name fallback', cardText(rows[0]).indexOf('Lamp 0122') >= 0, cardText(rows[0]));
  check(name + ': search shown', page.searchEl().hidden === false);
  var search = page.searchEl();
  search.value = 'reef';
  search.dispatch('input', {});
  var visible = rows.filter(function (r) { return !r.hidden; });
  var want = lamps.filter(function (l) { return l.name.toLowerCase().indexOf('reef') >= 0; }).length;
  eq(name + ': search filters names', visible.length, want);
  search.value = 'group 11';
  search.dispatch('input', {});
  visible = rows.filter(function (r) { return !r.hidden; });
  eq(name + ': search filters by group', visible.length, 12);
  eq(name + ': empty sections hidden', sections.map(function (s) { return s.hidden; }), [true, false, true]);
  search.value = '';
  search.dispatch('input', {});
  var before = page.doc.created;
  var firstRow = rows[0];
  page.doc.created = 0;
  await page.refresh();
  eq(name + ': identical poll creates nothing', page.doc.created, 0);
  check(name + ': identical poll keeps rows', page.lampsEl().querySelectorAll('[data-kid]')[0] === firstRow);
  check(name + ': creation counter works', before > 0);
}

async function scenarioTracks() {
  var name = 'coloured slider tracks';
  var server = stub.makeServer();
  var page = await newPage(server);
  var ranges = page.progRoot().querySelectorAll('input').filter(function (el) {
    return el.type === 'range' && el.style.getPropertyValue('--trk');
  });
  eq(name + ': one track per channel', ranges.length, 4);
  check(name + ': INT track', ranges[0].style.getPropertyValue('--trk').indexOf('#ffffff') >= 0);
  check(name + ': CLR track uses the strip stops', ranges[1].style.getPropertyValue('--trk').indexOf('#102030') >= 0);
  check(name + ': R track uses the overlay', ranges[2].style.getPropertyValue('--trk').indexOf('#ff0000') >= 0);
  check(name + ': AM track uses the overlay', ranges[3].style.getPropertyValue('--trk').indexOf('#ffaa00') >= 0);
  check(name + ': thumb has no fixed track colour', ranges[3].style.background === undefined || !ranges[3].style.background);
}

async function settleSnap(page) {
  // One tick fires the throttled snap, the next fires the trailing point preview.
  page.clock.tick(120);
  await stub.settle();
  page.clock.tick(120);
  await stub.settle();
}

async function scenarioWheelDrag() {
  var name = 'colour wheel snap';
  var server = stub.makeServer();
  var page = await newPage(server);
  var wheel = page.wheel();
  check(name + ': wheel present', !!wheel);
  var card = page.wheelCard();
  check(name + ': wheel card visible', !!card && card.hidden === false);
  check(name + ': strip is gone', page.progRoot().querySelector('.cstrip') === null && page.progRoot().querySelector('.cthumb') === null);
  var pre = slots(server);
  server.resetRequests();
  page.pointer(wheel, 'pointerdown', { button: 0, pointerId: 5, clientX: 160, clientY: 110 });
  page.pointer(wheel, 'pointermove', { pointerId: 5, clientX: 150, clientY: 90 });
  page.pointer(wheel, 'pointerup', { pointerId: 5 });
  await settleSnap(page);
  var snaps = server.snapRequests();
  check(name + ': one snap request', snaps.length === 1, 'got ' + snaps.length);
  if (snaps[0]) {
    check(name + ': snap sends the picked rgb', /^[0-9a-f]{6}$/.test(snaps[0].params.rgb), snaps[0].params.rgb);
    eq(name + ': snap gid', snaps[0].params.gid, '2');
    check(name + ': snap is a GET', snaps[0].method === 'GET', snaps[0].method);
  }
  var points = server.pointRequests();
  check(name + ': one point request', points.length === 1, 'got ' + points.length);
  if (points[0]) {
    eq(name + ': snapped channels sent', {
      gid: points[0].params.gid, slot: points[0].params.slot,
      c2: points[0].params.c2, c4: points[0].params.c4, c5: points[0].params.c5,
      c1: points[0].params.c1
    }, { gid: '2', slot: '1', c2: '100', c4: '20', c5: '10', c1: undefined });
  }
  eq(name + ': local slot follows the answer', server.group.slots[0].ch, [30, 100, 0, 20, 10]);
  check(name + ': lamp swatch shows the result', page.progRoot().textContent.indexOf('#403020') >= 0);
  check(name + ': closest wording', page.progRoot().textContent.indexOf('Closest this lamp can make') >= 0);
  check(name + ': INT unchanged', server.group.slots[0].ch[0] === pre[0].ch[0]);
  check(name + ': dragged', JSON.stringify(pre) !== JSON.stringify(slots(server)));

  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': undo restores', slots(server), pre);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);
}

async function scenarioWheelHexAndKeys() {
  var name = 'colour wheel hex and keys';
  var server = stub.makeServer();
  var page = await newPage(server);
  var input = page.hexInput();
  var snap = page.snapButton();
  check(name + ': colour field present', !!input);
  check(name + ': Snap button present', !!snap);
  check(name + ': field labelled Colour', !!input && input.getAttribute('aria-label') === 'Colour');
  server.resetRequests();
  input.value = '#00ff00';
  page.click(snap);
  await settleSnap(page);
  var snaps = server.snapRequests();
  check(name + ': one snap for the typed colour', snaps.length === 1, 'got ' + snaps.length);
  check(name + ': typed rgb sent', !!snaps[0] && snaps[0].params.rgb === '00ff00', snaps[0] && snaps[0].params.rgb);
  check(name + ': point sent', server.pointRequests().length === 1, server.pointRequests().length);

  var wheel = page.wheel();
  wheel.focus();
  var before = input.value;
  server.resetRequests();
  wheel.dispatch('keydown', { key: 'ArrowRight' });
  await settleSnap(page);
  check(name + ': arrow sent a snap', server.snapRequests().length === 1, server.snapRequests().length);
  check(name + ': arrow moved the target', input.value !== before, before + ' -> ' + input.value);
  check(name + ': arrow point sent', server.pointRequests().length === 1, server.pointRequests().length);

  server.resetRequests();
  input.value = 'not a colour';
  page.click(snap);
  await stub.settle();
  check(name + ': invalid colour sends nothing', server.snapRequests().length === 0, server.snapRequests().length);
  check(name + ': invalid colour explains', page.progRoot().textContent.indexOf('Enter a colour like #ff8800') >= 0);
}

async function scenarioWheelHiddenGeneric() {
  var name = 'wheel hidden for generic';
  var group = makeGroup(2, 'Generic', 'generic', ['ui1', 'ui2', 'ui3', 'ui4', 'ui5']);
  var server = stub.makeServer({ group: group, groups: [group] });
  var page = await newPage(server);
  var card = page.wheelCard();
  check(name + ': card exists', !!card);
  check(name + ': card hidden', !!card && card.hidden === true);
  server.resetRequests();
  var wheel = page.wheel();
  page.pointer(wheel, 'pointerdown', { button: 0, pointerId: 9, clientX: 150, clientY: 110 });
  page.clock.tick(120);
  await stub.settle();
  check(name + ': no snap for generic', server.snapRequests().length === 0, server.snapRequests().length);
}

async function scenarioWheelSliderSync() {
  var name = 'wheel follows sliders';
  var server = stub.makeServer();
  var page = await newPage(server);
  var before = page.progRoot().textContent;
  server.resetRequests();
  var ranges = page.progRoot().querySelectorAll('input').filter(function (el) {
    return el.type === 'range' && el.style.getPropertyValue('--trk');
  });
  var clr = ranges[1];
  clr.value = '255';
  clr.dispatch('input', {});
  page.clock.tick(120);
  await stub.settle();
  check(name + ': slider still sends a point', server.pointRequests().length === 1, server.pointRequests().length);
  check(name + ': slider sends no snap', server.snapRequests().length === 0, server.snapRequests().length);
  check(name + ': lamp swatch changed', page.progRoot().textContent !== before);
}

async function scenarioLightCard() {
  var name = 'light program card';
  var light = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var bus = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var lamp = { kid: 1, type: 1, gid: 2, name: 'A' };
  var server = stub.makeServer({ group: light, groups: [light, bus], lamps: [lamp], writes: false });
  var page = await newPage(server);
  var cards = page.groupsEl().children.filter(function (el) { return el.className === 'card'; });
  var card = cards.filter(function (el) { return cardText(el).indexOf('Board LED') >= 0; })[0];
  check(name + ': light card exists', !!card);
  check(name + ': light tag', !!card && cardText(card).indexOf('Light program') >= 0, card && cardText(card));
  check(name + ': no lamp chips', !!card && card.querySelectorAll('.chip').length === 0);
  var find = null;
  if (card) {
    card.querySelectorAll('button').forEach(function (b) {
      if (!find && b.textContent.trim() === 'Find these lamps') find = b;
    });
  }
  check(name + ': no find button rows', !find || find.parentNode.hidden === true);
  var banner = page.doc.getElementById('writes-banner');
  check(name + ': mixed banner shown', banner.hidden === false);
  check(name + ': mixed banner wording', page.doc.getElementById('writes-text').textContent.indexOf('Lamps on the K-Link bus are not being sent to yet') >= 0,
    page.doc.getElementById('writes-text').textContent);

  var only = stub.makeServer({ group: light, groups: [light], writes: false });
  var onlyPage = await newPage(only);
  check(name + ': all-light banner hidden', onlyPage.doc.getElementById('writes-banner').hidden === true);

  var orphans = page.groupsEl().children.filter(function (el) { return el.className === 'card orphan'; });
  check(name + ': light program is no orphan', orphans.length === 0, orphans.length);
  var selects = page.lampsEl().querySelectorAll('select');
  var targets = selects.length ? selects[0].children.map(function (o) { return o.value; }) : [];
  check(name + ': light program is no move target', targets.indexOf('101') < 0, targets.join(','));
  check(name + ': bus group is a move target', targets.indexOf('2') >= 0, targets.join(','));
}

async function presetFromPage(page, label) {
  page.click(buttonExact(page, label));
  page.click(buttonExact(page, 'Yes'));
  await stub.settle();
}

function noZeroColourAtInt(slotsArr) {
  for (var i = 0; i < slotsArr.length; i++) {
    var s = slotsArr[i];
    if (s.enabled && s.ch[0] > 0 && Math.max(s.ch[2], s.ch[3], s.ch[4]) === 0) return false;
  }
  return true;
}

async function scenarioLightPresets() {
  var name = 'light presets';
  var rgb = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: rgb, groups: [rgb] });
  var page = await newPage(server);
  await presetFromPage(page, 'Always on');
  eq(name + ': RGB Always on is white at full INT', server.group.slots[0].ch, [255, 0, 100, 100, 100]);
  check(name + ': RGB never black at INT>0', noZeroColourAtInt(server.group.slots));

  await presetFromPage(page, 'Moonlight');
  eq(name + ': RGB Moonlight is dim and warm', server.group.slots[0].ch, [51, 0, 100, 50, 20]);
  check(name + ': RGB moonlight never black', noZeroColourAtInt(server.group.slots));

  await presetFromPage(page, 'Daytime plateau');
  eq(name + ': RGB plateau white at the peak', server.group.slots[1].ch, [255, 0, 100, 100, 100]);
  eq(name + ': RGB plateau dark at the start', server.group.slots[0].ch, [0, 0, 100, 100, 100]);

  var rgbw = makeLight(101, 'Board LED', 'light_rgbw', ['int', 'w', 'r', 'g', 'b']);
  var wServer = stub.makeServer({ group: rgbw, groups: [rgbw] });
  var wPage = await newPage(wServer);
  await presetFromPage(wPage, 'Always on');
  eq(name + ': RGBW Always on uses the white channel', wServer.group.slots[0].ch, [255, 255, 100, 100, 100]);
  await presetFromPage(wPage, 'Moonlight');
  eq(name + ': RGBW Moonlight has no white', wServer.group.slots[0].ch, [51, 0, 100, 50, 20]);

  var ct = makeLight(102, 'Hallway', 'light_ct', ['int', 'ct', null, null, null]);
  var cServer = stub.makeServer({ group: ct, groups: [ct] });
  var cPage = await newPage(cServer);
  await presetFromPage(cPage, 'Always on');
  eq(name + ': CT Always on is neutral', cServer.group.slots[0].ch.slice(0, 2), [255, 128]);
  await presetFromPage(cPage, 'Moonlight');
  eq(name + ': CT Moonlight is warm', cServer.group.slots[0].ch.slice(0, 2), [51, 0]);
}

async function scenarioLightDirect() {
  var name = 'light direct colour';
  var rgb = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: rgb, groups: [rgb] });
  var page = await newPage(server);
  var card = page.wheelCard();
  check(name + ': picker shown', !!card && card.hidden === false);
  check(name + ': wheel used as a direct disc', !!page.wheel());
  check(name + ': single Colour swatch', page.progRoot().textContent.indexOf('Colour') >= 0);
  var sws = page.progRoot().querySelectorAll('.wsw');
  var lampLab = sws.filter(function (el) { return cardText(el).indexOf('Lamp makes') >= 0; })[0];
  check(name + ': no Lamp makes swatch', !lampLab || lampLab.hidden === true);

  var input = page.hexInput();
  var pre = slots(server);
  server.resetRequests();
  input.value = '#00ff00';
  page.click(page.snapButton());
  await settleSnap(page);
  check(name + ': no snap request', server.snapRequests().length === 0, server.snapRequests().length);
  var pts = server.pointRequests();
  check(name + ': one point request', pts.length === 1, pts.length);
  if (pts[0]) {
    eq(name + ': r,g,b sent directly', {
      c3: pts[0].params.c3, c4: pts[0].params.c4, c5: pts[0].params.c5, c1: pts[0].params.c1
    }, { c3: '0', c4: '100', c5: '0', c1: undefined });
  }
  eq(name + ': green applied', server.group.slots[0].ch, [20, 0, 0, 100, 0]);
  check(name + ': INT unchanged by the picker', server.group.slots[0].ch[0] === pre[0].ch[0]);

  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': undo restores the colour', slots(server), pre);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);

  var wheel = page.wheel();
  pre = slots(server);
  server.resetRequests();
  page.pointer(wheel, 'pointerdown', { button: 0, pointerId: 3, clientX: 170, clientY: 110 });
  page.pointer(wheel, 'pointermove', { pointerId: 3, clientX: 160, clientY: 100 });
  page.pointer(wheel, 'pointerup', { pointerId: 3 });
  page.clock.tick(120);
  await stub.settle();
  check(name + ': drag sends no snap', server.snapRequests().length === 0, server.snapRequests().length);
  pts = server.pointRequests();
  check(name + ': drag sends one point', pts.length === 1, pts.length);
  if (pts[0]) check(name + ': drag sends rgb channels', pts[0].params.c3 != null && pts[0].params.c4 != null && pts[0].params.c5 != null);
  check(name + ': drag changed the program', JSON.stringify(slots(server)) !== JSON.stringify(pre));
  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': one undo per drag', slots(server), pre);
  check(name + ': drag undo one request', server.pointRequests().length === 1, server.pointRequests().length);
}

async function scenarioLightCt() {
  var name = 'light CT strip';
  var ct = makeLight(102, 'Hallway', 'light_ct', ['int', 'ct', null, null, null]);
  var server = stub.makeServer({ group: ct, groups: [ct] });
  var page = await newPage(server);
  var strip = page.progRoot().querySelector('.cstrip');
  check(name + ': strip present', !!strip && strip.hidden === false);
  check(name + ': wheel hidden', page.wheel() === null || page.wheel().parentNode.hidden === true);
  check(name + ': picker visible', page.wheelCard().hidden === false);
  if (!strip) return;
  strip.clientWidth = 100;
  strip._left = 0;
  var pre = slots(server);
  server.resetRequests();
  page.pointer(strip, 'pointerdown', { button: 0, pointerId: 4, clientX: 100, clientY: 10 });
  page.pointer(strip, 'pointerup', { pointerId: 4 });
  page.clock.tick(120);
  await stub.settle();
  check(name + ': no snap request', server.snapRequests().length === 0, server.snapRequests().length);
  var pts = server.pointRequests();
  check(name + ': one point request', pts.length === 1, pts.length);
  if (pts[0]) eq(name + ': strip sets c2', pts[0].params.c2, '255');
  eq(name + ': cool end applied', server.group.slots[0].ch.slice(0, 2), [20, 255]);
  var thumb = page.progRoot().querySelector('.cthumb');
  check(name + ': thumb follows UI2', !!thumb && thumb.style.left === '100%', thumb && thumb.style.left);

  server.resetRequests();
  page.pointer(strip, 'pointerdown', { button: 0, pointerId: 5, clientX: 0, clientY: 10 });
  page.pointer(strip, 'pointerup', { pointerId: 5 });
  page.clock.tick(120);
  await stub.settle();
  eq(name + ': warm end applied', server.group.slots[0].ch.slice(0, 2), [20, 0]);

  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': first undo steps back one drag', server.group.slots[0].ch.slice(0, 2), [20, 255]);
  check(name + ': undo one request', server.pointRequests().length === 1, server.pointRequests().length);
  server.resetRequests();
  page.click(buttonExact(page, 'Undo'));
  await stub.settle();
  eq(name + ': second undo restores the start', slots(server), pre);

  var dim = makeLight(103, 'Dim', 'light_brightness', ['int', null, null, null, null]);
  var dServer = stub.makeServer({ group: dim, groups: [dim] });
  var dPage = await newPage(dServer);
  check(name + ': brightness has no picker', dPage.wheelCard().hidden === true);
  check(name + ': brightness has no strip', dPage.progRoot().querySelector('.cstrip') === null);
}

function legendButton(page, label) {
  var legend = page.progRoot().querySelector('.legend');
  if (!legend) return null;
  var found = null;
  legend.children.forEach(function (b) {
    if (b.textContent.trim() === label) found = b;
  });
  return found;
}

async function scenarioLightColours() {
  var name = 'light channel colours';
  var rgb = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: rgb, groups: [rgb] });
  var page = await newPage(server);
  var intBtn = legendButton(page, 'INT'), gBtn = legendButton(page, 'G'), bBtn = legendButton(page, 'B');
  check(name + ': legend has INT, G and B', !!intBtn && !!gBtn && !!bBtn);
  function stroke(btn) {
    var line = btn && btn.querySelectorAll('line')[0];
    return line ? line.getAttribute('stroke') : '';
  }
  check(name + ': G uses its own token', stroke(gBtn) === 'var(--k-g)', stroke(gBtn));
  check(name + ': B uses its own token', stroke(bBtn) === 'var(--k-b)', stroke(bBtn));
  check(name + ': G differs from INT', stroke(gBtn) !== stroke(intBtn), stroke(gBtn) + ' / ' + stroke(intBtn));
  check(name + ': B differs from INT', stroke(bBtn) !== stroke(intBtn));
  var ranges = page.progRoot().querySelectorAll('input').filter(function (el) {
    return el.type === 'range' && el.style.getPropertyValue('--trk');
  });
  var trks = ranges.map(function (r) { return r.style.getPropertyValue('--trk'); });
  check(name + ': G track is green', trks.some(function (t) { return t.indexOf('#00ff00') >= 0; }), trks.join(' | '));
  check(name + ': B track is blue', trks.some(function (t) { return t.indexOf('#0000ff') >= 0; }), trks.join(' | '));
  check(name + ': tracks differ from INT', trks[0].indexOf('#ffffff') >= 0 && trks.filter(function (t) { return t === trks[0]; }).length === 1,
    trks.join(' | '));
}

async function scenarioEffectsRow() {
  var name = 'effects row';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.effects = ['Storm', 'Cloud', 'Aurora'];
  var light = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: sun, groups: [sun, light] });
  var page = await newPage(server);
  var card = dashCard(page, 'Tuna Sun'), lcard = dashCard(page, 'Board LED');
  check(name + ': row shown for a Tuna group', !!card && fxRow(card).hidden === false);
  eq(name + ': one button per effect', fxButtons(card).map(function (b) { return b.textContent.trim(); }),
    ['Storm', 'Cloud', 'Aurora']);
  check(name + ': start label is accessible', fxNamed(card, 'Storm').getAttribute('aria-label') === 'Start Storm');
  check(name + ': light program has no row', !!lcard && fxRow(lcard).hidden === true);
  check(name + ': light program has no buttons', fxButtons(lcard).length === 0);
  check(name + ': speed group labelled', fxRow(card).querySelector('.fxseg').getAttribute('aria-label') === 'Effect speed');
  check(name + ': duration labelled', fxDuration(card).getAttribute('aria-label') === 'Effect duration');
  check(name + ': initial speed from the server', fxSpeed(card, 'Fast').getAttribute('aria-pressed') === 'true');
  eq(name + ': duration choices', fxDuration(card).children.map(function (o) { return o.value; }),
    ['0', '0.5', '1', '2', '3', '4', '5']);
  check(name + ': initial duration from the server', fxDuration(card).value === '2', fxDuration(card).value);
  check(name + ': stop hidden while idle', fxRow(card).querySelector('.danger').hidden === true);
}

async function scenarioEffectStart() {
  var name = 'start effect';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.effects = ['Storm', 'Cloud', 'Aurora'];
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  var card = dashCard(page, 'Tuna Sun');
  server.resetRequests();
  page.click(fxNamed(card, 'Storm'));
  await stub.settle();
  var reqs = server.effectRequests();
  check(name + ': one effect request', reqs.length === 1, reqs.length);
  if (reqs[0]) {
    eq(name + ': request payload', {
      gid: reqs[0].params.gid, name: reqs[0].params.name,
      speed: reqs[0].params.speed, minutes: reqs[0].params.minutes
    }, { gid: '2', name: 'Storm', speed: 'fast', minutes: '2' });
  }
  check(name + ': running button highlighted', fxNamed(card, 'Storm').getAttribute('aria-pressed') === 'true');
  check(name + ': others not highlighted', fxNamed(card, 'Cloud').getAttribute('aria-pressed') === 'false');
  check(name + ': live text', cardText(fxRow(card)).indexOf('Storm · 2:00 left') >= 0, cardText(fxRow(card)));
  check(name + ': stop shown while running', fxRow(card).querySelector('.danger').hidden === false);
  check(name + ': next-start hint shown', cardText(fxRow(card)).indexOf('apply to the next effect') >= 0);
  check(name + ': live output says effect', cardText(card).indexOf('Effect running') >= 0);
  check(name + ': mode unchanged', card.children[2].querySelector('button').getAttribute('aria-pressed') === 'true');
  eq(name + ': server mode unchanged', server.group.mode, 'program');

  /* Countdown runs locally between polls, without another request. */
  server.resetRequests();
  page.clock.tick(1000);
  check(name + ': one second off', cardText(fxRow(card)).indexOf('Storm · 1:59 left') >= 0, cardText(fxRow(card)));
  check(name + ': tick sends no request', server.requests.length === 0, server.requests.length);
}

async function scenarioEffectEnds() {
  var name = 'effect countdown ends';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.effects = ['Storm'];
  sun.effect_minutes = 0.5;
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  var card = dashCard(page, 'Tuna Sun');
  page.click(fxNamed(card, 'Storm'));
  await stub.settle();
  check(name + ': half a minute on the clock', cardText(fxRow(card)).indexOf('Storm · 0:30 left') >= 0,
    cardText(fxRow(card)));
  page.clock.tick(30000);
  await stub.settle();
  eq(name + ': server effect expired', server.group.effect, null);
  check(name + ': live text cleared', cardText(fxRow(card)).indexOf('left') < 0, cardText(fxRow(card)));
  check(name + ': nothing highlighted', fxNamed(card, 'Storm').getAttribute('aria-pressed') === 'false');
  check(name + ': stop hidden again', fxRow(card).querySelector('.danger').hidden === true);
  check(name + ': live output back to the schedule', cardText(card).indexOf('Effect running') < 0);
  page.clock.tick(5000);
  await stub.settle();
  check(name + ': never shows a negative time', cardText(fxRow(card)).indexOf('-') < 0, cardText(fxRow(card)));
  eq(name + ': mode still program', server.group.mode, 'program');
}

async function scenarioEffectStop() {
  var name = 'stop effect';
  var blue = makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  blue.effects = ['Storm', 'Rainbow'];
  var server = stub.makeServer({ group: blue, groups: [blue] });
  var page = await newPage(server);
  var card = dashCard(page, 'Tuna Blue');
  page.click(fxNamed(card, 'Rainbow'));
  await stub.settle();
  server.resetRequests();
  page.click(fxRow(card).querySelector('.danger'));
  await stub.settle();
  var stops = server.stopRequests();
  check(name + ': one stop request', stops.length === 1, stops.length);
  check(name + ': stop sends the group', !!stops[0] && stops[0].params.gid === '1', stops[0] && stops[0].params.gid);
  eq(name + ': server effect stopped', server.group.effect, null);
  check(name + ': no highlight after stop', fxNamed(card, 'Rainbow').getAttribute('aria-pressed') === 'false');
  check(name + ': stop hidden after stop', fxRow(card).querySelector('.danger').hidden === true);
  eq(name + ': mode untouched', server.group.mode, 'program');
}

async function scenarioEffectOptions() {
  var name = 'effect options';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.effects = ['Storm', 'Cloud'];
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  var card = dashCard(page, 'Tuna Sun');
  var sel = fxDuration(card), storm = fxNamed(card, 'Storm');
  page.click(fxSpeed(card, 'Slow'));
  sel.value = '1';
  sel.dispatch('change', {});
  check(name + ': slow selected', fxSpeed(card, 'Slow').getAttribute('aria-pressed') === 'true');
  check(name + ': fast cleared', fxSpeed(card, 'Fast').getAttribute('aria-pressed') === 'false');
  /* A poll must not reset what the user is choosing. */
  await page.refresh();
  check(name + ': speed kept across a poll', fxSpeed(card, 'Slow').getAttribute('aria-pressed') === 'true');
  check(name + ': duration kept across a poll', sel.value === '1', sel.value);
  check(name + ': same select node', fxDuration(card) === sel);
  check(name + ': same effect buttons', fxNamed(card, 'Storm') === storm);

  /* Focus is never stolen by a poll. */
  sel.focus();
  await page.refresh();
  check(name + ': focus kept', page.doc.activeElement === sel);
  check(name + ': focused value untouched', sel.value === '1', sel.value);

  server.resetRequests();
  page.click(storm);
  await stub.settle();
  var reqs = server.effectRequests();
  check(name + ': one start request', reqs.length === 1, reqs.length);
  if (reqs[0]) {
    eq(name + ': chosen options sent', {
      name: reqs[0].params.name, speed: reqs[0].params.speed, minutes: reqs[0].params.minutes
    }, { name: 'Storm', speed: 'slow', minutes: '1' });
  }
  eq(name + ': options persist with the group', [server.group.effect_speed, server.group.effect_minutes], [2, 1]);
  check(name + ': running with the chosen speed', fxSpeed(card, 'Slow').getAttribute('aria-pressed') === 'true');
}

async function main() {
  await scenarioPlace();
  await scenarioRefused();
  await scenarioAllUsed();
  await scenarioDragUndo();
  await scenarioAddUndo();
  await scenarioRemoveUndo();
  await scenarioCoalesce();
  await scenarioRevert();
  await scenarioDefaultPreset();
  await scenarioOrphans();
  await scenarioPicker();
  await scenarioManyLamps();
  await scenarioTracks();
  await scenarioWheelDrag();
  await scenarioWheelHexAndKeys();
  await scenarioWheelHiddenGeneric();
  await scenarioWheelSliderSync();
  await scenarioLightCard();
  await scenarioLightPresets();
  await scenarioLightDirect();
  await scenarioLightCt();
  await scenarioLightColours();
  await scenarioEffectsRow();
  await scenarioEffectStart();
  await scenarioEffectEnds();
  await scenarioEffectStop();
  await scenarioEffectOptions();
  console.log(failures ? failures + ' failure(s)' : 'all web tests passed');
  process.exit(failures ? 1 : 0);
}

main().catch(function (e) {
  console.error(e && e.stack || e);
  process.exit(1);
});
