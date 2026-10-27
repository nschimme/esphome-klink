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
/* Every channel curve in the chart, as the point pairs the drawing produced. */
function curvePoints(page) {
  var svg = page.svg(), out = [];
  (function walk(node) {
    for (var i = 0; i < node.children.length; i++) {
      var el = node.children[i];
      if (el.tagName === 'POLYLINE') out.push(el.getAttribute('points'));
      walk(el);
    }
  })(svg);
  return out.join('|');
}

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
function pickerArrows(page) {
  return picker(page).children.filter(function (el) { return el.tagName === 'BUTTON'; });
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
    ['Default', 'Daytime plateau', 'Always on']);
  check(name + ': Sunrise-sunset is gone', buttonExact(page, 'Sunrise-sunset') === null);
  check(name + ': Moonlight preset is gone', buttonExact(page, 'Moonlight') === null);
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
  check(name + ': documentation hint', cardText(card).indexOf('A program is created automatically') >= 0, cardText(card));
  check(name + ': program tab calls out the missing program',
    cardText(page.doc.getElementById('prog-orphans')).indexOf('Group 11') >= 0,
    cardText(page.doc.getElementById('prog-orphans')));
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
  var manage = card.children[2].children[1];
  check(name + ': manage lamps button', !!manage && manage.textContent.trim() === 'Manage lamps');
  page.click(manage);
  await stub.settle();
  eq(name + ': manage opens Settings', page.doc.body.getAttribute('data-tab'), 'settings');
  check(name + ': manage points at the lamp list', page.doc.getElementById('lamps-card')._scrolled === true);
  var orphanHint = page.doc.getElementById('settings-orphans');
  check(name + ': Settings names the group without a program', !!orphanHint && orphanHint.hidden === false,
    orphanHint && orphanHint.hidden);
  check(name + ': Settings explains how to add the program',
    cardText(orphanHint).indexOf('program is created automatically') >= 0, cardText(orphanHint));
  check(name + ': Settings hint names Group 11', cardText(orphanHint).indexOf('Group 11') >= 0, cardText(orphanHint));
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
  check(name + ': dropdown present', !!sel);
  check(name + ': labeled Lamp group', !!sel && sel.getAttribute('aria-label') === 'Lamp group');
  check(name + ': dropdown lists every group', !!sel && sel.children.length === 4, sel && sel.children.length);
  check(name + ': dropdown shows the first group', !!sel && sel.value === '1', sel && sel.value);
  var arrows = pickerArrows(page);
  check(name + ': previous and next arrows', arrows.length === 2, arrows.length);
  check(name + ': previous disabled on the first group', !!arrows[0] && arrows[0].disabled === true);
  check(name + ': next enabled on the first group', !!arrows[1] && arrows[1].disabled === false);
  if (sel) {
    sel.value = '3';
    sel.dispatch('change', {});
    await stub.settle();
    eq(name + ': change persists the group', page.storage('klink.prog.gid'), '3');
    check(name + ': panel follows the group', page.progRoot().textContent.indexOf('VIO') >= 0);
  }
  arrows = pickerArrows(page);
  check(name + ': arrows enabled in the middle', !!arrows[0] && arrows[0].disabled === false &&
    !!arrows[1] && arrows[1].disabled === false);
  page.click(arrows[1]);
  await stub.settle();
  eq(name + ': next arrow moves to group 4', pickerSelect(page).value, '4');
  arrows = pickerArrows(page);
  check(name + ': next disabled on the last group', !!arrows[1] && arrows[1].disabled === true);
  page.click(arrows[0]);
  await stub.settle();
  eq(name + ': previous arrow moves back', pickerSelect(page).value, '3');
  /* Shrinking to three groups keeps the same control, then grows back without errors. */
  var extra = gs.pop();
  await page.refresh();
  check(name + ': 3 groups keep the dropdown', !!pickerSelect(page));
  check(name + ': dropdown follows the smaller list', pickerSelect(page).children.length === 3,
    pickerSelect(page).children.length);
  gs.push(extra);
  await page.refresh();
  check(name + ': back to 4 groups', pickerSelect(page).children.length === 4);
}

async function scenarioFindLights() {
  var name = 'find lights on a light program';
  var lamp = makeLight(101, 'Hallway', 'light_ct', ['int', 'ct', null, null, null]);
  lamp.effects = ['Storm', 'Cloud', 'Aurora'];
  var server = stub.makeServer({ group: lamp, groups: [lamp] });
  var page = await newPage(server);
  var card = dashCard(page, 'Hallway');
  check(name + ': card is drawn', !!card);
  var buttons = card ? card.querySelectorAll('button') : [];
  var find = null;
  buttons.forEach(function (b) { if (!find && b.textContent.trim() === 'Find these lights') find = b; });
  check(name + ': offers Find these lights', !!find, card ? cardText(card) : '');
  check(name + ': no lamp count for a program', !!card && card.querySelector('.lamps').hidden === true);
  server.resetRequests();
  if (find) page.click(find);
  var ids = server.requests.filter(function (r) { return r.path === '/klink/identify'; });
  check(name + ': flashes the program', ids.length === 1 && ids[0].params.gid === '101', JSON.stringify(ids));
  check(name + ': lists its effects', !!card && fxButtons(card).length === 3, card ? fxButtons(card).length : 'no card');
}

