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

async function main() {
  await scenarioPlace();
  await scenarioRefused();
  await scenarioAllUsed();
  await scenarioDragUndo();
  await scenarioAddUndo();
  await scenarioRemoveUndo();
  await scenarioCoalesce();
  await scenarioRevert();
  await scenarioOrphans();
  await scenarioPicker();
  await scenarioManyLamps();
  await scenarioTracks();
  console.log(failures ? failures + ' failure(s)' : 'all web tests passed');
  process.exit(failures ? 1 : 0);
}

main().catch(function (e) {
  console.error(e && e.stack || e);
  process.exit(1);
});
