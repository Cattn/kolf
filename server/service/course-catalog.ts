// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { isAbsolute, relative, resolve } from 'node:path';
import type { CourseCatalogEntry } from './lobby.ts';
import { acceptsPropertyText, objectCatalog, type ObjectCatalogType } from './object-catalog.ts';

export interface CatalogFile {
  courseId: string;
  fileName: string;
  displayName?: string;
}

export const MAX_COURSE_BYTES = 4 * 1024 * 1024;
export const COURSE_CHUNK_BYTES = 48 * 1024;

export interface CourseMetadata { displayName: string; author: string; holes: number; par: number; holePars: number[] }

export function parseCourseBytes(content: Buffer): CourseMetadata {
  if (!content.length || content.length > MAX_COURSE_BYTES) throw Error('course exceeds the 4 MiB limit');
  const text = new TextDecoder('utf-8', { fatal: true }).decode(content);
  if (/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/u.test(text)) throw Error('course contains binary content');
  const groups = new Map<string, Map<string, string>>();
  let current: Map<string, string> | undefined;
  let currentType: ObjectCatalogType | undefined;
  for (const line of text.replaceAll('\r\n', '\n').replaceAll('\r', '\n').split('\n')) {
    if (line.length > 4096) throw Error('course line is too long');
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#') || trimmed.startsWith(';')) continue;
    if (trimmed.startsWith('[')) {
      const match = /^(\d+)-([a-z][a-z0-9_]*)@(-?\d+),(-?\d+)(?:\|(\d+))?$/u.exec(trimmed.slice(1, -1));
      if (!trimmed.endsWith(']') || !match || !objectCatalog.groups.has(match[2])
        || Number(match[1]) > 1000 || Math.abs(Number(match[3])) > 100000
        || Math.abs(Number(match[4])) > 100000 || groups.has(trimmed)) throw Error('unsupported course group');
      current = new Map(); groups.set(trimmed, current); currentType = objectCatalog.byId.get(match[2]);
      if (groups.size > 4096) throw Error('course has too many objects');
      continue;
    }
    const equals = line.indexOf('=');
    if (!current || equals < 1 || !/^[A-Za-z0-9_@.\[\]-]+$/u.test(line.slice(0, equals).trim()))
      throw Error('malformed course property');
    const key = line.slice(0, equals).trim(), value = line.slice(equals + 1).trim();
    if (/plugin|script|exec/iu.test(key) || /\.(?:exe|dll|zip)$/iu.test(value))
      throw Error('executable or archive content is not allowed');
    const spec = currentType?.properties.find(property => property.key === key);
    if (spec && !acceptsPropertyText(spec, value)) throw Error(`course object property ${key} is out of range`);
    current.set(key, value);
  }
  const header = groups.get('[0-course@-50,-50]');
  const displayName = header?.get('Name') ?? header?.get('name') ?? '';
  const author = header?.get('author') ?? '';
  const formatVersion = header?.get('formatVersion');
  if (formatVersion !== undefined && !/^[12]$/u.test(formatVersion)) throw Error('course needs a newer version of Kolf');
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

export function normalizedCourseBytes(content: Buffer): Buffer {
  return Buffer.from(content.toString('utf8').replaceAll('\r\n', '\n').replaceAll('\r', '\n'), 'utf8');
}

export function courseHash(content: Buffer): string {
  return createHash('sha256').update(normalizedCourseBytes(content)).digest('hex');
}

const shippedBytes = new Map<string, Buffer>();

export function shippedCourseChunk(sha256: string, index: number) {
  const bytes = shippedBytes.get(sha256);
  if (!bytes || !Number.isSafeInteger(index) || index < 0 || index >= Math.ceil(bytes.length / COURSE_CHUNK_BYTES))
    return undefined;
  return { sha256, index, byteSize: bytes.length,
    data: bytes.subarray(index * COURSE_CHUNK_BYTES, (index + 1) * COURSE_CHUNK_BYTES).toString('base64') };
}

export function loadCourseCatalog(root: string, files: CatalogFile[] = readCourseManifest(root)): CourseCatalogEntry[] {
  const catalogRoot = resolve(root);
  return files.map(file => {
    const path = resolve(catalogRoot, file.fileName);
    const fromRoot = relative(catalogRoot, path);
    if (!fromRoot || fromRoot.startsWith('..') || isAbsolute(fromRoot)) throw Error(`catalog path escapes root: ${file.fileName}`);
    const content = readFileSync(path);
    const metadata = parseCourseBytes(content);
    const normalized = normalizedCourseBytes(content);
    const expectedHash = createHash('sha256').update(normalized).digest('hex');
    shippedBytes.set(expectedHash, normalized);
    return { courseId: file.courseId, source: 'shipped', displayName: file.displayName ?? metadata.displayName,
      author: metadata.author, holes: metadata.holes, totalPar: metadata.par, byteSize: normalized.length,
      sha256: expectedHash, resourceName: file.fileName, expectedHash, par: metadata.holePars };
  });
}
