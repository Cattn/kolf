// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash } from 'node:crypto';

export interface CompatibilityInput { path: string; content: string | Buffer }

const sha256 = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');

export function normalizedPath(path: string): string {
  const result = path.replaceAll('\\', '/').replace(/^\.\//, '');
  if (!result || result.startsWith('/') || result.includes('../') || result.includes('\n') || result.includes('\r'))
    throw Error(`invalid compatibility path: ${path}`);
  return result;
}

export function compatibilityIdentity(inputs: CompatibilityInput[]): string {
  const records = inputs.map(input => {
    const path = normalizedPath(input.path);
    const text = Buffer.isBuffer(input.content) ? input.content.toString('utf8') : input.content;
    const normalized = text.replaceAll('\r\n', '\n').replaceAll('\r', '\n');
    return { path, hash: sha256(normalized) };
  }).sort((a, b) => Buffer.compare(Buffer.from(a.path), Buffer.from(b.path)));
  if (!records.length || new Set(records.map(record => record.path)).size !== records.length) throw Error('compatibility inputs must be unique and nonempty');
  return sha256(`kolf-rules-v2\n${records.map(record => `${record.path}\n${record.hash}\n`).join('')}`);
}
