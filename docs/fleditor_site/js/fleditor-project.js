/* global window */
(function (global) {
  'use strict';

  const PROJECT_KEY = 'flatl-fleditor-project';
  const LEGACY_KEY = 'flatl-fleditor-source';
  const DEFAULT_MAIN = 'main.fl';
  const DEFAULT_SOURCE = "#main\n  io.outln('hello from fleditor')\n  OK\n";
  const MEMFS_ROOT = '/fleditor';
  const EXAMPLES_MANIFEST = 'examples/manifest.json';
  const EXAMPLES_PREFIX = 'examples/';

  function normalizePath(raw) {
    if (!raw || typeof raw !== 'string') {
      return '';
    }
    let p = raw.replace(/\\/g, '/').trim();
    while (p.startsWith('/')) {
      p = p.slice(1);
    }
    const parts = p.split('/').filter(function (seg) {
      return seg && seg !== '.' && seg !== '..';
    });
    return parts.join('/');
  }

  function defaultProject() {
    return {
      version: 1,
      openPath: DEFAULT_MAIN,
      files: { [DEFAULT_MAIN]: DEFAULT_SOURCE },
      dirs: [],
    };
  }

  function loadProject() {
    const raw = localStorage.getItem(PROJECT_KEY);
    if (raw) {
      try {
        const data = JSON.parse(raw);
        if (data && data.files && typeof data.files === 'object') {
          data.openPath = normalizePath(data.openPath) || DEFAULT_MAIN;
          data.dirs = Array.isArray(data.dirs) ? data.dirs.map(normalizePath).filter(Boolean) : [];
          const files = {};
          for (const k of Object.keys(data.files)) {
            const nk = normalizePath(k);
            if (nk) {
              files[nk] = String(data.files[k]);
            }
          }
          data.files = files;
          if (!files[data.openPath]) {
            const keys = Object.keys(files);
            data.openPath = keys[0] || DEFAULT_MAIN;
            if (!files[data.openPath]) {
              files[data.openPath] = DEFAULT_SOURCE;
            }
          }
          return data;
        }
      } catch (_e) {
        /* fall through */
      }
    }
    const legacy = localStorage.getItem(LEGACY_KEY);
    if (legacy) {
      const proj = defaultProject();
      proj.files[DEFAULT_MAIN] = legacy;
      saveProject(proj);
      localStorage.removeItem(LEGACY_KEY);
      return proj;
    }
    const proj = defaultProject();
    return proj;
  }

  function saveProject(project) {
    localStorage.setItem(
      PROJECT_KEY,
      JSON.stringify({
        version: 1,
        openPath: project.openPath,
        files: project.files,
        dirs: project.dirs || [],
      })
    );
  }

  function memfsEntryPath(relPath) {
    const norm = normalizePath(relPath);
    return MEMFS_ROOT + '/' + norm;
  }

  /** @returns {string[]} relative paths under MEMFS_ROOT */
  function listMemfsRelFiles(Module) {
    const out = [];
    if (!Module.FS) {
      return out;
    }
    function walk(absDir, relPrefix) {
      let entries;
      try {
        entries = Module.FS.readdir(absDir);
      } catch (_e) {
        return;
      }
      for (let i = 0; i < entries.length; i++) {
        const name = entries[i];
        if (name === '.' || name === '..') {
          continue;
        }
        const abs = absDir + '/' + name;
        const rel = relPrefix ? relPrefix + '/' + name : name;
        let stat;
        try {
          stat = Module.FS.stat(abs);
        } catch (_e2) {
          continue;
        }
        if (Module.FS.isDir(stat.mode)) {
          walk(abs, rel);
        } else {
          out.push(rel);
        }
      }
    }
    walk(MEMFS_ROOT, '');
    return out;
  }

  const BINARY_STORE_PREFIX = 'FLEDITOR::BINARY::BASE64::\n';

  function isBinaryProjectValue(content) {
    return typeof content === 'string' && content.startsWith(BINARY_STORE_PREFIX);
  }

  function bytesEqual(a, b) {
    if (!a || !b || a.length !== b.length) {
      return false;
    }
    for (let i = 0; i < a.length; i++) {
      if (a[i] !== b[i]) {
        return false;
      }
    }
    return true;
  }

  function readMemfsFileBytes(Module, rel) {
    return Module.FS.readFile(memfsEntryPath(rel));
  }

  /** @returns {Map<string, Uint8Array>} */
  function snapshotMemfsBytes(Module) {
    const map = new Map();
    if (!Module.FS) {
      return map;
    }
    const rels = listMemfsRelFiles(Module);
    for (let i = 0; i < rels.length; i++) {
      const rel = rels[i];
      try {
        map.set(rel, readMemfsFileBytes(Module, rel));
      } catch (_e) {
        /* skip unreadable */
      }
    }
    return map;
  }

  function bytesToProjectValueBinary(u8) {
    let bin = '';
    for (let i = 0; i < u8.length; i++) {
      bin += String.fromCharCode(u8[i]);
    }
    return BINARY_STORE_PREFIX + btoa(bin);
  }

  /** Binary when stored with the project wrapper (e.g. created with +file → Binary). */
  function isBinaryProjectFile(_path, content) {
    return isBinaryProjectValue(content);
  }

  function bytesToProjectValue(u8) {
    try {
      const text = new TextDecoder('utf-8', { fatal: true }).decode(u8);
      if (text.indexOf('\0') >= 0) {
        throw new Error('binary');
      }
      return text;
    } catch (_e) {
      let bin = '';
      for (let i = 0; i < u8.length; i++) {
        bin += String.fromCharCode(u8[i]);
      }
      return BINARY_STORE_PREFIX + btoa(bin);
    }
  }

  function projectValueToMemfsPayload(content) {
    if (isBinaryProjectValue(content)) {
      const raw = atob(content.slice(BINARY_STORE_PREFIX.length));
      const u8 = new Uint8Array(raw.length);
      for (let i = 0; i < raw.length; i++) {
        u8[i] = raw.charCodeAt(i);
      }
      return u8;
    }
    return content;
  }

  /**
   * Pull new or changed files from MEMFS into project.files (text or binary wrapper).
   * @returns {{ path: string, kind: 'new'|'updated' }[]}
   */
  function syncMemfsChangesToProject(Module, project, beforeBytesMap) {
    const report = [];
    if (!Module.FS) {
      return report;
    }
    const rels = listMemfsRelFiles(Module);
    for (let i = 0; i < rels.length; i++) {
      const rel = rels[i];
      let after;
      try {
        after = readMemfsFileBytes(Module, rel);
      } catch (_e) {
        continue;
      }
      const before = beforeBytesMap.get(rel);
      if (before && bytesEqual(before, after)) {
        continue;
      }
      project.files[rel] = bytesToProjectValue(after);
      ensureParentDirs(project, rel);
      report.push({ path: rel, kind: before ? 'updated' : 'new' });
    }
    return report;
  }

  function formatMemfsImportReport(report) {
    if (!report || report.length === 0) {
      return '';
    }
    const parts = report.map(function (r) {
      return r.path + (r.kind === 'updated' ? ' (updated)' : ' (new)');
    });
    return '[project] synced: ' + parts.join(', ');
  }

  function binaryProjectValueToBytes(content) {
    const payload = projectValueToMemfsPayload(content);
    return payload instanceof Uint8Array ? payload : new Uint8Array(0);
  }

  /** Hex dump (full file); prefer paginated view in editor via FleditorHex.hexDumpPageFromBytes. */
  function binaryProjectValueToHexView(content) {
    const u8 = binaryProjectValueToBytes(content);
    const H = global.FleditorHex;
    if (H) {
      return H.hexDumpAllFromBytes(u8);
    }
    return '';
  }

  function binaryProjectValueToBytesExport(content) {
    return binaryProjectValueToBytes(content);
  }

  function syncToMemfs(Module, files) {
    if (!Module.FS) {
      throw new Error('WASM FS not available');
    }
    if (typeof Module.FS.rmTree === 'function') {
      try {
        Module.FS.rmTree(MEMFS_ROOT);
      } catch (_e) {
        /* missing root */
      }
    }
    Module.FS.mkdirTree(MEMFS_ROOT);
    for (const rel of Object.keys(files)) {
      const norm = normalizePath(rel);
      if (!norm) {
        continue;
      }
      const full = MEMFS_ROOT + '/' + norm;
      const slash = full.lastIndexOf('/');
      if (slash > MEMFS_ROOT.length) {
        Module.FS.mkdirTree(full.slice(0, slash));
      }
      const raw = files[norm] != null ? files[norm] : files[rel];
      Module.FS.writeFile(full, projectValueToMemfsPayload(raw));
    }
  }

  /** @returns {{ name: string, path: string, kind: 'dir'|'file', children?: unknown[] }[]} */
  function buildTree(project) {
    const root = { name: '', path: '', kind: 'dir', children: new Map() };

    function ensureDir(parts) {
      let node = root;
      let acc = '';
      for (const part of parts) {
        acc = acc ? acc + '/' + part : part;
        if (!node.children.has(part)) {
          node.children.set(part, { name: part, path: acc, kind: 'dir', children: new Map() });
        }
        node = node.children.get(part);
      }
      return node;
    }

    for (const d of project.dirs || []) {
      const norm = normalizePath(d);
      if (norm) {
        ensureDir(norm.split('/'));
      }
    }

    for (const filePath of Object.keys(project.files)) {
      const norm = normalizePath(filePath);
      if (!norm) {
        continue;
      }
      const parts = norm.split('/');
      const fileName = parts.pop();
      const dirNode = parts.length ? ensureDir(parts) : root;
      dirNode.children.set(fileName, { name: fileName, path: norm, kind: 'file' });
    }

    function toArray(node) {
      if (!node.children) {
        return [];
      }
      const list = Array.from(node.children.values());
      list.sort(function (a, b) {
        if (a.kind !== b.kind) {
          return a.kind === 'dir' ? -1 : 1;
        }
        return a.name.localeCompare(b.name);
      });
      return list.map(function (n) {
        if (n.kind === 'dir') {
          return { name: n.name, path: n.path, kind: 'dir', children: toArray(n) };
        }
        return { name: n.name, path: n.path, kind: 'file' };
      });
    }

    return toArray(root);
  }

  function listFilePaths(project) {
    return Object.keys(project.files)
      .map(normalizePath)
      .filter(Boolean)
      .sort();
  }

  /** Files exactly under folder prefix (path/to/file.fl), not the folder path itself. */
  function filesUnderFolder(project, dirPath) {
    const norm = normalizePath(dirPath);
    if (!norm) {
      return [];
    }
    const pre = norm + '/';
    return listFilePaths(project).filter(function (f) {
      return f.startsWith(pre);
    });
  }

  function removeFile(project, path) {
    const norm = normalizePath(path);
    if (norm) {
      delete project.files[norm];
    }
  }

  /** Removes folder marker and all files under prefix. Returns number of files removed. */
  function removeFolder(project, dirPath) {
    const norm = normalizePath(dirPath);
    if (!norm) {
      return 0;
    }
    const pre = norm + '/';
    let n = 0;
    for (const k of listFilePaths(project)) {
      if (k.startsWith(pre)) {
        delete project.files[k];
        n++;
      }
    }
    project.dirs = (project.dirs || []).filter(function (d) {
      const dn = normalizePath(d);
      return dn !== norm && !dn.startsWith(pre);
    });
    return n;
  }

  function removeEmptyFolder(project, dirPath) {
    const norm = normalizePath(dirPath);
    if (!norm) {
      return false;
    }
    if (filesUnderFolder(project, norm).length > 0) {
      return false;
    }
    project.dirs = (project.dirs || []).filter(function (d) {
      return normalizePath(d) !== norm;
    });
    return true;
  }

  function exampleProjectPath(relativeToExamplesDir) {
    const norm = normalizePath(relativeToExamplesDir);
    if (!norm) {
      return '';
    }
    if (norm.startsWith(EXAMPLES_PREFIX)) {
      return norm;
    }
    return EXAMPLES_PREFIX + norm;
  }

  function mergeExampleFilesIntoProject(project, filesByProjectPath) {
    for (const path of Object.keys(filesByProjectPath)) {
      const norm = exampleProjectPath(path);
      if (!norm) {
        continue;
      }
      project.files[norm] = filesByProjectPath[path];
      ensureParentDirs(project, norm);
    }
  }

  async function fetchExampleFilesByProjectPath() {
    const fetchFn = global.fetch;
    if (typeof fetchFn !== 'function') {
      return {};
    }
    const manifestRes = await fetchFn(EXAMPLES_MANIFEST);
    if (!manifestRes.ok) {
      return {};
    }
    const manifest = await manifestRes.json();
    const list = manifest && Array.isArray(manifest.files) ? manifest.files : [];
    const out = {};
    for (const rel of list) {
      const entry = normalizePath(String(rel));
      if (!entry) {
        continue;
      }
      const url = EXAMPLES_PREFIX + entry;
      const projectPath = exampleProjectPath(entry);
      const fileRes = await fetchFn(url);
      if (!fileRes.ok) {
        continue;
      }
      out[projectPath] = await fileRes.text();
    }
    return out;
  }

  async function hydrateProjectExamples(project) {
    const files = await fetchExampleFilesByProjectPath();
    mergeExampleFilesIntoProject(project, files);
    return project;
  }

  async function resetProjectWithExamples() {
    const proj = defaultProject();
    await hydrateProjectExamples(proj);
    saveProject(proj);
    return proj;
  }

  function ensureParentDirs(project, filePath) {
    const norm = normalizePath(filePath);
    const parts = norm.split('/');
    parts.pop();
    if (!parts.length) {
      return;
    }
    const dirPath = parts.join('/');
    project.dirs = project.dirs || [];
    if (!project.dirs.includes(dirPath)) {
      project.dirs.push(dirPath);
    }
  }

  global.FleditorProject = {
    PROJECT_KEY,
    DEFAULT_MAIN,
    DEFAULT_SOURCE,
    MEMFS_ROOT,
    normalizePath,
    defaultProject,
    loadProject,
    saveProject,
    memfsEntryPath,
    syncToMemfs,
    listMemfsRelFiles,
    snapshotMemfsBytes,
    syncMemfsChangesToProject,
    formatMemfsImportReport,
    isBinaryProjectValue,
    binaryProjectValueToHexView,
    binaryProjectValueToBytes: binaryProjectValueToBytesExport,
    bytesToProjectValue,
    bytesToProjectValueBinary,
    isBinaryProjectFile,
    buildTree,
    ensureParentDirs,
    listFilePaths,
    filesUnderFolder,
    removeFile,
    removeFolder,
    removeEmptyFolder,
    EXAMPLES_MANIFEST,
    EXAMPLES_PREFIX,
    exampleProjectPath,
    fetchExampleFilesByProjectPath,
    mergeExampleFilesIntoProject,
    hydrateProjectExamples,
    resetProjectWithExamples,
  };
})(typeof window !== 'undefined' ? window : globalThis);
