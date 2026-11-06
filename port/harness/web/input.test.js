'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const {
  curveAxis, gamepadState, selectGamepad, validateInputConfig
} = require('./input.js');

const gamepad = (id, index = 0) => ({
  id,
  index,
  connected: true,
  axes: [0.5, -0.25, 0, 0],
  buttons: Array.from({ length: 16 }, () => ({ pressed: false, value: 0 }))
});
const validConfig = () => ({
  schema: 1,
  preset: null,
  players: [{
    player: 0,
    devices: [{
      id: 'pad',
      kind: 'gamepad',
      selector: 'index:0',
      enabled: true,
      passthrough: false,
      bindings: [{
        action: 'accept',
        source: { type: 'button', index: 0 },
        target: { type: 'button', button: 'a' }
      }],
      axes: [{
        action: 'steering',
        source_axis: 0,
        target_axis: 'left_x',
        deadzone: 0.1,
        saturation: 1,
        curve: 'linear',
        exponent: 1,
        invert: false
      }]
    }]
  }],
  hotkeys: [{ key: 'F11', action: 'fullscreen' }]
});

test('input configuration validation accepts the supported shape', () => {
  const config = validConfig();
  assert.equal(validateInputConfig(config), config);
});

test('input configuration validation rejects structured unsafe values', () => {
  const cases = [
    { mutate: (c) => { c.players[0].devices = {}; }, match: /devices/ },
    { mutate: (c) => { c.players[0].devices[0].enabled = 'yes'; }, match: /booleans/ },
    { mutate: (c) => { c.players[0].devices[0].bindings[0].source.index = 32; }, match: /button mapping/ },
    { mutate: (c) => { c.players[0].devices[0].axes[0].deadzone = 1; }, match: /axis mapping/ },
    { mutate: (c) => { c.hotkeys[0].action = 'eval'; }, match: /invalid action/ }
  ];
  for (const { mutate, match } of cases) {
    const config = validConfig();
    mutate(config);
    assert.throws(() => validateInputConfig(config), match);
  }
});

test('selectors are exact or explicit index selectors', () => {
  const pads = [gamepad('Acme Controller Pro'), gamepad('Acme Controller', 1)];
  assert.equal(selectGamepad(pads, [{ selector: 'Controller' }]), null);
  assert.equal(selectGamepad(pads, [{ selector: 'Acme Controller' }])[0], pads[1]);
  assert.equal(selectGamepad(pads, [{ selector: 'index:0' }])[0], pads[0]);
});

test('non-passthrough devices do not leak standard triggers or axes', () => {
  const pad = gamepad('Pad');
  pad.buttons[6] = { pressed: true, value: 0.75 };
  const state = gamepadState(pad, {
    passthrough: false,
    bindings: [],
    axes: []
  });
  assert.deepEqual(state, {
    bits: 0,
    leftTrigger: 0,
    rightTrigger: 0,
    leftX: 0,
    leftY: 0,
    rightX: 0,
    rightY: 0
  });
});

test('explicit mappings apply curves and buttons', () => {
  const pad = gamepad('Pad');
  pad.buttons[1].pressed = true;
  const mapping = {
    source_axis: 0,
    target_axis: 'left_x',
    deadzone: 0,
    saturation: 1,
    curve: 'linear',
    exponent: 1,
    invert: true
  };
  const state = gamepadState(pad, {
    passthrough: false,
    bindings: [{
      source: { type: 'button', index: 1 },
      target: { type: 'button', button: 'a' }
    }],
    axes: [mapping]
  });
  assert.equal(state.bits, 0x1000);
  assert.equal(state.leftX, Math.round(-0.5 * 32767));
  assert.equal(curveAxis(0.05, { ...mapping, deadzone: 0.1 }), 0);
});
