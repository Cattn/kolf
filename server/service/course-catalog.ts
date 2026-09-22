// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { isAbsolute, relative, resolve } from 'node:path';
import type { CourseCatalogEntry } from './lobby.ts';

export interface CatalogFile {
  courseId: string;
  displayName: string;
  fileName: string;
  par?: number[];
}

export const shippedDevelopmentCourses: CatalogFile[] = [
  { courseId: 'classic', displayName: 'Classic', fileName: 'Classic.kolf' },
  { courseId: 'easy', displayName: 'Easy', fileName: 'Easy.kolf' },
  { courseId: 'practice', displayName: 'Practice', fileName: 'Practice' },
];

export function courseHash(content: Buffer): string {
  const normalized = content.toString('utf8').replaceAll('\r\n', '\n').replaceAll('\r', '\n');
  return createHash('sha256').update(normalized).digest('hex');
}

export function loadCourseCatalog(root: string, files: CatalogFile[] = shippedDevelopmentCourses): CourseCatalogEntry[] {
  const catalogRoot = resolve(root);
  return files.map(file => {
    const path = resolve(catalogRoot, file.fileName);
    const fromRoot = relative(catalogRoot, path);
    if (!fromRoot || fromRoot.startsWith('..') || isAbsolute(fromRoot)) throw Error(`catalog path escapes root: ${file.fileName}`);
    const expectedHash = courseHash(readFileSync(path));
    return { courseId: file.courseId, displayName: file.displayName, resourceName: file.fileName,
      expectedHash, par: file.par ? [...file.par] : undefined };
  });
}
