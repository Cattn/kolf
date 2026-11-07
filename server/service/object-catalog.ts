// SPDX-License-Identifier: GPL-2.0-or-later
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';

export type PropertyType = 'number' | 'int' | 'bool' | 'enum' | 'angle' | 'point' | 'pointf' | 'text';

export interface PropertySpec {
  key: string;
  type: PropertyType;
  min?: number;
  max?: number;
  values?: string[];
  maxLength?: number;
}

export interface ObjectCatalogType {
  id: string;
  name: string;
  category: string;
  affectsGameplay: boolean;
  addOnNewHole: boolean;
  properties: PropertySpec[];
}

export interface ObjectCatalog {
  catalogHash: string;
  structuralGroups: string[];
  types: ObjectCatalogType[];
  groups: Set<string>;
  byId: Map<string, ObjectCatalogType>;
}

const typeId = /^[a-z][a-z0-9_]{0,31}$/u;
const propertyTypes = new Set<string>(['number', 'int', 'bool', 'enum', 'angle', 'point', 'pointf', 'text']);
const booleanText = new Set(['true', 'false', 'on', 'off', 'yes', 'no', '1', '0']);

function finiteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value);
}

function parseProperty(value: unknown): PropertySpec {
  const spec = value as Record<string, unknown> | null;
  if (!spec || typeof spec.key !== 'string' || !/^[A-Za-z0-9_]{1,64}$/u.test(spec.key)
    || typeof spec.type !== 'string' || !propertyTypes.has(spec.type)) throw Error('object catalog has an invalid property');
  const result: PropertySpec = { key: spec.key, type: spec.type as PropertyType };
  if (['number', 'int', 'angle', 'point', 'pointf'].includes(result.type)) {
    if (!finiteNumber(spec.min) || !finiteNumber(spec.max) || spec.min > spec.max) throw Error('object catalog has an invalid range');
    result.min = spec.min; result.max = spec.max;
  }
  if (result.type === 'enum') {
    if (!Array.isArray(spec.values) || !spec.values.every(choice => typeof choice === 'string')) throw Error('object catalog has invalid choices');
    result.values = spec.values as string[];
  }
  if (result.type === 'text') {
    if (!finiteNumber(spec.maxLength) || spec.maxLength < 0) throw Error('object catalog has an invalid text limit');
    result.maxLength = spec.maxLength;
  }
  return result;
}

function numberText(text: string, min: number, max: number, integral: boolean): boolean {
  const trimmed = text.trim();
  if (integral ? !/^[+-]?\d+$/u.test(trimmed) : !/^[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?$/u.test(trimmed)) return false;
  const value = Number(trimmed);
  return Number.isFinite(value) && value >= min && value <= max;
}

export function acceptsPropertyText(spec: PropertySpec, text: string): boolean {
  switch (spec.type) {
    case 'number': return numberText(text, spec.min!, spec.max!, false);
    case 'int':
    case 'angle': return numberText(text, spec.min!, spec.max!, true);
    case 'bool': return booleanText.has(text.trim().toLowerCase());
    case 'enum': return spec.values!.includes(text.trim());
    case 'point':
    case 'pointf': {
      const parts = text.split(',');
      return parts.length === 2 && parts.every(part => numberText(part, spec.min!, spec.max!, spec.type === 'point'));
    }
    case 'text': return text.length <= spec.maxLength!;
  }
}

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
  const byId = new Map<string, ObjectCatalogType>();
  const checked = types.map((value: unknown) => {
    const type = value as Partial<ObjectCatalogType> | null;
    if (!type || typeof type.id !== 'string' || !typeId.test(type.id) || groups.has(type.id)
      || typeof type.name !== 'string' || typeof type.category !== 'string'
      || typeof type.affectsGameplay !== 'boolean' || typeof type.addOnNewHole !== 'boolean'
      || !Array.isArray(type.properties)) throw Error('object catalog has an invalid type');
    groups.add(type.id);
    const entry: ObjectCatalogType = { id: type.id, name: type.name, category: type.category, affectsGameplay: type.affectsGameplay,
      addOnNewHole: type.addOnNewHole, properties: type.properties.map(parseProperty) };
    byId.set(entry.id, entry);
    return entry;
  });
  return { catalogHash, structuralGroups, types: checked, groups, byId };
}

export const objectCatalog = parseObjectCatalog(
  readFileSync(new URL('../../protocol/object-catalog.json', import.meta.url), 'utf8'));
