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

async function main() {
  await scenarioPlace();
  await scenarioRefused();
  await scenarioAllUsed();
  await scenarioDragUndo();
  await scenarioAddUndo();
  await scenarioRemoveUndo();
  await scenarioCoalesce();
  await scenarioRevert();
  console.log(failures ? failures + ' failure(s)' : 'all web tests passed');
  process.exit(failures ? 1 : 0);
}

main().catch(function (e) {
  console.error(e && e.stack || e);
  process.exit(1);
});
