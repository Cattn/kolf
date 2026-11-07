// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { parseCourseBytes } from '../service/course-catalog.ts';
import { objectCatalog, parseObjectCatalog } from '../service/object-catalog.ts';

const committed = readFileSync(new URL('../../protocol/object-catalog.json', import.meta.url), 'utf8');

const course = (type: string) => Buffer.from([
  '[0-course@-50,-50]', 'Name=Catalog', 'author=test', '',
  '[1-hole@-50,-50|0]', 'par=3', '',
  '[1-ball@100,100]', 'dummykey=true', '',
  '[1-cup@200,200|1]', 'dummykey=true', '',
  `[1-${type}@150,150|2]`, 'dummykey=true', ''].join('\n'));

test('the committed object catalog lists the built-in types', () => {
  assert.deepEqual(objectCatalog.types.map(type => type.id), ['blackhole', 'bridge', 'bumper', 'cup', 'floater',
    'puddle', 'sand', 'sign', 'slope', 'wall', 'windmill']);
  assert.deepEqual(objectCatalog.structuralGroups, ['ball', 'course', 'hole']);
  assert.match(objectCatalog.catalogHash, /^[0-9a-f]{64}$/u);
});

test('an edited catalog fails its hash check', () => {
  const edited = JSON.parse(committed);
  edited.types.push({ ...edited.types[0], id: 'speed_pad' });
  assert.throws(() => parseObjectCatalog(JSON.stringify(edited)), /hash does not match/u);
  edited.catalogHash = 'x'.repeat(64);
  assert.throws(() => parseObjectCatalog(JSON.stringify(edited)), /hash does not match/u);
});

test('course validation accepts catalog types and rejects others', () => {
  assert.equal(parseCourseBytes(course('windmill')).holes, 1);
  assert.throws(() => parseCourseBytes(course('speed_pad')), /unsupported course group/u);
  assert.throws(() => parseCourseBytes(course('Wall')), /unsupported course group/u);
});
