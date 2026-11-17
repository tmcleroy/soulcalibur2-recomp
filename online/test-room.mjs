// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. From sf3comp's online/test-room.mjs, for Soulcalibur II Recomp.
// The waiting room's countries. Run with node online/test-room.mjs; no deployed service needed.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
const source = await readFile(new URL('./src/index.js', import.meta.url), 'utf8');
const { Room } = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
const room = new Room({}, {});
const here = async (secret, more) => (await room.fetch(new Request('https://room/here', { method:'POST',
  body: JSON.stringify({ secret: secret.repeat(16), name: secret, games:['soulcalibur2'], protocol:1001, version:'v', ...more }) }))).json();
await here('a', { country:'fr' });
let seen = await here('b', {});
assert.equal(seen.players[0].country, 'FR', 'a chosen country is passed on, in capitals');
seen = await here('a', {});
assert.equal(seen.players[0].country, undefined, 'none chosen: none said');
for (const country of ['FRA', '1x', '<b', 7, null]) {
  await here('b', { country });
  assert.equal((await here('a', {})).players[0].country, undefined, `not a country: ${JSON.stringify(country)}`);
}
console.log('ok: a chosen country is passed on; none, or anything not two letters, is not');