async function scenarioRefugium() {
  var name = 'refugium group';
  var r = makeGroup(26, 'Refugium', 'refugium', ['int', 'clr', null, 'w', 'grn']);
  r.max = [255, 240, 100, 100, 100];
  r.slots = [
    { minute: 480, ch: [255, 240, 0, 50, 10], enabled: true },
    { minute: 1200, ch: [255, 240, 0, 50, 10], enabled: true }
  ];
  r.now_minute = 600;
  var server = stub.makeServer({ group: r, groups: [r] });
  var page = await newPage(server);
  var card = dashCard(page, 'Refugium');
  check(name + ': card is drawn', !!card);
  var text = card ? cardText(card) : '';
  check(name + ': CLR is a percent of its 240 limit', text.indexOf('CLR 100%') >= 0, text);
  check(name + ': shows the white and green channels', text.indexOf('W 50%') >= 0 && text.indexOf('GRN 10%') >= 0, text);
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

async function scenarioMoveBackToDefaultGroup() {
  var name = 'move back to default group';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var lamp = { kid: 18000001, type: 1, gid: 3, name: 'Follower' };
  var server = stub.makeServer({ group: sun, groups: [sun], lamps: [lamp] });
  var page = await newPage(server);
  var sel = page.doc.getElementById('lamp-group-18000001');
  check(name + ': group select exists', !!sel);
  var targets = sel ? sel.children.map(function (o) { return o.value; }) : [];
  check(name + ': default GID 1 is offered', targets.indexOf('1') >= 0, targets.join(','));
  check(name + ': another type default is not offered', targets.indexOf('26') < 0, targets.join(','));
  if (sel) {
    sel.value = '1';
    sel.dispatch('change', {});
    var row = page.lampsEl().querySelector('[data-kid="18000001"]');
    var move = null;
    row.querySelectorAll('button').forEach(function (b) { if (b.textContent.trim() === 'Move') move = b; });
    check(name + ': move button exists', !!move);
    page.click(move);
    await stub.settle();
    var reqs = server.requests.filter(function (r) { return r.path === '/klink/assign'; });
    eq(name + ': move sends the default GID', reqs.length ? reqs[0].params : null,
      { kid: '18000001', gid: '1' });
    eq(name + ': the lamp moved back', lamp.gid, 1);
  }

  var refugiumLamp = { kid: 18000002, type: 3, gid: 3, name: '' };
  var refugiumServer = stub.makeServer({ group: sun, groups: [sun], lamps: [refugiumLamp] });
  var refugiumPage = await newPage(refugiumServer);
  var refugiumSel = refugiumPage.doc.getElementById('lamp-group-18000002');
  var refugiumTargets = refugiumSel ? refugiumSel.children.map(function (o) { return o.value; }) : [];
  check(name + ': Refugium default GID 26 is offered', refugiumTargets.indexOf('26') >= 0,
    refugiumTargets.join(','));
  check(name + ': Tuna default GID 1 is not offered to a Refugium',
    refugiumTargets.indexOf('1') < 0, refugiumTargets.join(','));
}

async function scenarioTracks() {
  var name = 'colored slider tracks';
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
  check(name + ': thumb has no fixed track color', ranges[3].style.background === undefined || !ranges[3].style.background);
}

async function settleSnap(page) {
  // One tick fires the throttled snap, the next fires the trailing point preview.
  page.clock.tick(120);
  await stub.settle();
  page.clock.tick(120);
  await stub.settle();
}

async function scenarioWheelDrag() {
  var name = 'color wheel snap';
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
  var name = 'color wheel hex and keys';
  var server = stub.makeServer();
  var page = await newPage(server);
  var input = page.hexInput();
  var snap = page.snapButton();
  check(name + ': color field present', !!input);
  check(name + ': Snap button present', !!snap);
  check(name + ': field labeled Color', !!input && input.getAttribute('aria-label') === 'Color');
  server.resetRequests();
  input.value = '#00ff00';
  page.click(snap);
  await settleSnap(page);
  var snaps = server.snapRequests();
  check(name + ': one snap for the typed color', snaps.length === 1, 'got ' + snaps.length);
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
  input.value = 'not a color';
  page.click(snap);
  await stub.settle();
  check(name + ': invalid color sends nothing', server.snapRequests().length === 0, server.snapRequests().length);
  check(name + ': invalid color explains', page.progRoot().textContent.indexOf('Enter a color like #ff8800') >= 0);
}

async function scenarioWheelHiddenBrightness() {
  var name = 'wheel hidden for a brightness light';
  var group = makeLight(2, 'Lamp', 'light_brightness', ['int', null, null, null, null]);
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
  check(name + ': no snap for a brightness light', server.snapRequests().length === 0, server.snapRequests().length);
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

function noZeroColorAtInt(slotsArr) {
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
  check(name + ': RGB never black at INT>0', noZeroColorAtInt(server.group.slots));

  await presetFromPage(page, 'Daytime plateau');
  eq(name + ': RGB plateau white at the peak', server.group.slots[1].ch, [255, 0, 100, 100, 100]);
  eq(name + ': RGB plateau dark at the start', server.group.slots[0].ch, [0, 0, 100, 100, 100]);

  var rgbw = makeLight(101, 'Board LED', 'light_rgbw', ['int', 'w', 'r', 'g', 'b']);
  var wServer = stub.makeServer({ group: rgbw, groups: [rgbw] });
  var wPage = await newPage(wServer);
  await presetFromPage(wPage, 'Always on');
  eq(name + ': RGBW Always on uses the white channel', wServer.group.slots[0].ch, [255, 255, 100, 100, 100]);

  var ct = makeLight(102, 'Hallway', 'light_ct', ['int', 'ct', null, null, null]);
  var cServer = stub.makeServer({ group: ct, groups: [ct] });
  var cPage = await newPage(cServer);
  await presetFromPage(cPage, 'Always on');
  eq(name + ': CT Always on is neutral', cServer.group.slots[0].ch.slice(0, 2), [255, 128]);
}

async function scenarioLightDirect() {
  var name = 'light direct color';
  var rgb = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: rgb, groups: [rgb] });
  var page = await newPage(server);
  var card = page.wheelCard();
  check(name + ': picker shown', !!card && card.hidden === false);
  check(name + ': wheel used as a direct disc', !!page.wheel());
  check(name + ': single Color swatch', page.progRoot().textContent.indexOf('Color') >= 0);
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
  eq(name + ': undo restores the color', slots(server), pre);
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

async function scenarioLightColors() {
  var name = 'light channel colors';
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
  check(name + ': speed group labeled', fxRow(card).querySelector('.fxseg').getAttribute('aria-label') === 'Effect speed');
  check(name + ': duration labeled', fxDuration(card).getAttribute('aria-label') === 'Effect duration');
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

function outputCard(page, gid) { return page.doc.getElementById('output-' + gid); }
function accCard(page, gid) { return page.doc.getElementById('acc-card-' + gid); }

/* Sun and moon helpers. */
function sourceSeg(page) { return page.doc.getElementById('prog-source'); }
function sourceButton(page, label) {
  var seg = sourceSeg(page), found = null;
  if (!seg) return null;
  seg.children.forEach(function (b) { if (!found && b.textContent.trim() === label) found = b; });
  return found;
}
function dateButton(page, label) {
  var row = page.doc.getElementById('prog-dates'), found = null;
  if (!row) return null;
  row.children.forEach(function (b) {
    if (!found && b.tagName === 'BUTTON' && b.textContent.trim() === label) found = b;
  });
  return found;
}
function pad2(v) { return ('0' + v).slice(-2); }
function yearDate(month, day) { return new Date().getFullYear() + '-' + pad2(month) + '-' + pad2(day); }
async function finishPending(page) { page.clock.tick(200); await stub.settle(); }
function toastButton(page, label) {
  var found = null;
  page.doc.getElementById('toasts').querySelectorAll('button').forEach(function (b) {
    if (!found && b.textContent.trim() === label) found = b;
  });
  return found;
}

async function scenarioOutputCards() {
  var name = 'light behavior cards';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var light = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: sun, groups: [sun, light] });
  var page = await newPage(server);
  var card = outputCard(page, 2);
  check(name + ': card for a bus group', !!card);
  check(name + ': no card for a light program', outputCard(page, 101) === null);
  check(name + ': title', !!card && cardText(card).indexOf('Light behavior') >= 0, card && cardText(card));
  check(name + ': group name in the subtitle', !!card && cardText(page.doc.getElementById('output-group-2')).indexOf('Tuna Sun') >= 0);
  check(name + ': curve hint', cardText(page.doc.getElementById('output-curve-hint-2')).indexOf('dim end is easier to set') >= 0);
  check(name + ': transition hint', cardText(page.doc.getElementById('output-seconds-hint-2')).indexOf('0 changes instantly') >= 0);
  var curve = page.doc.getElementById('output-curve-2');
  eq(name + ': curve choices', curve.children.map(function (o) { return o.value; }), ['0', '1']);
  check(name + ': curve label tied', page.doc.getElementById('output-2').querySelectorAll('label')
    .some(function (l) { return l.getAttribute('for') === 'output-curve-2'; }));
  check(name + ': initial curve from state', curve.value === '0', curve.value);
  check(name + ': initial seconds wording', cardText(page.doc.getElementById('output-seconds-value-2')) === 'instant');
}

