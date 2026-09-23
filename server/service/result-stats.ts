// SPDX-License-Identifier: GPL-2.0-or-later

export interface HoleHighlight { hole: number; strokes: number; par: number | null; relativeToPar: number | null }
export interface PlayerStatistics {
  playerId: string;
  rank: number;
  total: number;
  completedHoles: number;
  relativeToPar: number | null;
  holesInOne: number;
  bestHole: HoleHighlight | null;
  worstHole: HoleHighlight | null;
  acceptedShots: number;
  hazardChoices: number;
}

/** Calculate only from committed scores and counted commands. Zero is an unfinished hole. */
export function resultStatistics(playerIds: string[], scores: number[][], par: number[],
  acceptedShots: number[] = [], hazardChoices: number[] = [], completedHoleCounts?: number[]): PlayerStatistics[] {
  const rows = playerIds.map((playerId, index) => {
    const scoreRow = scores[index] ?? [];
    const completed = scoreRow.flatMap((strokes, holeIndex) => {
      if (completedHoleCounts && holeIndex >= completedHoleCounts[index]) return [];
      if (strokes <= 0) return [];
      const holePar = par[holeIndex] > 0 ? par[holeIndex] : null;
      return [{ hole: holeIndex + 1, strokes, par: holePar,
        relativeToPar: holePar === null ? null : strokes - holePar } satisfies HoleHighlight];
    });
    const rated = completed.filter(hole => hole.relativeToPar !== null);
    const compareValue = completed.every(hole => hole.relativeToPar !== null)
      ? (hole: HoleHighlight) => hole.relativeToPar!
      : (hole: HoleHighlight) => hole.strokes;
    const ordered = [...completed].sort((a, b) =>
      compareValue(a) - compareValue(b) || a.hole - b.hole);
    return {
      playerId, rank: 0, total: scoreRow.reduce((sum, strokes) => sum + strokes, 0),
      completedHoles: completed.length,
      relativeToPar: rated.length ? rated.reduce((sum, hole) => sum + hole.relativeToPar!, 0) : null,
      holesInOne: completed.filter(hole => hole.strokes === 1).length,
      bestHole: ordered[0] ?? null,
      worstHole: [...ordered].sort((a, b) =>
        compareValue(b) - compareValue(a) || a.hole - b.hole)[0] ?? null,
      acceptedShots: acceptedShots[index] ?? 0, hazardChoices: hazardChoices[index] ?? 0,
    };
  });
  rows.sort((a, b) => b.completedHoles - a.completedHoles || a.total - b.total
    || playerIds.indexOf(a.playerId) - playerIds.indexOf(b.playerId));
  rows.forEach((row, index) => {
    const previous = rows[index - 1];
    row.rank = previous && previous.completedHoles === row.completedHoles && previous.total === row.total
      ? previous.rank : index + 1;
  });
  return rows;
}
