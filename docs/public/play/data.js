// The player's Interstate '76 discs: picking them, checking them, and importing them into the site's origin
// private file system (OPFS). Nothing here is game data; everything comes from the user's own discs and
// stays on their device.
//
// The game (openi76.wasm, src/platform_gasm.c) reads:
//   disc 1   the asset "cd": an .iso (ISO 9660) or raw .bin of the disc; or, from a folder, its files as
//            assets named by their paths (I76.EXE, I76.ZFS, MISSIONS/...). The game checks I76.EXE's size
//            and CRC-32 (version 1.0) and reads its tables at run time.
//   disc 2   the asset "cd2", optional: the disc in the CD drive (the trip's and the training's missions
//            need it). Its data track as an .iso, or the whole disc as a raw .bin.
//   its cue  the asset "cd2cue", optional: with the .bin, the CUE sheet that places the 16 audio tracks,
//            the game's music.
// A source is { kind: 'image' | 'folder', name, entries: [[asset or path, File], ...] }.
import { directoryHandleEntries, fileListEntries } from './vendor/gasm/gasm-host.js';
import { clearNamespace, opfsFileSystem, persist } from '@emdzej/csfs-opfs';

/** OPFS directory the discs are imported into. */
export const NAMESPACE = 'openi76-data';
/** Written last: an import without it is incomplete. Hidden, so the game never sees it. */
const MARKER = '.openi76-import.json';
/** I76.EXE, version 1.0 (src/exe.c checks the size and CRC-32 4129421d). */
export const EXE = 'I76.EXE';
export const EXE_SIZE = 1019392;
/** What the game never reads off disc 1: the installer's DirectX and the demo. */
const SKIPPED = ['DIRECTX', 'DEMO'];

const upper = (s) => s.replace(/[a-z]/g, (c) => c.toUpperCase());
export const mb = (n) => (n >= 1 << 30 ? `${(n / 2 ** 30).toFixed(1)} GB` : `${(n / 1048576).toFixed(n < 10485760 ? 1 : 0)} MB`);

// ---- picking --------------------------------------------------------------------------

/** Disc 1's folder from showDirectoryPicker() (Chromium). */
export async function folderFromHandle(handle) {
  return checkFolder(handle.name, await directoryHandleEntries(handle));
}

/** Disc 1's folder from <input type="file" webkitdirectory> (all browsers). */
export function folderFromFileList(files) {
  const name = files[0]?.webkitRelativePath.split('/')[0] || 'folder';
  return checkFolder(name, fileListEntries(files));
}

/**
 * Disc 1's files copied off the disc (or the folder one above them): everything but DIRECTX and DEMO,
 * from wherever I76.EXE is. Returns { source, problems, files, bytes }.
 */
export function checkFolder(name, entries) {
  const problems = [];
  const exePath = entries.map(([p]) => p).filter((p) => upper(p.split('/').pop()) === EXE && p.split('/').length <= 2)
    .sort((a, b) => a.length - b.length)[0];
  const root = exePath && exePath.includes('/') ? exePath.slice(0, exePath.lastIndexOf('/') + 1) : '';
  const kept = [];
  if (!exePath) problems.push(`no ${EXE} in this folder: choose the folder with disc 1's files`);
  else {
    for (const [path, file] of entries) {
      if (!path.startsWith(root)) continue;
      const rel = path.slice(root.length);
      if (!SKIPPED.includes(upper(rel.split('/')[0])) || !rel.includes('/')) kept.push([rel, file]);
    }
    const byName = new Map(kept.map(([p, f]) => [upper(p), f]));
    const exe = byName.get(EXE);
    if (exe && exe.size !== EXE_SIZE) problems.push(`${EXE} is ${exe.size.toLocaleString('en')} bytes, not ${EXE_SIZE.toLocaleString('en')}: OpenI76 needs version 1.0, as on the disc`);
    if (!byName.has('I76.ZFS')) problems.push('I76.ZFS is missing');
    if (![...byName.keys()].some((p) => p.startsWith('MISSIONS/'))) problems.push('the MISSIONS folder is missing');
  }
  const bytes = kept.reduce((n, [, f]) => n + f.size, 0);
  const shown = root ? `${name}/${root.slice(0, -1)}` : name;
  return { source: { kind: 'folder', name: shown, entries: kept }, problems, files: kept.length, bytes };
}

/** Where the ISO 9660 volume descriptor ("CD001") is: 2048-byte sectors (.iso) or raw 2352-byte ones (.bin). */
const ISO_MAGIC = [16 * 2048 + 1, 16 * 2352 + 16 + 1, 16 * 2352 + 24 + 1];

/** The ISO 9660 volume's label (I76_CD1, I76_CD2), or null when there is no volume. */
async function volumeLabel(file) {
  for (const off of ISO_MAGIC) {
    if (file.size < off + 71) continue;
    const b = new Uint8Array(await file.slice(off, off + 71).arrayBuffer());
    if (String.fromCharCode(...b.subarray(0, 5)) === 'CD001') return String.fromCharCode(...b.subarray(39, 71)).trim();
  }
  return null;
}
const extOf = (f) => (/\.[^.]*$/.exec(f.name)?.[0] ?? '').toLowerCase();

