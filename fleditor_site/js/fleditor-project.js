/* global window */
(function (global) {
  'use strict';

  const PROJECT_KEY = 'flatl-fleditor-project';
  const LEGACY_KEY = 'flatl-fleditor-source';
  const DEFAULT_MAIN = 'main.fl';
  const DEFAULT_SOURCE = "#main\n  io.outln('hello from fleditor')\n  OK\n";
  const MEMFS_ROOT = '/fleditor';

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
    saveProject(proj);
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
      Module.FS.writeFile(full, files[norm] != null ? files[norm] : files[rel]);
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
    buildTree,
    ensureParentDirs,
  };
})(window);
