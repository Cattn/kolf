// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { parseCourseBytes } from '../service/course-catalog.ts';
import { objectCatalog, parseObjectCatalog } from '../service/object-catalog.ts';

const committed = readFileSync(new URL('../../protocol/object-catalog.json', import.meta.url), 'utf8');

const course = (type: string, properties: string[] = ['dummykey=true']) => Buffer.from([
  '[0-course@-50,-50]', 'Name=Catalog', 'author=test', '',
  '[1-hole@-50,-50|0]', 'par=3', '',
  '[1-ball@100,100]', 'dummykey=true', '',
  '[1-cup@200,200|1]', 'dummykey=true', '',
  `[1-${type}@150,150|2]`, ...properties, ''].join('\n'));

test('the committed object catalog lists the built-in types', () => {
  assert.deepEqual(objectCatalog.types.map(type => type.id), ['barrel', 'blackhole', 'bridge', 'bumper', 'conveyor', 'crate', 'crumble',
    'cup', 'fence', 'floater', 'gate', 'ice', 'lava', 'magnet', 'mine', 'mud', 'pin', 'pit', 'plate', 'puddle', 'quicksand',
    'repulsor', 'rough', 'sand', 'shallowwater', 'sign', 'slope', 'speedpad', 'spinner', 'sweeper', 'wall', 'whirlpool', 'wind', 'windmill']);
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

test('course validation checks object properties against the catalog schema', () => {
  assert.equal(parseCourseBytes(course('windmill', ['speed=9', 'bottom=false', 'width=120', 'hasFinalLoad=true'])).holes, 1);
  assert.equal(parseCourseBytes(course('slope', ['gradient=Opposite Diagonal', 'grade=2.5'])).holes, 1);
  assert.equal(parseCourseBytes(course('blackhole', ['exit=-26,293', 'exitDeg=350', 'maxspeed=10'])).holes, 1);
  for (const [type, property] of [['windmill', 'speed=50'], ['windmill', 'speed=fast'], ['slope', 'gradient=Sideways'],
    ['slope', 'grade=-1'], ['blackhole', 'exit=1,2,3'], ['blackhole', 'exitDeg=400'], ['floater', 'startPoint=1e9,0'],
    ['sign', `Comment=${'x'.repeat(1001)}`], ['bridge', 'topWallVisible=maybe'], ['puddle', 'width=Infinity']])
    assert.throws(() => parseCourseBytes(course(type!, [property!])), /out of range/u, `${type} ${property}`);
});

test('course validation refuses a newer course format', () => {
  const versioned = (version: string) => Buffer.from(course('cup').toString('utf8').replace('author=test', `author=test
formatVersion=${version}`));
  assert.equal(parseCourseBytes(versioned('2')).holes, 1);
  assert.throws(() => parseCourseBytes(versioned('3')), /newer version of Kolf/u);
});

test('the catalog describes every property with a type and range', () => {
  const windmill = objectCatalog.byId.get('windmill')!;
  assert.deepEqual(windmill.properties.map(property => property.key), ['width', 'height', 'topWallVisible', 'leftWallVisible',
    'rightWallVisible', 'botWallVisible', 'speed', 'bottom']);
  assert.deepEqual(objectCatalog.byId.get('cup')!.properties, []);
  assert.deepEqual(objectCatalog.byId.get('slope')!.properties.find(property => property.key === 'gradient')!.values,
    ['Vertical', 'Horizontal', 'Diagonal', 'Opposite Diagonal', 'Elliptic']);
});
