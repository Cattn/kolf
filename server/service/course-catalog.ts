// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { isAbsolute, relative, resolve } from 'node:path';
import type { CourseCatalogEntry } from './lobby.ts';

export interface CatalogFile {
  courseId: string;
  fileName: string;
  displayName?: string;
}

export const MAX_COURSE_BYTES = 4 * 1024 * 1024;
export const COURSE_CHUNK_BYTES = 48 * 1024;
const allowedTypes = new Set(['ball', 'blackhole', 'bridge', 'bumper', 'course', 'cup', 'floater',
  'hole', 'puddle', 'sand', 'sign', 'slope', 'wall', 'windmill']);

export interface CourseMetadata { displayName: string; author: string; holes: number; par: number; holePars: number[] }

export function parseCourseBytes(content: Buffer): CourseMetadata {
  if (!content.length || content.length > MAX_COURSE_BYTES) throw Error('course exceeds the 4 MiB limit');
  const text = new TextDecoder('utf-8', { fatal: true }).decode(content);
  if (/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/u.test(text)) throw Error('course contains binary content');
  const groups = new Map<string, Map<string, string>>();
  let current: Map<string, string> | undefined;
  for (const line of text.replaceAll('\r\n', '\n').replaceAll('\r', '\n').split('\n')) {
    if (line.length > 4096) throw Error('course line is too long');
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#') || trimmed.startsWith(';')) continue;
    if (trimmed.startsWith('[')) {
      const match = /^(\d+)-([a-z]+)@(-?\d+),(-?\d+)(?:\|(\d+))?$/u.exec(trimmed.slice(1, -1));
      if (!trimmed.endsWith(']') || !match || !allowedTypes.has(match[2])
        || Number(match[1]) > 1000 || Math.abs(Number(match[3])) > 100000
        || Math.abs(Number(match[4])) > 100000 || groups.has(trimmed)) throw Error('unsupported course group');
      current = new Map(); groups.set(trimmed, current);
      if (groups.size > 4096) throw Error('course has too many objects');
      continue;
    }
    const equals = line.indexOf('=');
    if (!current || equals < 1 || !/^[A-Za-z0-9_@.\[\]-]+$/u.test(line.slice(0, equals).trim()))
      throw Error('malformed course property');
    const key = line.slice(0, equals).trim(), value = line.slice(equals + 1).trim();
    if (/plugin|script|exec/iu.test(key) || /\.(?:exe|dll|zip)$/iu.test(value))
      throw Error('executable or archive content is not allowed');
    current.set(key, value);
  }
  const header = groups.get('[0-course@-50,-50]');
  const displayName = header?.get('Name') ?? header?.get('name') ?? '';
  const author = header?.get('author') ?? '';
  if (!displayName || [...displayName].length > 64 || [...author].length > 128) throw Error('invalid course metadata');
  const holePars: number[] = [];
  for (let hole = 1; hole <= 1000; ++hole) {
    const group = groups.get(`[${hole}-hole@-50,-50|0]`);
    if (!group) break;
    const par = Number(group.get('par') ?? '3');
    if (!Number.isSafeInteger(par) || par < 0 || par > 1000
      || ![...groups.keys()].some(key => key.startsWith(`[${hole}-ball@`))
      || ![...groups.keys()].some(key => key.startsWith(`[${hole}-cup@`))) throw Error('invalid hole geometry');
    holePars.push(par);
  }
  if (!holePars.length || [...groups.keys()].some(key => {
    const match = /^\[(\d+)-hole@/u.exec(key);
    return match && Number(match[1]) > holePars.length;
  })) throw Error('course holes are not contiguous');
  return { displayName, author, holes: holePars.length, par: holePars.reduce((a, b) => a + b, 0), holePars };
}

export function readCourseManifest(root: string): CatalogFile[] {
  const lines = readFileSync(resolve(root, 'manifest.txt'), 'utf8').split(/\r?\n/u).filter(line => line && !line.startsWith('#'));
  const files = lines.map(line => {
    const parts = line.split('|');
    if (parts.length !== 2 || !/^[a-z0-9_-]+$/u.test(parts[0]) || !/^[A-Za-z0-9_.-]+$/u.test(parts[1]))
      throw Error('invalid course manifest entry');
    return { courseId: parts[0], fileName: parts[1] };
  });
  if (!files.length || new Set(files.map(file => file.courseId)).size !== files.length
    || new Set(files.map(file => file.fileName)).size !== files.length) throw Error('invalid shipped course manifest');
  return files;
}

export function courseHash(content: Buffer): string {
  const normalized = content.toString('utf8').replaceAll('\r\n', '\n').replaceAll('\r', '\n');
  return createHash('sha256').update(normalized).digest('hex');
}

export function loadCourseCatalog(root: string, files: CatalogFile[] = readCourseManifest(root)): CourseCatalogEntry[] {
  const catalogRoot = resolve(root);
  return files.map(file => {
    const path = resolve(catalogRoot, file.fileName);
    const fromRoot = relative(catalogRoot, path);
    if (!fromRoot || fromRoot.startsWith('..') || isAbsolute(fromRoot)) throw Error(`catalog path escapes root: ${file.fileName}`);
    const content = readFileSync(path);
    const metadata = parseCourseBytes(content);
    const expectedHash = courseHash(content);
    return { courseId: file.courseId, source: 'shipped', displayName: file.displayName ?? metadata.displayName,
      author: metadata.author, holes: metadata.holes, totalPar: metadata.par, byteSize: content.length,
      sha256: expectedHash, resourceName: file.fileName, expectedHash, par: metadata.holePars };
  });
}
