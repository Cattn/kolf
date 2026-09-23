import { test } from 'node:test';
import assert from 'node:assert/strict';
import { resultStatistics } from '../service/result-stats.ts';

test('completed scorecard ranks ties and computes highlights from completed holes', () => {
  const rows = resultStatistics(['a', 'b', 'c'], [[1, 4, 2], [2, 3, 2], [3, 4, 2]], [3, 4, 2],
    [3, 4, 5], [0, 2, 1]);
  assert.deepEqual(rows.map(row => [row.playerId, row.rank, row.total, row.relativeToPar]),
    [['a', 1, 7, -2], ['b', 1, 7, -2], ['c', 3, 9, 0]]);
  assert.equal(rows[0].holesInOne, 1);
  assert.equal(rows[0].bestHole?.hole, 1);
  assert.equal(rows[0].worstHole?.hole, 2);
  assert.equal(rows[1].hazardChoices, 2);
  assert.equal(rows[2].acceptedShots, 5);
});

test('unfinished rows and missing or zero par do not fabricate scores or relative totals', () => {
  const rows = resultStatistics(['a', 'b', 'c'], [[2, 0], [1], []], [0]);
  assert.deepEqual(rows.map(row => [row.playerId, row.completedHoles, row.total, row.relativeToPar]),
    [['b', 1, 1, null], ['a', 1, 2, null], ['c', 0, 0, null]]);
  assert.equal(rows[0].bestHole?.par, null);
  assert.equal(rows[2].bestHole, null);
  assert.equal(rows[2].rank, 3);
});

test('an interrupted current hole contributes strokes but not a completed-hole highlight', () => {
  const rows = resultStatistics(['a', 'b'], [[2, 1], [3, 0]], [3, 2], [3, 3], [0, 0], [1, 1]);
  assert.equal(rows[0].total, 3);
  assert.equal(rows[0].completedHoles, 1);
  assert.equal(rows[0].holesInOne, 0);
  assert.equal(rows[0].relativeToPar, -1);
});

test('skipped partial hole keeps strokes but cannot become a completed-hole highlight', () => {
  const rows = resultStatistics(['a', 'b'], [[1, 2], [0, 3]], [3, 3], [2, 1], [0, 0], [2, 2], [1]);
  assert.deepEqual(rows.map(row => [row.playerId, row.total, row.completedHoles, row.holesInOne]),
    [['a', 3, 1, 0], ['b', 3, 1, 0]]);
  assert(rows.every(row => row.bestHole?.hole === 2 && row.worstHole?.hole === 2));
});