async function scenarioOutputSeconds() {
  var name = 'transition time';
  var server = stub.makeServer();
  var page = await newPage(server);
  var seconds = page.doc.getElementById('output-seconds-2');
  server.resetRequests();
  seconds.value = '2.5';
  seconds.dispatch('change', {});
  await stub.settle();
  var reqs = server.outputRequests();
  check(name + ': exactly one output request', reqs.length === 1, reqs.length);
  if (reqs[0]) {
    eq(name + ': only gid and seconds sent', reqs[0].params, { gid: '2', seconds: '2.5' });
    check(name + ': it is a POST', reqs[0].method === 'POST', reqs[0].method);
  }
  eq(name + ': server took the time', server.group.output.seconds, 2.5);
  check(name + ': value shown', cardText(page.doc.getElementById('output-seconds-value-2')) === '2.5 s');
}

async function scenarioOutputPoll() {
  var name = 'light behavior follows the poll';
  var server = stub.makeServer();
  var page = await newPage(server);
  server.group.output = { curve: 1, seconds: 4 };
  await page.refresh();
  var curve = page.doc.getElementById('output-curve-2');
  var seconds = page.doc.getElementById('output-seconds-2');
  check(name + ': curve reflects state', curve.value === '1', curve.value);
  check(name + ': seconds reflect state', seconds.value === '4', seconds.value);
  check(name + ': value wording', cardText(page.doc.getElementById('output-seconds-value-2')) === '4.0 s');
  server.resetRequests();
  curve.value = '0';
  curve.dispatch('change', {});
  await stub.settle();
  eq(name + ': curve change sends only the curve', server.outputRequests()[0] && server.outputRequests()[0].params,
    { gid: '2', curve: '0' });
}

async function scenarioAcclimationCards() {
  var name = 'acclimation cards';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var light = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: sun, groups: [sun, light] });
  var page = await newPage(server);
  var card = accCard(page, 2);
  check(name + ': card for a bus group', !!card);
  check(name + ': only the selected group has a card', accCard(page, 101) === null);
  check(name + ': title', !!card && cardText(card).indexOf('Acclimation') >= 0, card && cardText(card));
  check(name + ': group name in the subtitle', cardText(page.doc.getElementById('acc-group-2')).indexOf('Tuna Sun') >= 0);
  check(name + ': hint', cardText(page.doc.getElementById('acc-hint-2')).indexOf(
    'the whole program starts at a lower intensity and rises to full') >= 0);
  var on = page.doc.getElementById('acc-on-2');
  var start = page.doc.getElementById('acc-start-2');
  var days = page.doc.getElementById('acc-days-2');
  check(name + ': the switch is off to start', on.checked === false);
  eq(name + ': starting intensity range', { min: start.min, max: start.max, step: start.step },
    { min: '10', max: '90', step: '1' });
  eq(name + ': days range', { min: days.min, max: days.max, step: days.step },
    { min: '1', max: '90', step: '1' });
  check(name + ': sliders stay enabled while off', start.disabled === false && days.disabled === false);
  check(name + ': no status line while off', page.doc.getElementById('acc-status-2').hidden === true);
  eq(name + ': starting intensity wording', cardText(page.doc.getElementById('acc-start-value-2')), '30 %');
  eq(name + ': days wording', cardText(page.doc.getElementById('acc-days-value-2')), '14 days');

  var sel = page.doc.getElementById('settings-gid');
  check(name + ': group selector shown', sel.hidden === false);
  eq(name + ': group selector choices', sel.children.map(function (o) { return o.value; }), ['2', '101']);
  sel.value = '101';
  sel.dispatch('change', {});
  await stub.settle();
  check(name + ': card for the chosen light program', !!accCard(page, 101));
  check(name + ': old group card removed', accCard(page, 2) === null);
  check(name + ': chosen group name in the subtitle',
    cardText(page.doc.getElementById('acc-group-101')).indexOf('Board LED') >= 0);
}

