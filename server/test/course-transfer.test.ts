// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { COURSE_CHUNK_BYTES, MAX_COURSE_BYTES, parseCourseBytes } from '../service/course-catalog.ts';
import { CourseTransfer } from '../service/course-transfer.ts';

const fixture = readFileSync(fileURLToPath(new URL('../../tests/multiplayer/fixtures/static.kolf', import.meta.url)));
const hash = (bytes: Buffer) => createHash('sha256').update(bytes).digest('hex');
const upload = (transfer: CourseTransfer, bytes: Buffer, id = 'upload_1') => {
  const sha256 = hash(bytes);
  transfer.begin(id, sha256, bytes.length);
  for (let index = 0; index < Math.ceil(bytes.length / COURSE_CHUNK_BYTES); ++index)
    transfer.chunk(id, index, bytes.subarray(index * COURSE_CHUNK_BYTES,
      (index + 1) * COURSE_CHUNK_BYTES).toString('base64'));
  return transfer.finish(id);
};

test('custom course upload publishes metadata and exact downloadable bytes', () => {
  const transfer = new CourseTransfer(() => 1_000);
  const descriptor = upload(transfer, fixture);
  assert.equal(descriptor.source, 'uploaded');
  assert.equal(descriptor.sha256, hash(fixture));
  assert.equal(descriptor.byteSize, fixture.length);
  assert.equal(descriptor.holes, parseCourseBytes(fixture).holes);
  assert.equal(transfer.get(hash(fixture), 0).data, fixture.toString('base64'));
  assert.deepEqual(transfer.begin('upload_2', hash(fixture), fixture.length).descriptor, descriptor);
});

test('custom course bounds, hash, order, and expiry reject unsafe transfers', () => {
  let now = 1_000;
  const transfer = new CourseTransfer(() => now);
  assert.throws(() => transfer.begin('large', hash(fixture), MAX_COURSE_BYTES + 1));
  transfer.begin('order', hash(fixture), COURSE_CHUNK_BYTES + 1);
  assert.throws(() => transfer.chunk('order', 1, 'AA=='));
  assert.throws(() => transfer.finish('order'));
  transfer.begin('bad_hash', 'a'.repeat(64), fixture.length);
  transfer.chunk('bad_hash', 0, fixture.toString('base64'));
  assert.throws(() => transfer.finish('bad_hash'));
  transfer.begin('expired', hash(fixture), fixture.length);
  now += 30_001;
  assert.throws(() => transfer.chunk('expired', 0, fixture.toString('base64')));
});

test('malformed course groups and executable properties are rejected', () => {
  assert.throws(() => parseCourseBytes(Buffer.from('[0-course@-50,-50]\nName=Broken\n[1-unknown@0,0]\n')));
  assert.throws(() => parseCourseBytes(Buffer.from(fixture.toString('utf8').replace(/Name=[^\n]+/u,
    'Name=Unsafe\nPlugin=evil.dll'))));
});
