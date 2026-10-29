// Every language must translate every string in i18n/en.json, with the same {placeholders}.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const load = (code) => JSON.parse(readFileSync(new URL(`../i18n/${code}.json`, import.meta.url), 'utf8'));
const english = load('en');
const languages = ['de', 'es', 'fr', 'hu', 'it', 'nl', 'pl', 'pt-BR', 'ru', 'tr'];
const holes = (text) => [...String(text).matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort().join(',');
const forms = (value) => (typeof value === 'string' ? [value] : Object.values(value));

for (const code of languages) {
  test(`i18n ${code}: complete, with matching placeholders`, () => {
    const lang = load(code);
    const missing = [], broken = [];
    for (const [key, value] of Object.entries(english)) {
      const got = lang[key];
      if (got === undefined || forms(got).some((s) => typeof s !== 'string' || !s.trim())) { missing.push(key); continue; }
      const wanted = holes(forms(value).join(' '));
      for (const form of forms(got)) {
        const have = holes(form);
        // plural forms may drop {count} (e.g. "one" written as "1 game"), but never add or rename one
        if (have !== wanted && !wanted.split(',').filter(Boolean).some((n) => have.split(',').includes(n)) && have !== '') broken.push(key);
      }
    }
    assert.deepEqual(missing.slice(0, 10), [], `${missing.length} missing or empty`);
    assert.deepEqual([...new Set(broken)].slice(0, 10), [], 'placeholder mismatch');
  });
}