async function scenarioAcclimationToggle() {
  var name = 'acclimation switch';
  var server = stub.makeServer();
  var page = await newPage(server);
  var on = page.doc.getElementById('acc-on-2');
  server.resetRequests();
  on.checked = true;
  on.dispatch('change', {});
  await stub.settle();
  var reqs = server.acclimationRequests();
  check(name + ': exactly one acclimation request', reqs.length === 1, reqs.length);
  if (reqs[0]) {
    eq(name + ': only gid and on sent', reqs[0].params, { gid: '2', on: '1' });
    check(name + ': it is a POST', reqs[0].method === 'POST', reqs[0].method);
  }
  check(name + ': server turned it on', server.group.acclimation.on === true);
  eq(name + ': it starts at the starting share', server.group.acclimation.share, 30);
  check(name + ': the switch stays on after the poll', on.checked === true);

  server.resetRequests();
  on.checked = false;
  on.dispatch('change', {});
  await stub.settle();
  reqs = server.acclimationRequests();
  check(name + ': turning off sends one request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': turning off sends zero', reqs[0].params, { gid: '2', on: '0' });
  eq(name + ': server is back to full light', server.group.acclimation.share, 100);
}

async function scenarioAcclimationFields() {
  var name = 'acclimation sliders';
  var server = stub.makeServer();
  var page = await newPage(server);
  var days = page.doc.getElementById('acc-days-2');
  server.resetRequests();
  days.value = '30';
  days.dispatch('change', {});
  await stub.settle();
  var reqs = server.acclimationRequests();
  check(name + ': one days request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': only gid and days sent', reqs[0].params, { gid: '2', days: '30' });
  eq(name + ': server took the days', server.group.acclimation.days, 30);
  eq(name + ': days wording', cardText(page.doc.getElementById('acc-days-value-2')), '30 days');

  server.resetRequests();
  var start = page.doc.getElementById('acc-start-2');
  start.value = '45';
  start.dispatch('change', {});
  await stub.settle();
  reqs = server.acclimationRequests();
  check(name + ': one start request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': only gid and start sent', reqs[0].params, { gid: '2', start: '45' });
  eq(name + ': server took the start', server.group.acclimation.start, 45);
}

async function scenarioAcclimationStatus() {
  var name = 'acclimation status';
  var server = stub.makeServer();
  var page = await newPage(server);
  server.group.acclimation = { on: true, start: 30, days: 14, share: 42 };
  await page.refresh();
  var status = page.doc.getElementById('acc-status-2');
  check(name + ': status shown while on', status.hidden === false);
  eq(name + ': share and days left', cardText(status),
    'Today at 42 % of the program. Full light in 12 days.');

  server.group.acclimation = { on: true, start: 30, days: 14, share: 99 };
  await page.refresh();
  eq(name + ': full light today', cardText(status),
    'Today at 99 % of the program. Full light today.');

  server.group.acclimation = { on: false, start: 30, days: 14, share: 100 };
  await page.refresh();
  check(name + ': nothing while off', status.hidden === true);
  eq(name + ': no status text while off', cardText(status), '');
}

async function scenarioSoftStart() {
  var name = 'soft start';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var light = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: sun, groups: [sun, light] });
  var page = await newPage(server);
  var soft = page.doc.getElementById('output-soft-start-2');
  check(name + ': slider on the Light behavior card', !!soft);
  check(name + ': no slider for a light program', page.doc.getElementById('output-soft-start-101') === null);
  eq(name + ': range and step', { min: soft.min, max: soft.max, step: soft.step },
    { min: '0', max: '120', step: '5' });
  eq(name + ': default shown', cardText(page.doc.getElementById('output-soft-start-value-2')), '30 s');
  check(name + ': hint', cardText(page.doc.getElementById('output-soft-start-hint-2')).indexOf(
    'the lamps fade in from dark over this time') >= 0);

  server.resetRequests();
  soft.value = '0';
  soft.dispatch('change', {});
  await stub.settle();
  var reqs = server.outputRequests();
  check(name + ': exactly one output request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': only gid and soft_start sent', reqs[0].params, { gid: '2', soft_start: '0' });
  eq(name + ': server took zero', server.group.output.soft_start, 0);
  eq(name + ': zero reads Off', cardText(page.doc.getElementById('output-soft-start-value-2')), 'Off');

  server.resetRequests();
  soft.value = '45';
  soft.dispatch('change', {});
  await stub.settle();
  reqs = server.outputRequests();
  if (reqs[0]) eq(name + ': only soft_start sent again', reqs[0].params, { gid: '2', soft_start: '45' });
  eq(name + ': seconds wording', cardText(page.doc.getElementById('output-soft-start-value-2')), '45 s');
}

async function scenarioAcclimationDashboard() {
  var name = 'acclimation on the dashboard';
  var server = stub.makeServer();
  server.group.acclimation = { on: true, start: 30, days: 14, share: 42 };
  var page = await newPage(server);
  var card = dashCard(page, 'Tuna Sun');
  check(name + ': card found', !!card);
  check(name + ': line shown', cardText(card).indexOf('Acclimating: 42 %') >= 0, card && cardText(card));
  server.group.acclimation = { on: false, start: 30, days: 14, share: 100 };
  await page.refresh();
  check(name + ': line gone when off', cardText(card).indexOf('Acclimating') < 0, cardText(card));
}

async function scenarioSourceControl() {
  var name = 'source control';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var light = makeLight(101, 'Board LED', 'light_rgb', ['int', null, 'r', 'g', 'b']);
  var server = stub.makeServer({ group: sun, groups: [sun, light] });
  var page = await newPage(server);
  var seg = sourceSeg(page);
  check(name + ': control present', !!seg);
  check(name + ': control visible for a bus group', !!seg && seg.hidden === false);
  eq(name + ': two choices', seg && seg.children.map(function (b) { return b.textContent.trim(); }),
    ['My points', 'Sky']);
  check(name + ': group labeled', !!seg && seg.getAttribute('aria-label') === 'Program source');
  check(name + ': My points pressed', sourceButton(page, 'My points').getAttribute('aria-pressed') === 'true');
  check(name + ': Sky not pressed', sourceButton(page, 'Sky').getAttribute('aria-pressed') === 'false');
  check(name + ': no date chips for My points', page.doc.getElementById('prog-dates').hidden === true);
  check(name + ': no sky panel for My points', page.doc.getElementById('prog-sky').hidden === true);

  var only = stub.makeServer({ group: light, groups: [light] });
  var onlyPage = await newPage(only);
  check(name + ': hidden for a light program', sourceSeg(onlyPage).hidden === true);
  check(name + ': no date chips for a light program', onlyPage.doc.getElementById('prog-dates').hidden === true);
}

async function scenarioChooseSun() {
  var name = 'choose the sky';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  server.resetRequests();
  page.click(sourceButton(page, 'Sky'));
  await stub.settle();
  var reqs = server.sourceRequests();
  check(name + ': exactly one source request', reqs.length === 1, reqs.length);
  if (reqs[0]) {
    eq(name + ': gid and source only', reqs[0].params, { gid: '2', source: 'sky' });
    check(name + ': it is a POST', reqs[0].method === 'POST', reqs[0].method);
  }
  eq(name + ': server switched', server.group.source, 1);
  check(name + ': poll reflects the source', sourceButton(page, 'Sky').getAttribute('aria-pressed') === 'true');
  check(name + ': My points cleared', sourceButton(page, 'My points').getAttribute('aria-pressed') === 'false');
  check(name + ': date chips appear', page.doc.getElementById('prog-dates').hidden === false);
  check(name + ': sky panel appears', page.doc.getElementById('prog-sky').hidden === false);
  check(name + ': Sun toggle on', page.doc.getElementById('sky-sun-on').checked === true);
  check(name + ': Moon toggle hidden for a Tuna Sun', page.doc.getElementById('sky-moon-on').parentNode.hidden === true);
}

async function scenarioGeneratedReadOnly() {
  var name = 'generated curve is read only';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  await finishPending(page);
  check(name + ': curve is drawn', page.svg() && page.svg().children.length > 0);
  eq(name + ': no point handles', page.svg().querySelectorAll('[data-slot]').length, 0);
  check(name + ': editor tools hidden', page.progRoot().querySelector('.ptools').hidden === true);
  check(name + ': point panel hidden', page.progRoot().querySelector('.panel').hidden === true);
  check(name + ': generated tag', page.doc.getElementById('prog-gen-tag').hidden === false);
  var hints = page.progRoot().querySelectorAll('.hint').filter(function (h) { return h.textContent.indexOf('points are used') >= 0; });
  check(name + ': no slots-full hint on a generated day', hints.length === 1 && hints[0].hidden === true);
  server.resetRequests();
  var svg = page.svg();
  page.pointer(svg, 'pointerdown', { button: 0, pointerId: 11, clientX: xFor(500), clientY: yFor(60) });
  page.pointer(svg, 'pointermove', { pointerId: 11, clientX: xFor(620), clientY: yFor(90) });
  page.pointer(svg, 'pointerup', { pointerId: 11 });
  page.clock.tick(120);
  await stub.settle();
  eq(name + ': dragging sends no point', server.pointRequests().length, 0);
  eq(name + ': dragging sends no preview', server.previewRequests().length, 0);
}

async function scenarioDateChip() {
  var name = 'preview date';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  await finishPending(page);
  var want = yearDate(6, 21);
  server.resetRequests();
  page.click(dateButton(page, 'Jun 21'));
  await stub.settle();
  var reqs = server.generatedRequests();
  check(name + ': one generated request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': right gid, source and date', reqs[0].params, { gid: '2', source: 'sky', date: want });
  check(name + ': pending answer is retried', true);
  await finishPending(page);
  reqs = server.generatedRequests();
  check(name + ': second request asks again', reqs.length === 2, reqs.length);
  if (reqs[1]) eq(name + ': same date on the retry', reqs[1].params.date, want);
  check(name + ': preview note shown', page.doc.getElementById('prog-preview-note').hidden === false);
  check(name + ': preview wording', cardText(page.doc.getElementById('prog-preview-note')).indexOf('Preview only. The lamps follow today.') >= 0);
  check(name + ': date chip marked', dateButton(page, 'Jun 21').getAttribute('aria-pressed') === 'true');
  page.click(dateButton(page, 'Today'));
  await stub.settle();
  check(name + ': Today hides the preview note', page.doc.getElementById('prog-preview-note').hidden === true);
}

async function scenarioSummaries() {
  var name = 'day summaries';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var sServer = stub.makeServer({ group: sun, groups: [sun] });
  var sPage = await newPage(sServer);
  await finishPending(sPage);
  var text = cardText(sPage.doc.getElementById('prog-summary'));
  eq(name + ': sun line', text, 'Sunrise 07:14 · sunset 17:56 · 10 h 42 min');

  var blue = makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  blue.source = 1;
  blue.sky = { layers: 2 };
  var mServer = stub.makeServer({ group: blue, groups: [blue] });
  var mPage = await newPage(mServer);
  await finishPending(mPage);
  text = cardText(mPage.doc.getElementById('prog-summary'));
  eq(name + ': moon line', text,
    'Moon 98% lit · rises 19:05 · sets 07:08 (estimated, within about 15 minutes)');

  var dark = makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  dark.source = 1;
  dark.sky = { layers: 2 };
  var dServer = stub.makeServer({
    group: dark, groups: [dark],
    generated: function () {
      return {
        points: [{ minute: 600, ch: [0, 100, 0, 0, 0], enabled: true }],
        summary: { rise: -1, set: -1, illumination: 0.1 }
      };
    }
  });
  var dPage = await newPage(dServer);
  await finishPending(dPage);
  eq(name + ': moon stays down', cardText(dPage.doc.getElementById('prog-summary')),
    'The moon is not up during the dark hours');

  var polar = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  polar.source = 1;
  var pServer = stub.makeServer({
    group: polar, groups: [polar],
    generated: function () {
      return {
        points: [{ minute: 720, ch: [200, 120, 0, 0, 0], enabled: true }],
        summary: { sunrise: -1, noon: 720, sunset: -1, length: 1440, always_up: true }
      };
    }
  });
  var pPage = await newPage(pServer);
  await finishPending(pPage);
  eq(name + ': polar day', cardText(pPage.doc.getElementById('prog-summary')), 'The sun stays up all day');
}

async function scenarioSkySettings() {
  var name = 'sky settings';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  await finishPending(page);
  check(name + ': peak field', !!page.doc.getElementById('sky-peak'));
  check(name + ': glow field', !!page.doc.getElementById('sky-glow'));
  eq(name + ': glow defaults to half strength', page.doc.getElementById('sky-glow').value, '50');
  eq(name + ': glow text says half strength', cardText(page.doc.getElementById('sky-glow-value')), '50%');
  check(name + ': settings sit beside the curve', page.doc.getElementById('prog-sky').parentNode.className.indexOf('pwrap') >= 0,
    page.doc.getElementById('prog-sky').parentNode.className);
  check(name + ': settings row has Undo', !!page.doc.getElementById('prog-sky-undo'));
  check(name + ': night glow for Tuna Sun', page.doc.getElementById('sky-amber').parentNode.hidden === false);
  check(name + ': Tuna Sun night glow is amber',
    cardText(page.doc.getElementById('prog-sky')).indexOf('A dim amber light through the night.') >= 0,
    cardText(page.doc.getElementById('prog-sky')));
  check(name + ': day length select', page.doc.getElementById('sky-hours-mode').children.length === 2);
  server.resetRequests();
  var peak = page.doc.getElementById('sky-peak');
  peak.value = '80';
  peak.dispatch('change', {});
  await stub.settle();
  var reqs = server.skyRequests();
  check(name + ': one sky request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': only peak sent', reqs[0].params, { gid: '2', peak: '80' });
  eq(name + ': server took the peak', server.group.sky.peak, 80);

  server.resetRequests();
  var mode = page.doc.getElementById('sky-hours-mode');
  mode.value = '1';
  mode.dispatch('change', {});
  await stub.settle();
  reqs = server.skyRequests();
  check(name + ': one day-length request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': only hours sent', reqs[0].params, { gid: '2', hours: '8' });
  eq(name + ': fixed length stored', server.group.sky.hours, 8);
  check(name + ': fixed slider shown', page.doc.getElementById('sky-hours').parentNode.hidden === false);

  var shift = page.doc.getElementById('sky-shift');
  var inDetails = false, anc = shift;
  while (anc) { if (anc.tagName === 'DETAILS') { inDetails = true; break; } anc = anc.parentNode; }
  check(name + ': time shift not hidden under More', !!shift && !inDetails);
  server.resetRequests();
  shift.value = '15';
  shift.dispatch('change', {});
  await stub.settle();
  reqs = server.skyRequests();
  check(name + ': one shift request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': only shift sent', reqs[0].params, { gid: '2', shift: '15' });
  eq(name + ': server took the shift', server.group.sky.shift, 15);

  var blue = makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  blue.source = 1;
  blue.sky = { layers: 2 };
  var bServer = stub.makeServer({ group: blue, groups: [blue] });
  var bPage = await newPage(bServer);
  await finishPending(bPage);
  check(name + ': sun settings hidden on a Moon day',
    bPage.doc.getElementById('sky-amber').parentNode.parentNode.hidden === true);
  check(name + ': moon slider', !!bPage.doc.getElementById('sky-moon'));
  check(name + ': phase checkbox', !!bPage.doc.getElementById('sky-phase'));
  check(name + ': time shift visible on a Moon day',
    !!bPage.doc.getElementById('sky-shift') && bPage.doc.getElementById('sky-shift').parentNode.parentNode.hidden === false);
  bServer.resetRequests();
  var phase = bPage.doc.getElementById('sky-phase');
  phase.checked = true;
  phase.dispatch('change', {});
  await stub.settle();
  reqs = bServer.skyRequests();
  if (reqs[0]) eq(name + ': phase sent alone', reqs[0].params, { gid: '1', phase: '1' });
}

async function scenarioSkyRamp() {
  var name = 'sun ramp';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  await finishPending(page);
  var ramp = page.doc.getElementById('sky-ramp');
  check(name + ': control present for a Sun day', !!ramp);
  if (!ramp) return;
  eq(name + ': range and step', { min: ramp.min, max: ramp.max, step: ramp.step },
    { min: '0', max: '240', step: '10' });
  check(name + ': label', cardText(ramp.parentNode).indexOf('Morning and evening ramp') >= 0,
    cardText(ramp.parentNode));
  check(name + ': hint', cardText(ramp.parentNode.parentNode).indexOf(
    'How long the light takes to climb from sunrise to near full, and to fall again before sunset. ' +
    'The real sun does it in under an hour; tanks usually like two to three hours.') >= 0);
  var shown = page.doc.getElementById('sky-ramp-value');
  eq(name + ': 150 shown in hours and minutes', cardText(shown), '2 h 30 min');
  eq(name + ': slider follows the state', ramp.value, '150');

  /* The poll fills the slider and the text back in when nobody is holding the control. */
  server.group.sky.ramp = 0;
  page.clock.tick(2100);
  await stub.settle();
  eq(name + ': zero means the real sun', cardText(shown), 'Real sun');
  eq(name + ': slider follows zero', ramp.value, '0');

  server.resetRequests();
  ramp.value = '150';
  ramp.dispatch('change', {});
  await stub.settle();
  var reqs = server.skyRequests();
  check(name + ': one ramp request', reqs.length === 1, reqs.length);
  if (reqs[0]) eq(name + ': only ramp sent', reqs[0].params, { gid: '2', ramp: '150' });
  eq(name + ': server took the ramp', server.group.sky.ramp, 150);

  var blue = makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  blue.source = 1;
  blue.sky = { layers: 2 };
  var bServer = stub.makeServer({ group: blue, groups: [blue] });
  var bPage = await newPage(bServer);
  await finishPending(bPage);
  var bRamp = bPage.doc.getElementById('sky-ramp');
  check(name + ': no ramp for a Moon day', bRamp.parentNode.parentNode.hidden === true);
}

/* A generated day whose peak follows the setting, so the chart must move when it changes. */
function peakDay(group) {
  return function (gid, source, date) {
    var peak = group.sky.peak | 0;
    return {
      points: [
        { minute: 480, ch: [0, 0, 0, 0, 0], enabled: true },
        { minute: 720, ch: [Math.round(peak * 2.55), 0, 0, 0, 0], enabled: true },
        { minute: 960, ch: [0, 0, 0, 0, 0], enabled: true }
      ],
      summary: { sunrise: 480, noon: 720, sunset: 960, length: 480, always_up: false }
    };
  };
}

async function scenarioSkyRedraw() {
  var name = 'sun settings redraw the curve';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun], generated: peakDay(sun) });
  var page = await newPage(server);
  await finishPending(page);
  var before = curvePoints(page);
  server.resetRequests();
  var peak = page.doc.getElementById('sky-peak');
  peak.value = '90';
  peak.dispatch('change', {});
  await stub.settle();
  page.clock.tick(400);
  await stub.settle();
  await finishPending(page);
  eq(name + ': slider keeps the new value', cardText(page.doc.getElementById('sky-peak-value')), '90%');
  check(name + ': today curve moved', curvePoints(page) !== before, curvePoints(page));
  eq(name + ': device took the peak', server.group.sky.peak, 90);
}

async function scenarioSkyPreviewRedraw() {
  var name = 'sun settings redraw a preview';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun], generated: peakDay(sun) });
  var page = await newPage(server);
  await finishPending(page);
  page.click(dateButton(page, 'Jun 21'));
  await stub.settle();
  await finishPending(page);
  var before = curvePoints(page);
  server.resetRequests();
  var peak = page.doc.getElementById('sky-peak');
  peak.value = '80';
  peak.dispatch('change', {});
  await stub.settle();
  page.clock.tick(400);
  await stub.settle();
  await finishPending(page);
  check(name + ': the preview was asked for again', server.generatedRequests().length >= 1,
    server.generatedRequests().length);
  check(name + ': preview curve moved', curvePoints(page) !== before, curvePoints(page));
  check(name + ': preview date kept', dateButton(page, 'Jun 21').getAttribute('aria-pressed') === 'true');
}

