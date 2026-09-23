// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash } from 'node:crypto';
import { COURSE_CHUNK_BYTES, MAX_COURSE_BYTES, parseCourseBytes } from './course-catalog.ts';
import { LobbyError } from './errors.ts';
import type { CourseCatalogEntry } from './lobby.ts';

const MAX_UPLOADS = 3;
const UPLOAD_TIMEOUT_MS = 30_000;

interface Upload {
  id: string;
  sha256: string;
  byteSize: number;
  chunks: Buffer[];
  lastAt: number;
}

export class CourseTransfer {
  private upload?: Upload;
  private readonly completed = new Map<string, { descriptor: CourseCatalogEntry; bytes: Buffer }>();
  private readonly completedIds = new Map<string, string>();
  private readonly now: () => number;
  constructor(now: () => number) { this.now = now; }

  expire() {
    if (this.upload && this.now() - this.upload.lastAt > UPLOAD_TIMEOUT_MS) this.upload = undefined;
  }

  begin(uploadId: string, sha256: string, byteSize: number) {
    this.expire();
    const completed = this.completed.get(sha256);
    if (completed) {
      if (completed.bytes.length !== byteSize) throw new LobbyError('UploadConflict', 'course hash and size conflict');
      return { nextIndex: Math.ceil(byteSize / COURSE_CHUNK_BYTES), descriptor: completed.descriptor };
    }
    if (this.upload) {
      if (this.upload.id !== uploadId || this.upload.sha256 !== sha256 || this.upload.byteSize !== byteSize)
        throw new LobbyError('UploadBusy', 'another course upload is in progress');
      this.upload.lastAt = this.now();
      return { nextIndex: this.upload.chunks.length };
    }
    if (this.completed.size >= MAX_UPLOADS) throw new LobbyError('UploadLimit', 'this lobby has reached its custom-course limit');
    if (!Number.isSafeInteger(byteSize) || byteSize < 1 || byteSize > MAX_COURSE_BYTES)
      throw new LobbyError('InvalidCourseSize', 'course exceeds the 4 MiB limit');
    this.upload = { id: uploadId, sha256, byteSize, chunks: [], lastAt: this.now() };
    return { nextIndex: 0 };
  }

  chunk(uploadId: string, index: number, data: string) {
    this.expire();
    const upload = this.upload;
    if (!upload || upload.id !== uploadId) throw new LobbyError('UploadExpired', 'course upload expired');
    const expectedCount = Math.ceil(upload.byteSize / COURSE_CHUNK_BYTES);
    if (!Number.isSafeInteger(index) || index < 0 || index >= expectedCount
      || data.length > Math.ceil(COURSE_CHUNK_BYTES / 3) * 4 || !/^[A-Za-z0-9+/]+={0,2}$/u.test(data))
      return this.discard('invalid course chunk');
    const bytes = Buffer.from(data, 'base64');
    const expectedSize = Math.min(COURSE_CHUNK_BYTES, upload.byteSize - index * COURSE_CHUNK_BYTES);
    if (bytes.length !== expectedSize || bytes.toString('base64') !== data)
      return this.discard('course chunk has incorrect size or encoding');
    if (index < upload.chunks.length) {
      if (!upload.chunks[index].equals(bytes)) return this.discard('conflicting course chunk');
    } else if (index === upload.chunks.length) upload.chunks.push(bytes);
    else return this.discard('course chunks arrived out of order');
    upload.lastAt = this.now();
    return { nextIndex: upload.chunks.length };
  }

  finish(uploadId: string) {
    this.expire();
    const completedHash = this.completedIds.get(uploadId);
    if (completedHash) return this.completed.get(completedHash)!.descriptor;
    const upload = this.upload;
    if (!upload || upload.id !== uploadId) throw new LobbyError('UploadExpired', 'course upload expired');
    if (upload.chunks.length !== Math.ceil(upload.byteSize / COURSE_CHUNK_BYTES))
      return this.discard('course upload is incomplete');
    const bytes = Buffer.concat(upload.chunks);
    if (bytes.length !== upload.byteSize || createHash('sha256').update(bytes).digest('hex') !== upload.sha256)
      return this.discard('course hash does not match uploaded bytes');
    let metadata;
    try { metadata = parseCourseBytes(bytes); }
    catch (error) { return this.discard(error instanceof Error ? error.message : 'invalid course'); }
    const courseId = `upload_${upload.sha256.slice(0, 24)}`;
    const descriptor: CourseCatalogEntry = {
      courseId, source: 'uploaded', displayName: metadata.displayName, author: metadata.author,
      holes: metadata.holes, totalPar: metadata.par, par: metadata.holePars,
      sha256: upload.sha256, expectedHash: upload.sha256, byteSize: upload.byteSize,
    };
    this.completed.set(upload.sha256, { descriptor, bytes });
    this.completedIds.set(upload.id, upload.sha256);
    this.upload = undefined;
    return descriptor;
  }

  get(sha256: string, index: number) {
    const course = this.completed.get(sha256);
    if (!course || !Number.isSafeInteger(index) || index < 0
      || index >= Math.ceil(course.bytes.length / COURSE_CHUNK_BYTES))
      throw new LobbyError('CourseUnavailable', 'custom course chunk is unavailable');
    return { sha256, index, byteSize: course.bytes.length,
      data: course.bytes.subarray(index * COURSE_CHUNK_BYTES, (index + 1) * COURSE_CHUNK_BYTES).toString('base64') };
  }

  private discard(reason: string): never {
    this.upload = undefined;
    throw new LobbyError('InvalidCourseUpload', reason);
  }
}
