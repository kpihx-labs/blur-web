'use strict';

(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.BlurLocale = api;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  const catalogs = {
    en: {
      brand_gl: 'Blur on WebGL2', brand_gpu: 'Blur on WebGPU', connecting: 'connecting…', connected: 'connected',
      connected_input_disabled: 'connected; configured input disabled (see log)', viewer_busy: 'another viewer is connected - retrying every 3 s', disconnected_retrying: 'disconnected - retrying',
      input_keyboard: 'input: keyboard', input_controller: 'input: controller', fullscreen: 'Fullscreen', exit_fullscreen: 'Exit fullscreen', panels: 'Panels',
      status_bar: 'Status bar', log: 'Log', hud: 'Frame-time HUD', fullscreen_hint: 'F11 / double-click = fullscreen',
      hud_avg: 'avg', hud_max: 'max', hud_scale: 'scale', hud_off: 'off', hud_draws: 'draws', hud_gpu: 'gpu', hud_unavailable: 'n/a',
      hud_page_js: 'page JS per frame', hud_intervals: 'intervals above = time between game Presents', hud_pipelines: 'pipelines', hud_pending: 'pending', hud_skipped: 'skipped/frame', hud_held: 'held'
    },
    fr: {
      brand_gl: 'Blur sur WebGL2', brand_gpu: 'Blur sur WebGPU', connecting: 'connexion…', connected: 'connecté',
      connected_input_disabled: 'connecté ; entrée configurée désactivée (voir le journal)', viewer_busy: 'un autre spectateur est connecté — nouvelle tentative dans 3 s', disconnected_retrying: 'déconnecté — nouvelle tentative',
      input_keyboard: 'entrée : clavier', input_controller: 'entrée : manette', fullscreen: 'Plein écran', exit_fullscreen: 'Quitter le plein écran', panels: 'Panneaux',
      status_bar: 'Barre d’état', log: 'Journal', hud: 'HUD des temps d’image', fullscreen_hint: 'F11 / double-clic = plein écran',
      hud_avg: 'moy.', hud_max: 'max.', hud_scale: 'échelle', hud_off: 'désactivé', hud_draws: 'tracés', hud_gpu: 'GPU', hud_unavailable: 'indispo.',
      hud_page_js: 'JS de la page par image', hud_intervals: 'intervalles ci-dessus = temps entre présentations du jeu', hud_pipelines: 'pipelines', hud_pending: 'en attente', hud_skipped: 'ignorés/image', hud_held: 'retenus'
    }
  };
  const baseKeys = Object.keys(catalogs.en).sort();
  for (const [locale, catalog] of Object.entries(catalogs)) {
    const keys = Object.keys(catalog).sort();
    if (keys.join('\0') !== baseKeys.join('\0')) throw new Error(`locale ${locale} does not exactly cover the English catalog`);
  }
  function canonicalTag(value) {
    if (typeof value !== 'string' || !value || value.length > 35 || !/^[A-Za-z0-9_-]+$/.test(value)) return null;
    const parts = value.replaceAll('_', '-').split('-');
    if (!/^[A-Za-z]{2,3}$/.test(parts[0]) || parts.some((part) => !part)) return null;
    const canonical = [parts[0].toLowerCase()];
    let index = 1;
    if (index < parts.length && /^[A-Za-z]{4}$/.test(parts[index])) {
      const script = parts[index].toLowerCase();
      canonical.push(script[0].toUpperCase() + script.slice(1)); index++;
    }
    if (index < parts.length && (/^[A-Za-z]{2}$/.test(parts[index]) || /^[0-9]{3}$/.test(parts[index]))) {
      canonical.push(parts[index].toUpperCase()); index++;
    }
    const variants = new Set();
    for (; index < parts.length; index++) {
      if (!/^(?:[0-9][A-Za-z0-9]{3}|[A-Za-z0-9]{5,8})$/.test(parts[index])) return null;
      const variant = parts[index].toLowerCase();
      if (variants.has(variant)) return null;
      variants.add(variant); canonical.push(variant);
    }
    return canonical.join('-');
  }
  function normalize(value) {
    const tag = canonicalTag(value);
    return tag && Object.hasOwn(catalogs, tag.split('-')[0]) ? tag : 'en';
  }
  function create(locale) {
    locale = normalize(locale);
    const catalogLocale = locale.split('-')[0];
    return { locale, catalogLocale, coverage: Object.keys(catalogs[catalogLocale]).length / baseKeys.length, t(key) {
      if (!Object.hasOwn(catalogs.en, key)) throw new Error(`unknown canonical locale key: ${key}`);
      return catalogs[catalogLocale][key] ?? catalogs.en[key];
    } };
  }
  function fromHash(hash) {
    hash = String(hash || '');
    if (hash.length > 2048) return create('en');
    const params = new URLSearchParams(hash.replace(/^#/, ''));
    return create(params.get('lang') || 'en');
  }
  return { catalogs, canonicalTag, create, fromHash };
});