async function scenarioSkyHistory() {
  var name = 'sun settings undo, redo and revert';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  await finishPending(page);
  var undo = page.doc.getElementById('prog-sky-undo');
  var redo = page.doc.getElementById('prog-sky-redo');
  var revert = page.doc.getElementById('prog-sky-revert');
  check(name + ': undo starts disabled', !!undo && undo.disabled === true);
  check(name + ': redo starts disabled', !!redo && redo.disabled === true);
  check(name + ': revert hidden at the base', !!revert && revert.hidden === true);

  var peak = page.doc.getElementById('sky-peak');
  peak.value = '80';
  peak.dispatch('change', {});
  await stub.settle();
  eq(name + ': device took the change', server.group.sky.peak, 80);
  check(name + ': undo enabled', undo.disabled === false);
  check(name + ': revert shown', revert.hidden === false);

  server.resetRequests();
  page.click(undo);
  await stub.settle();
  page.clock.tick(400);
  await stub.settle();
  eq(name + ': undo posts the old peak', server.skyRequests()[0].params, { gid: '2', peak: '60' });
  eq(name + ': device back at the default', server.group.sky.peak, 60);
  check(name + ': redo enabled after undo', redo.disabled === false);
  check(name + ': revert hidden again', revert.hidden === true);

  page.click(redo);
  await stub.settle();
  page.clock.tick(400);
  await stub.settle();
  eq(name + ': redo puts it back', server.group.sky.peak, 80);

  page.click(revert);
  await stub.settle();
  page.clock.tick(400);
  await stub.settle();
  eq(name + ': revert returns to the base', server.group.sky.peak, 60);
  check(name + ': revert hidden after reverting', revert.hidden === true);
}

