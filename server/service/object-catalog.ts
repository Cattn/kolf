// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';

export interface ObjectCatalogType {
  id: string;
  name: string;
  category: string;
  affectsGameplay: boolean;
  addOnNewHole: boolean;
}

export interface ObjectCatalog {
  catalogHash: string;
  structuralGroups: string[];
  types: ObjectCatalogType[];
  groups: Set<string>;
}

const typeId = /^[a-z][a-z0-9_]{0,31}$/u;

export function parseObjectCatalog(text: string): ObjectCatalog {
  const parsed: unknown = JSON.parse(text);
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) throw Error('object catalog is not an object');
  const { catalogHash, ...hashed } = parsed as Record<string, unknown>;
  const { catalogVersion, structuralGroups, types } = hashed;
  if (catalogVersion !== 1) throw Error('unsupported object catalog version');
  if (typeof catalogHash !== 'string' || createHash('sha256').update(JSON.stringify(hashed)).digest('hex') !== catalogHash)
    throw Error('object catalog hash does not match its content');
  if (!Array.isArray(structuralGroups) || !structuralGroups.every(group => typeof group === 'string' && typeId.test(group)))
    throw Error('object catalog has invalid structural groups');
  if (!Array.isArray(types)) throw Error('object catalog has no types');
  const groups = new Set<string>(structuralGroups);
  const checked = types.map((value: unknown) => {
    const type = value as Partial<ObjectCatalogType> | null;
    if (!type || typeof type.id !== 'string' || !typeId.test(type.id) || groups.has(type.id)
      || typeof type.name !== 'string' || typeof type.category !== 'string'
      || typeof type.affectsGameplay !== 'boolean' || typeof type.addOnNewHole !== 'boolean')
      throw Error('object catalog has an invalid type');
    groups.add(type.id);
    return { id: type.id, name: type.name, category: type.category, affectsGameplay: type.affectsGameplay, addOnNewHole: type.addOnNewHole };
  });
  return { catalogHash, structuralGroups, types: checked, groups };
}

export const objectCatalog = parseObjectCatalog(
  readFileSync(new URL('../../protocol/object-catalog.json', import.meta.url), 'utf8'));
