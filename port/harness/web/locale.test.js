'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const locale = require('./locale.js');

test('English and French catalogs have exact complete coverage', () => {
  assert.equal(locale.create('en').coverage, 1);
  assert.equal(locale.create('fr').coverage, 1);
  assert.equal(locale.create('fr').t('fullscreen'), 'Plein écran');
});

test('unknown locales fall back strictly to English', () => {
  assert.equal(locale.create('de').locale, 'en');
  assert.equal(locale.fromHash('#gpu=1&lang=fr').locale, 'fr');
  assert.equal(locale.fromHash('#lang=fr-FR').locale, 'fr-FR');
  assert.equal(locale.fromHash('#lang=FR_fr').locale, 'fr-FR');
  assert.equal(locale.fromHash('#lang=fr-FR').t('fullscreen'), 'Plein écran');
  assert.throws(() => locale.create('en').t('not_a_key'), /unknown canonical/);
});

test('hash input cannot inject catalog keys, markup, or extra parameters', () => {
  assert.equal(locale.fromHash('#lang=%3Cimg%20src=x%20onerror=alert(1)%3E').locale, 'en');
  assert.equal(locale.fromHash('#lang=fr%26gpu%3D0').locale, 'en');
  assert.equal(locale.fromHash('#lang=__proto__').locale, 'en');
  assert.equal(locale.fromHash('#lang=fr--FR').locale, 'en');
  assert.equal(locale.fromHash('#lang=' + 'f'.repeat(2049)).locale, 'en');
  assert.equal(locale.create('fr').t('fullscreen').includes('<'), false);
});

test('BCP47 normalization is canonical and conservative', () => {
  assert.equal(locale.canonicalTag('zh_hant_tw'), 'zh-Hant-TW');
  assert.equal(locale.canonicalTag('en-u-ca-gregory'), null);
  assert.equal(locale.canonicalTag('../fr'), null);
});