async function scenarioSunMoon() {
  var name = 'sky program';
  var blue = makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  blue.source = 1;
  blue.sky = { layers: 3 };
  var server = stub.makeServer({ group: blue, groups: [blue] });
  var page = await newPage(server);
  await finishPending(page);
  var sunToggle = page.doc.getElementById('sky-sun-on');
  var moonToggle = page.doc.getElementById('sky-moon-on');
  check(name + ': Sky pressed', sourceButton(page, 'Sky').getAttribute('aria-pressed') === 'true');
  check(name + ': Sun layer on', sunToggle.checked === true);
  check(name + ': Moon layer on', moonToggle.checked === true);
  check(name + ': moon settings visible', page.doc.getElementById('sky-moon').parentNode.parentNode.hidden === false);
  check(name + ': moon hint explains the overlay',
    cardText(page.doc.getElementById('prog-sky')).indexOf("The moon is added over the sun's dark hours") >= 0,
    cardText(page.doc.getElementById('prog-sky')));
  var text = cardText(page.doc.getElementById('prog-summary'));
  check(name + ': summary has the sun', text.indexOf('Sunrise') >= 0, text);
  check(name + ': summary has the moon', text.indexOf('Moon 98% lit') >= 0, text);

  /* Sun off: the same Sky program becomes a moon-only night light. */
  sunToggle.checked = false;
  sunToggle.dispatch('change', {});
  await stub.settle();
  await finishPending(page);
  eq(name + ': Sun off makes a moon-only layer', server.group.source, 1);
  eq(name + ': only the Moon layer is left', server.group.sky.layers, 2);
  check(name + ': sun settings hidden', page.doc.getElementById('sky-peak').parentNode.parentNode.hidden === true);
  check(name + ': moon settings still visible', page.doc.getElementById('sky-moon').parentNode.parentNode.hidden === false);
  check(name + ': no sun in the moon-only summary',
    cardText(page.doc.getElementById('prog-summary')).indexOf('Sunrise') < 0,
    cardText(page.doc.getElementById('prog-summary')));

  /* The last layer cannot be turned off: that would be no program at all. */
  server.resetRequests();
  moonToggle.checked = false;
  moonToggle.dispatch('change', {});
  await stub.settle();
  eq(name + ': both layers off sends nothing',
    server.sourceRequests().length + server.skyRequests().length, 0);
  check(name + ': the last layer is put back', moonToggle.checked === true);

  /* Moon on top of the sun again. */
  sunToggle.checked = true;
  sunToggle.dispatch('change', {});
  await stub.settle();
  await finishPending(page);
  eq(name + ': Sun on returns to both layers', server.group.sky.layers, 3);

  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var sServer = stub.makeServer({ group: sun, groups: [sun] });
  var sPage = await newPage(sServer);
  await finishPending(sPage);
  check(name + ': a Tuna Sun uses the Sky program', sourceButton(sPage, 'Sky').getAttribute('aria-pressed') === 'true');
  check(name + ': no Moon layer on a Tuna Sun', sPage.doc.getElementById('sky-moon-on').parentNode.hidden === true);
  check(name + ': no moon settings on a Tuna Sun',
    sPage.doc.getElementById('sky-moon').parentNode.parentNode.hidden === true);
  check(name + ': no moon in the Tuna Sun summary',
    cardText(sPage.doc.getElementById('prog-summary')).indexOf('Moon') < 0,
    cardText(sPage.doc.getElementById('prog-summary')));
}

