(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.BlurInput = api;
})(typeof globalThis === 'object' ? globalThis : this, () => {
  'use strict';

  const BUTTON_BITS = {
    a: 0x1000, b: 0x2000, x: 0x4000, y: 0x8000,
    left_shoulder: 0x100, right_shoulder: 0x200, back: 0x20, start: 0x10,
    left_stick: 0x40, right_stick: 0x80,
    dpad_up: 0x1, dpad_down: 0x2, dpad_left: 0x4, dpad_right: 0x8
  };
  const STANDARD_BUTTONS = [
    [0, 0x1000], [1, 0x2000], [2, 0x4000], [3, 0x8000],
    [4, 0x100], [5, 0x200], [8, 0x20], [9, 0x10],
    [10, 0x40], [11, 0x80], [12, 0x1], [13, 0x2], [14, 0x4], [15, 0x8]
  ];
  const TARGET_AXES = new Set([
    'left_x', 'left_y', 'right_x', 'right_y', 'left_trigger', 'right_trigger'
  ]);
  const CURVES = new Set(['linear', 'exponential', 'cubic']);
  const HOTKEY_ACTIONS = new Set(['fullscreen', 'hud', 'release_inputs']);
  const isObject = (value) => value !== null && typeof value === 'object' && !Array.isArray(value);
  const isText = (value, maximum) => typeof value === 'string'
    && value.length > 0 && value.length <= maximum && !/[\x00-\x1f\x7f]/.test(value);
  const isInteger = (value, minimum, maximum) => Number.isInteger(value)
    && value >= minimum && value <= maximum;

  function validateInputConfig(config) {
    const fail = (path, message) => { throw new TypeError(`${path}: ${message}`); };
    const array = (value, path, maximum, nonempty = false) => {
      if (!Array.isArray(value) || value.length > maximum || (nonempty && !value.length)) {
        fail(path, `expected ${nonempty ? '1-' : '0-'}${maximum} items`);
      }
    };
    const text = (value, path, maximum) => {
      if (!isText(value, maximum)) fail(path, `expected 1-${maximum} printable bytes`);
    };
    if (!isObject(config)) fail('config', 'expected object');
    if (config.schema !== 1) fail('schema', 'unsupported schema');
    array(config.players, 'players', 4, true);
    array(config.hotkeys, 'hotkeys', 64);
    const players = new Set();
    config.players.forEach((player, pi) => {
      const pp = `players[${pi}]`;
      if (!isObject(player)) fail(pp, 'expected object');
      if (!isInteger(player.player, 0, 3) || players.has(player.player)) fail(`${pp}.player`, 'expected unique integer in 0..3');
      players.add(player.player);
      array(player.devices, `${pp}.devices`, 16);
      const ids = new Set();
      player.devices.forEach((device, di) => {
        const dp = `${pp}.devices[${di}]`;
        if (!isObject(device)) fail(dp, 'expected object');
        text(device.id, `${dp}.id`, 64);
        if (ids.has(device.id)) fail(`${dp}.id`, 'duplicate device id');
        ids.add(device.id);
        if (device.kind !== 'keyboard' && device.kind !== 'gamepad') fail(`${dp}.kind`, 'expected keyboard or gamepad');
        text(device.selector, `${dp}.selector`, 256);
        if (typeof device.enabled !== 'boolean' || typeof device.passthrough !== 'boolean') fail(dp, 'enabled and passthrough must be booleans');
        array(device.bindings, `${dp}.bindings`, 256);
        array(device.axes, `${dp}.axes`, 32);
        device.bindings.forEach((binding, bi) => {
          const bp = `${dp}.bindings[${bi}]`;
          if (!isObject(binding) || !isObject(binding.source) || !isObject(binding.target)) fail(bp, 'expected binding objects');
          text(binding.action, `${bp}.action`, 64);
          if (device.kind === 'keyboard') {
            if (binding.source.type !== 'key' || !isText(binding.source.code, 64)
                || binding.target.type !== 'key' || !isInteger(binding.target.vk, 1, 255)) {
              fail(bp, 'invalid keyboard mapping');
            }
          } else if (binding.source.type !== 'button' || !isInteger(binding.source.index, 0, 31)
                     || binding.target.type !== 'button' || !Object.hasOwn(BUTTON_BITS, binding.target.button)) {
            fail(bp, 'invalid gamepad button mapping');
          }
        });
        if (device.kind !== 'gamepad' && device.axes.length) fail(`${dp}.axes`, 'axes require a gamepad');
        device.axes.forEach((axis, ai) => {
          const ap = `${dp}.axes[${ai}]`;
          if (!isObject(axis)) fail(ap, 'expected object');
          text(axis.action, `${ap}.action`, 64);
          if (!isInteger(axis.source_axis, 0, 31) || !TARGET_AXES.has(axis.target_axis)
              || !Number.isFinite(axis.deadzone) || axis.deadzone < 0 || axis.deadzone >= 0.95
              || !Number.isFinite(axis.saturation) || axis.saturation <= axis.deadzone || axis.saturation > 1
              || !CURVES.has(axis.curve) || !Number.isFinite(axis.exponent) || axis.exponent < 1 || axis.exponent > 5
              || typeof axis.invert !== 'boolean') {
            fail(ap, 'invalid axis mapping');
          }
        });
      });
    });
    const hotkeys = new Set();
    config.hotkeys.forEach((hotkey, hi) => {
      const hp = `hotkeys[${hi}]`;
      if (!isObject(hotkey)) fail(hp, 'expected object');
      text(hotkey.key, `${hp}.key`, 64);
      if (hotkeys.has(hotkey.key) || !HOTKEY_ACTIONS.has(hotkey.action)) fail(hp, 'duplicate key or invalid action');
      hotkeys.add(hotkey.key);
    });
    return config;
  }

  function selectGamepad(gamepads, devices) {
    for (const device of devices) {
      const selector = device.selector || '*';
      const indexed = /^index:(\d+)$/.exec(selector);
      if (indexed) {
        const gamepad = gamepads[Number(indexed[1])];
        if (gamepad && gamepad.connected) return [gamepad, device];
        continue;
      }
      for (const gamepad of gamepads) {
        if (gamepad && gamepad.connected && (selector === '*' || gamepad.id === selector)) {
          return [gamepad, device];
        }
      }
    }
    return null;
  }

  function curveAxis(raw, mapping) {
    let value = Math.max(-1, Math.min(1, raw || 0));
    if (mapping.invert) value = -value;
    const sign = Math.sign(value);
    const magnitude = Math.abs(value);
    if (magnitude <= mapping.deadzone) return 0;
    let normalized = Math.min(
      1,
      (magnitude - mapping.deadzone) / (mapping.saturation - mapping.deadzone)
    );
    if (mapping.curve === 'cubic') normalized *= normalized * normalized;
    else if (mapping.curve === 'exponential') {
      normalized = Math.pow(normalized, mapping.exponent || 1);
    }
    return sign * normalized;
  }

  function gamepadState(gamepad, device) {
    const buttons = gamepad.buttons || [];
    const axis = (index) => Math.max(
      -32767,
      Math.min(32767, Math.round(((gamepad.axes || [])[index] || 0) * 32767))
    );
    let bits = 0;
    if (device.passthrough) {
      for (const [index, bit] of STANDARD_BUTTONS) {
        if (buttons[index] && buttons[index].pressed) bits |= bit;
      }
    }
    for (const binding of device.bindings || []) {
      if (binding.source.type === 'button'
          && buttons[binding.source.index]
          && buttons[binding.source.index].pressed
          && binding.target.type === 'button') {
        bits |= BUTTON_BITS[binding.target.button] || 0;
      }
    }
    let leftTrigger = device.passthrough
      ? Math.round(((buttons[6] && buttons[6].value) || 0) * 255)
      : 0;
    let rightTrigger = device.passthrough
      ? Math.round(((buttons[7] && buttons[7].value) || 0) * 255)
      : 0;
    let leftX = device.passthrough ? axis(0) : 0;
    let leftY = device.passthrough ? -axis(1) : 0;
    let rightX = device.passthrough ? axis(2) : 0;
    let rightY = device.passthrough ? -axis(3) : 0;
    for (const mapping of device.axes || []) {
      const curved = curveAxis((gamepad.axes || [])[mapping.source_axis], mapping);
      const value = Math.round(curved * 32767);
      if (mapping.target_axis === 'left_x') leftX = value;
      else if (mapping.target_axis === 'left_y') leftY = value;
      else if (mapping.target_axis === 'right_x') rightX = value;
      else if (mapping.target_axis === 'right_y') rightY = value;
      else if (mapping.target_axis === 'left_trigger') {
        leftTrigger = Math.round(Math.max(0, curved) * 255);
      } else if (mapping.target_axis === 'right_trigger') {
        rightTrigger = Math.round(Math.max(0, curved) * 255);
      }
    }
    return {
      bits, leftTrigger, rightTrigger, leftX, leftY, rightX, rightY
    };
  }

  return { validateInputConfig, selectGamepad, curveAxis, gamepadState };
});