/**
 * Picked disc image files: disc 1 (required), disc 2 and disc 2's .cue (optional), told apart by their
 * volumes' labels (I76_CD1, I76_CD2). Returns { source, problems, files, bytes }.
 */
export async function imagesFromFiles(files) {
  const problems = [];
  const entries = [];
  const images = [], cues = [];
  for (const f of files) {
    const ext = extOf(f);
    if (ext === '.cue') cues.push(f);
    else if (ext === '.iso' || ext === '.bin') images.push(f);
    else problems.push(`${f.name}: not an .iso, .bin or .cue`);
  }
  const labelled = [];
  for (const f of images) {
    const label = await volumeLabel(f);
    if (label === null) { problems.push(`${f.name}: no ISO 9660 file system in this image`); continue; }
    if (label !== 'I76_CD1' && label !== 'I76_CD2') { problems.push(`${f.name}: the disc "${label}", not Interstate '76's (I76_CD1, I76_CD2)`); continue; }
    labelled.push([f, label === 'I76_CD2' ? 2 : 1]);
  }
  const disc1 = labelled.filter(([, d]) => d === 1).map(([f]) => f);
  const disc2 = labelled.filter(([, d]) => d === 2).map(([f]) => f);
  if (disc1.length) entries.push(['cd', disc1[0]]);
  else problems.push('no image of disc 1: choose its .iso (or .bin)');
  if (disc1.length > 1 || disc2.length > 1) problems.push('more than one image for a disc: choose one per disc');
  if (disc2.length) entries.push(['cd2', disc2[0]]);
  if (cues.length) {
    if (!disc2.length || extOf(disc2[0]) !== '.bin') problems.push('a .cue sheet goes with disc 2\'s .bin (its audio tracks): choose the .bin too');
    else if (cues[0].size > 65536) problems.push(`${cues[0].name} is too big for a CUE sheet`);
    else entries.push(['cd2cue', cues[0]]);
  }
  const bytes = entries.reduce((n, [, f]) => n + f.size, 0);
  const name = entries.map(([, f]) => f.name).join(', ');
  return { source: { kind: 'image', name, entries }, problems, files: entries.length, bytes,
    disc2: disc2[0]?.name ?? null, music: entries.some(([k]) => k === 'cd2cue') };
}

// ---- OPFS import ----------------------------------------------------------------------

/** The completed import's description ({ kind, name, files, bytes, date }), or null. */
export async function existingImport() {
  try {
    const root = await navigator.storage.getDirectory();
    const dir = await root.getDirectoryHandle(NAMESPACE);
    const f = await (await dir.getFileHandle(MARKER)).getFile();
    return JSON.parse(await f.text());
  } catch {
    return null;
  }
}

export async function removeImport() {
  await clearNamespace(NAMESPACE);
  try { await (await navigator.storage.getDirectory()).removeEntry(NAMESPACE, { recursive: true }); } catch {}
}

/** Ask the browser to keep the data under storage pressure. Firefox may never answer a prompt. */
export async function requestPersist() {
  return persist({ signal: AbortSignal.timeout(10000) }).catch(() => false);
}

export async function storageInfo() {
  const est = await navigator.storage?.estimate?.().catch(() => null);
  const persisted = await navigator.storage?.persisted?.().catch(() => false);
  return { usage: est?.usage, quota: est?.quota, persisted: !!persisted };
}

/**
 * Copy a checked source into OPFS (csfs writes each file through a stream, so a large file never sits
 * in memory). The marker goes last. onProgress({ bytes, total, file, files, done }).
 */
export async function importSource(source, onProgress = () => {}) {
  const items = source.entries;
  const total = items.reduce((n, [, f]) => n + f.size, 0);
  await persist({ signal: AbortSignal.timeout(3000) }).catch(() => false);
  await removeImport();
  const fs = await opfsFileSystem({ namespace: NAMESPACE });
  let bytes = 0, last = 0;
  for (let i = 0; i < items.length; i++) {
    const [path, file] = items[i];
    const counted = file.stream().pipeThrough(new TransformStream({
      transform(chunk, ctrl) {
        bytes += chunk.byteLength;
        const now = performance.now();
        if (now - last > 100) { last = now; onProgress({ bytes, total, file: path, files: items.length, done: i }); }
        ctrl.enqueue(chunk);
      },
    }));
    await fs.write(`/${path}`, counted);
    onProgress({ bytes, total, file: path, files: items.length, done: i + 1 });
  }
  const info = { kind: source.kind, name: source.name, files: items.length, bytes: total, date: new Date().toISOString() };
  await fs.write(`/${MARKER}`, new TextEncoder().encode(JSON.stringify(info)));
  return info;
}

// ---- running --------------------------------------------------------------------------

/** gasm Worker-mode asset specs for a source ('opfs' = the imported data). */
export function assetSpecs(source) {
  if (source === 'opfs') return [{ kind: 'opfs', dir: NAMESPACE }];
  return [{ kind: 'files', entries: source.entries }];
}