async function scenarioSkyTracks() {
  var name = 'sun and moon color sliders';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun] });
  var page = await newPage(server);
  await finishPending(page);
  var glow = page.doc.getElementById('sky-glow');
  var peak = page.doc.getElementById('sky-peak');
  var amber = page.doc.getElementById('sky-amber');
  check(name + ': glow has a warm track', glow.className.indexOf('trk') >= 0 &&
    glow.style.getPropertyValue('--trk').indexOf('#ff8a3c') >= 0, glow.style.getPropertyValue('--trk'));
  check(name + ': peak has a bright track', peak.className.indexOf('trk') >= 0 &&
    peak.style.getPropertyValue('--trk').indexOf('#ffffff') >= 0, peak.style.getPropertyValue('--trk'));
  check(name + ': amber has an amber track', amber.className.indexOf('trk') >= 0 &&
    amber.style.getPropertyValue('--trk').indexOf('#ffaa00') >= 0, amber.style.getPropertyValue('--trk'));
  var ramp = page.doc.getElementById('sky-ramp');
  check(name + ': ramp stays a plain slider', ramp.className.indexOf('trk') < 0, ramp.className);

  var blue = makeGroup(1, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  blue.source = 1;
  blue.sky = { layers: 1 };
  var bServer = stub.makeServer({ group: blue, groups: [blue] });
  var bPage = await newPage(bServer);
  await finishPending(bPage);
  var bGlow = bPage.doc.getElementById('sky-glow');
  check(name + ': a Tuna Blue glows violet', bGlow.style.getPropertyValue('--trk').indexOf('#8a2be2') >= 0,
    bGlow.style.getPropertyValue('--trk'));
  var bAmber = bPage.doc.getElementById('sky-amber');
  check(name + ': a Tuna Blue can glow at night', bAmber.parentNode.hidden === false);
  check(name + ': Tuna Blue night glow is violet',
    cardText(bPage.doc.getElementById('prog-sky')).indexOf('A dim violet light through the night.') >= 0,
    cardText(bPage.doc.getElementById('prog-sky')));
  check(name + ': blue glow hint follows intensity',
    cardText(bPage.doc.getElementById('prog-sky')).indexOf('violet otherwise follows intensity') >= 0,
    cardText(bPage.doc.getElementById('prog-sky')));
  var bNight = bPage.doc.getElementById('sky-amber');
  check(name + ': blue night glow track is violet',
    bNight.style.getPropertyValue('--trk').indexOf('#8a2be2') >= 0,
    bNight.style.getPropertyValue('--trk'));
}

async function scenarioMakeCopy() {
  var name = 'make an editable copy';
  var server = stub.makeServer();
  server.group.source = 1;
  var page = await newPage(server);
  await finishPending(page);
  var before = stub.cloneSlots(server.group.slots);
  server.resetRequests();
  page.click(buttonExact(page, 'Make an editable copy'));
  await stub.settle();
  var copies = server.copyRequests();
  check(name + ': one copy request', copies.length === 1, copies.length);
  if (copies[0]) eq(name + ': copy names the group', copies[0].params, { gid: '2' });
  eq(name + ': source returns to My points', server.group.source, 0);
  check(name + ': points changed', JSON.stringify(stub.cloneSlots(server.group.slots)) !== JSON.stringify(before));
  check(name + ': toast names the copy', page.toastText().indexOf('Copied to My points') >= 0, page.toastText());
  check(name + ': toast offers Undo', page.toastText().indexOf('Undo') >= 0, page.toastText());
  check(name + ': My points pressed again', sourceButton(page, 'My points').getAttribute('aria-pressed') === 'true');
  check(name + ': editor tools back', page.progRoot().querySelector('.ptools').hidden === false);
  /* The copy handler snapshots the curve before it asks the device, so Undo is ready right away. */
  var undoBtn = toastButton(page, 'Undo');
  check(name + ': Undo action present', !!undoBtn);
  check(name + ': undo snapshot taken', buttonExact(page, 'Undo').disabled === false);
  page.click(undoBtn);
  await stub.settle();
  check(name + ': undo uses the snapshot', buttonExact(page, 'Undo').disabled === true);
  check(name + ': undo brings back the user\'s own points', JSON.stringify(stub.cloneSlots(server.group.slots)) === JSON.stringify(before));
  check(name + ': redo offered after undo', buttonExact(page, 'Redo').disabled === false);
}

async function scenarioMoonNeedsBlue() {
  var name = 'moon needs blue';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  var blue = makeGroup(3, 'Tuna Blue', 'tuna_blue', ['int', 'clr', 'vio', 'r', 'grn']);
  var server = stub.makeServer({ group: sun, groups: [sun, blue] });
  var page = await newPage(server);
  check(name + ': Sky offered for a Tuna Sun', sourceButton(page, 'Sky').hidden === false);
  check(name + ': no Moon layer for a Tuna Sun', page.doc.getElementById('sky-moon-on').parentNode.hidden === true);
  var groupButton = null;
  sourceSeg(page); /* the group switcher lists both groups */
  page.progRoot().querySelectorAll('button').forEach(function (b) { if (b.textContent.indexOf('Tuna Blue') >= 0) groupButton = b; });
  if (groupButton) {
    page.click(groupButton);
    await stub.settle();
    check(name + ': Moon layer offered for a Tuna Blue', page.doc.getElementById('sky-moon-on').parentNode.hidden === false);
  }
}

async function scenarioEditAfterSources() {
  var name = 'edit after switching sources';
  var server = stub.makeServer();
  var page = await newPage(server);
  var svg = page.svg();
  /* Through Sun and Moon and back: the chart is editable again. */
  page.click(sourceButton(page, 'Sky'));
  await stub.settle();
  await page.refresh();
  page.click(sourceButton(page, 'My points'));
  await stub.settle();
  await page.refresh();
  check(name + ': My points pressed again', sourceButton(page, 'My points').getAttribute('aria-pressed') === 'true');
  server.resetRequests();
  page.pointer(svg, 'pointerdown', { button: 0, pointerId: 11, clientX: xFor(480), clientY: yFor(cv(30)) });
  page.pointer(svg, 'pointermove', { pointerId: 11, clientX: xFor(540), clientY: yFor(cv(60)) });
  page.pointer(svg, 'pointerup', { pointerId: 11 });
  page.clock.tick(120);
  await stub.settle();
  check(name + ': dragging a point works again', server.pointRequests().length >= 1, server.pointRequests().length);
  check(name + ': the point moved', server.group.slots[0].minute === 540, 'minute ' + server.group.slots[0].minute);
  var add = buttonExact(page, '+ Add point');
  server.resetRequests();
  page.click(add);
  place(page, 900);
  await stub.settle();
  check(name + ': adding a point works again', server.pointRequests().length === 1, server.pointRequests().length);
}

async function scenarioNoLocation() {
  var name = 'no location';
  var sun = makeGroup(2, 'Tuna Sun', 'tuna_sun');
  sun.source = 1;
  var server = stub.makeServer({ group: sun, groups: [sun], location: null });
  var page = await newPage(server);
  var cta = page.doc.getElementById('prog-location-btn');
  check(name + ': call to action shown', !!cta && cta.hidden === false);
  check(name + ': call to action wording',
    cardText(cta).indexOf('Set a location in Settings to use the sun or moon.') >= 0, cardText(cta));
  check(name + ': settings card says no location',
    cardText(page.doc.getElementById('location-line')).indexOf('No location known yet.') >= 0,
    cardText(page.doc.getElementById('location-line')));
  page.click(cta);
  await stub.settle();
  eq(name + ': switches to Settings', page.doc.body.getAttribute('data-tab'), 'settings');
  check(name + ': Settings panel shown', page.doc.getElementById('tab-settings').hidden === false);
  eq(name + ': no generated request without a location', server.generatedRequests().length, 0);

  var here = makeGroup(3, 'Tuna Sun', 'tuna_sun');
  here.source = 1;
  var hereServer = stub.makeServer({ group: here, groups: [here] });
  var herePage = await newPage(hereServer);
  await finishPending(herePage);
  check(name + ': location line on the Program tab',
    cardText(herePage.doc.getElementById('prog-location')).indexOf('Location: Home Assistant (41.88, -87.63)') >= 0,
    cardText(herePage.doc.getElementById('prog-location')));
}

async function scenarioLocalFind() {
  var name = 'follower light find';
  var server = stub.makeServer({
    role: 'follower',
    followers: [{
      id: 'desk_follow', gid: 3, name: 'Desk strip', model: 'tuna_sun',
      target: 'light_rgb', lights: ['Desk RGB strip']
    }]
  });
  var page = await newPage(server);
  var card = dashCard(page, 'Desk strip');
  check(name + ': dashboard card shown', !!card);
  check(name + ': follower tag', cardText(card).indexOf('Follower') >= 0, cardText(card));
  check(name + ': bus lamp list hidden', page.doc.getElementById('lamps-card').hidden === true);
  check(name + ': bus scan hidden', page.doc.getElementById('find-lamps-card').hidden === true);
  check(name + ': send to lights hidden', page.doc.getElementById('writes-block').hidden === true);
  check(name + ': empty bus card hidden', page.doc.getElementById('bus-card').hidden === true);
  var btn = null;
  if (card) {
    card.querySelectorAll('button').forEach(function (b) {
      if (!btn && b.textContent.indexOf('Find these lights') >= 0) btn = b;
    });
  }
  check(name + ': find button present', !!btn, btn ? btn.textContent : 'missing');
  check(name + ': find button enabled on a follower', !!btn && btn.disabled === false);
  check(name + ': follower hides the leader transition',
    page.doc.getElementById('output-seconds-2').parentNode.hidden === true);
  check(name + ': follower hides acclimation', page.doc.getElementById('acc-card-2') === null);
  check(name + ': group settings heading shown', page.doc.getElementById('group-settings-heading').hidden === false);
  check(name + ': card names the followed group',
    cardText(card).indexOf('Follows group 3') >= 0, cardText(card));
  server.resetRequests();
  page.click(btn);
  await stub.settle();
  var reqs = server.requests.filter(function (r) { return r.path === '/klink/identify'; });
  check(name + ': one follower find request', reqs.length === 1, 'got ' + reqs.length);
  eq(name + ': find targets the entry id', reqs[0] ? reqs[0].params.follower : null, 'desk_follow');

  /* The leader's lamp list already has Identify for the same entry; do not repeat the card. */
  var leaderServer = stub.makeServer({
    followers: [{ id: 'lamp_entry', kid: 18000001, gid: 2, name: 'Board LED', model: 'tuna_sun',
      target: 'light_rgb', lights: ['Board LED'] }],
    lamps: [{ kid: 18000001, type: 2, gid: 2, name: 'Board LED' }]
  });
  var leaderPage = await newPage(leaderServer);
  var tags = leaderPage.groupsEl().querySelectorAll('.tag').filter(function (t) {
    return t.textContent === 'Follower';
  });
  check(name + ': leader does not repeat a lamp entry', tags.length === 0, 'got ' + tags.length);
  check(name + ': leader keeps the lamp list', leaderPage.doc.getElementById('lamps-card').hidden === false);

  /* A plain `lamp: false` follower has no lamp row, so it keeps its own dashboard card. */
  var plainServer = stub.makeServer({
    followers: [{ id: 'plain_follow', kid: 0, gid: 3, name: 'Sump strip', model: 'tuna_sun',
      target: 'light_brightness', lights: ['Sump strip'] }]
  });
  var plainPage = await newPage(plainServer);
  check(name + ': leader shows a plain follower card', !!dashCard(plainPage, 'Sump strip'));
}

async function main() {
  await scenarioPlace();
  await scenarioRefused();
  await scenarioAllUsed();
  await scenarioRefugium();
  await scenarioFindLights();
  await scenarioDragUndo();
  await scenarioAddUndo();
  await scenarioRemoveUndo();
  await scenarioCoalesce();
  await scenarioRevert();
  await scenarioDefaultPreset();
  await scenarioOrphans();
  await scenarioPicker();
  await scenarioManyLamps();
  await scenarioMoveBackToDefaultGroup();
  await scenarioTracks();
  await scenarioWheelDrag();
  await scenarioWheelHexAndKeys();
  await scenarioWheelHiddenBrightness();
  await scenarioWheelSliderSync();
  await scenarioLightCard();
  await scenarioLightPresets();
  await scenarioLightDirect();
  await scenarioLightCt();
  await scenarioLightColors();
  await scenarioEffectsRow();
  await scenarioEffectStart();
  await scenarioEffectEnds();
  await scenarioEffectStop();
  await scenarioEffectOptions();
  await scenarioOutputCards();
  await scenarioOutputSeconds();
  await scenarioOutputPoll();
  await scenarioAcclimationCards();
  await scenarioAcclimationToggle();
  await scenarioAcclimationFields();
  await scenarioAcclimationStatus();
  await scenarioSoftStart();
  await scenarioAcclimationDashboard();
  await scenarioSourceControl();
  await scenarioChooseSun();
  await scenarioGeneratedReadOnly();
  await scenarioDateChip();
  await scenarioSummaries();
  await scenarioSkySettings();
  await scenarioSkyRamp();
  await scenarioSkyRedraw();
  await scenarioSkyPreviewRedraw();
  await scenarioSkyHistory();
  await scenarioSunMoon();
  await scenarioSkyTracks();
  await scenarioMakeCopy();
  await scenarioMoonNeedsBlue();
  await scenarioEditAfterSources();
  await scenarioNoLocation();
  await scenarioLocalFind();
  console.log(failures ? failures + ' failure(s)' : 'all web tests passed');
  process.exit(failures ? 1 : 0);
}

main().catch(function (e) {
  console.error(e && e.stack || e);
  process.exit(1);
});
