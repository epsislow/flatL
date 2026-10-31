/* global FlatLModule, CodeMirror, Terminal */
(function () {
  'use strict';

  const STORAGE_KEY = 'flatl-fleditor-source';
  const DEFAULT_SOURCE = "#main\n  io.outln('hello from fleditor')\n  OK\n";

  const IO_OK = 0;
  const IO_PANIC_EOF = 2;
  const IO_PANIC_CANCEL = 3;

  const IO_PROMPT_MAX = 32;
  const IO_KIND_SUFFIX = {
    0: ' (ln)',
    1: ' (num)',
    2: ' (bool)',
    3: ' (list)',
    4: ' (obj)',
    5: ' (bools)',
  };

  const statusEl = document.getElementById('status');
  const bannerEl = document.getElementById('wasmBanner');
  const runBtn = document.getElementById('runBtn');
  const clearBtn = document.getElementById('clearBtn');

  let wasmModule = null;
  let wasmLoadPromise = null;
  let running = false;
  let term = null;

  let ioDialog = null;
  let ioForm = null;
  let ioTitle = null;
  let ioHint = null;
  let ioInput = null;
  let ioCancelBtn = null;
  /** @type {null | ((line: string, status: number) => void)} */
  let ioPendingWake = null;
  let ioAllowEmpty = false;
  let ioCharMode = false;

  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.classList.toggle('fleditor-status--error', !!isError);
  }

  function showBanner(msg) {
    bannerEl.hidden = !msg;
    bannerEl.textContent = msg || '';
  }

  function initIoModal() {
    ioDialog = document.getElementById('ioModal');
    ioForm = document.getElementById('ioModalForm');
    ioTitle = document.getElementById('ioModalTitle');
    ioHint = document.getElementById('ioModalHint');
    ioInput = document.getElementById('ioModalInput');
    ioCancelBtn = document.getElementById('ioModalCancel');
    if (!ioDialog || !ioForm || !ioInput) {
      return;
    }

    ioForm.addEventListener('submit', function (e) {
      e.preventDefault();
      submitIoModal();
    });
    ioCancelBtn.addEventListener('click', function () {
      dismissIoModal(null);
    });
    ioDialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      dismissIoModal(null);
    });
  }

  function submitIoModal() {
    if (!ioPendingWake) {
      ioDialog.close();
      return;
    }
    const raw = ioInput.value;
    if (ioCharMode) {
      if (raw.length === 0) {
        dismissIoModal(null);
        return;
      }
      const ch = raw.charAt(0);
      if (term) term.write(ch);
      finishIoWake(ch, IO_OK);
      return;
    }
    dismissIoModal(raw);
  }

  function dismissIoModal(value) {
    if (!ioPendingWake) {
      if (ioDialog.open) ioDialog.close();
      return;
    }
    if (value === null) {
      finishIoWake('', IO_PANIC_EOF);
      return;
    }
    if (!ioAllowEmpty && value.trim() === '') {
      finishIoWake('', IO_PANIC_CANCEL);
      return;
    }
    if (term) term.write(value + '\r\n');
    finishIoWake(value, IO_OK);
  }

  function truncateIoPrompt(text) {
    const s = (text || '').trim();
    if (s.length <= IO_PROMPT_MAX) {
      return s;
    }
    return s.slice(0, IO_PROMPT_MAX) + '…';
  }

  function formatLineModalTitle(prompt, kind) {
    const suffix = IO_KIND_SUFFIX[kind] || ' (ln)';
    const p = truncateIoPrompt(prompt);
    if (!p) {
      return 'Input:' + suffix;
    }
    return p + suffix;
  }

  function formatCharModalTitle(prompt) {
    const p = truncateIoPrompt(prompt);
    if (!p) {
      return 'Input:';
    }
    return p;
  }

  function parseNeedLineHook(allowEmpty, a, b, c) {
    if (typeof a === 'function') {
      return { allowEmpty: allowEmpty, prompt: '', kind: 0, wake: a };
    }
    return { allowEmpty: allowEmpty, prompt: a || '', kind: b | 0, wake: c };
  }

  function parseNeedCharHook(a, b) {
    if (typeof a === 'function') {
      return { prompt: '', wake: a };
    }
    return { prompt: a || '', wake: b };
  }

  function finishIoWake(line, status) {
    const wake = ioPendingWake;
    ioPendingWake = null;
    ioCharMode = false;
    ioDialog.close();
    setStatus('running');
    if (wake) wake(line, status);
  }

  function openIoModal(options) {
    if (!ioDialog) {
      const line = window.prompt(options.title, '');
      if (line === null) {
        options.wake('', IO_PANIC_EOF);
      } else if (!options.allowEmpty && line.trim() === '') {
        options.wake('', IO_PANIC_CANCEL);
      } else {
        if (term && !options.charMode) term.write(line + '\r\n');
        else if (term && options.charMode && line.length) term.write(line.charAt(0));
        options.wake(options.charMode ? line.charAt(0) : line, IO_OK);
      }
      setStatus('running');
      return;
    }

    ioPendingWake = options.wake;
    ioAllowEmpty = !!options.allowEmpty;
    ioCharMode = !!options.charMode;
    ioTitle.textContent = options.title;
    if (ioHint) {
      ioHint.hidden = true;
      ioHint.textContent = '';
    }
    ioInput.value = '';
    ioInput.maxLength = options.charMode ? 8 : 8000;
    ioDialog.showModal();
    ioInput.focus();
    ioInput.select();
  }

  function initEditor() {
    const saved = localStorage.getItem(STORAGE_KEY);
    document.getElementById('code').value = saved || DEFAULT_SOURCE;
    const cm = CodeMirror.fromTextArea(document.getElementById('code'), {
      lineNumbers: true,
      theme: 'flatl-dark',
      mode: 'flatl',
      indentUnit: 2,
      tabSize: 2,
    });
    cm.on('change', () => localStorage.setItem(STORAGE_KEY, cm.getValue()));
    return cm;
  }

  function initTerminal() {
    term = new Terminal({
      theme: {
        background: '#1c1c22',
        foreground: '#e6dcc8',
        cursor: '#e6dcc8',
      },
      fontFamily: 'ui-monospace, Menlo, Consolas, monospace',
      fontSize: 14,
      cursorBlink: true,
      convertEol: true,
    });
    const host = document.getElementById('terminalHost');
    term.open(host);
    host.addEventListener('mousedown', function () {
      term.focus();
    });
  }

  /** Hooks must live on the object passed to FlatLModule (Asyncify reads Module[...]). */
  function wasmModuleConfig() {
    return {
      locateFile: (p) => (p.endsWith('.wasm') ? 'wasm/' + p : p),
      flatWasmOnStdout: function (u8) {
        const text = new TextDecoder().decode(u8);
        if (term) term.write(text);
      },
      flatWasmOnNeedLine: function (allowEmpty, prompt, kind, wake) {
        const io = parseNeedLineHook(allowEmpty, prompt, kind, wake);
        setStatus(io.allowEmpty ? 'waiting input (line, empty ok)' : 'waiting input (line)');
        openIoModal({
          title: formatLineModalTitle(io.prompt, io.kind),
          allowEmpty: io.allowEmpty,
          charMode: false,
          wake: io.wake,
        });
      },
      flatWasmOnNeedChar: function (prompt, wake) {
        const io = parseNeedCharHook(prompt, wake);
        setStatus('waiting input (char)');
        openIoModal({
          title: formatCharModalTitle(io.prompt),
          allowEmpty: false,
          charMode: true,
          wake: io.wake,
        });
      },
    };
  }

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = src;
      s.onload = () => resolve();
      s.onerror = () => reject(new Error('failed to load ' + src));
      document.body.appendChild(s);
    });
  }

  function attachWasmHooks(Module) {
    const cfg = wasmModuleConfig();
    Module.flatWasmOnStdout = cfg.flatWasmOnStdout;
    Module.flatWasmOnNeedLine = cfg.flatWasmOnNeedLine;
    Module.flatWasmOnNeedChar = cfg.flatWasmOnNeedChar;
    Module.locateFile = cfg.locateFile;
    return Module;
  }

  async function loadWasm() {
    if (wasmLoadPromise) {
      return wasmLoadPromise;
    }
    wasmLoadPromise = (async () => {
      if (typeof FlatLModule !== 'function') {
        try {
          await loadScript('wasm/flatl.js');
        } catch (_e) {
          showBanner('WASM not built. Run: make -f Makefile.wasm fleditor-wasm');
          return null;
        }
      }
      if (typeof FlatLModule !== 'function') {
        showBanner('WASM not built. Run: make -f Makefile.wasm fleditor-wasm');
        return null;
      }
      try {
        const cfg = wasmModuleConfig();
        const Module = await FlatLModule(cfg);
        Module.noExitRuntime = true;
        return attachWasmHooks(Module);
      } catch (e) {
        showBanner('WASM init failed: ' + e.message);
        wasmLoadPromise = null;
        return null;
      }
    })();
    return wasmLoadPromise;
  }

  async function compileAndRun(Module, source) {
    const errBuf = Module._malloc(4096);
    Module.setValue(errBuf, 0, 'i8');

    attachWasmHooks(Module);
    setStatus('running');
    let runRc = Module.ccall(
      'flat_wasm_run_source',
      'number',
      ['string', 'number', 'number'],
      [source, errBuf, 4096],
      { async: true }
    );
    if (runRc != null && typeof runRc.then === 'function') {
      runRc = await runRc;
    }

    if (runRc !== 0) {
      const msg = Module.UTF8ToString(errBuf);
      if (msg.startsWith('flc:') || msg.includes('compile') || msg.includes('parse')) {
        term.write('\r\n[compile error] ' + msg + '\r\n');
      } else if (msg) {
        term.write('\r\n[run] ' + msg + '\r\n');
      } else {
        term.write('\r\n[error] program failed (exit ' + runRc + ')\r\n');
      }
    }
    Module._free(errBuf);
  }

  async function onRun(cm) {
    if (running) return;
    if (ioDialog && ioDialog.open) {
      return;
    }
    if (!wasmModule) {
      wasmModule = await loadWasm();
      if (!wasmModule) return;
    }
    running = true;
    runBtn.disabled = true;
    setStatus('compiling');
    term.clear();
    try {
      await compileAndRun(wasmModule, cm.getValue());
    } catch (e) {
      term.write('\r\n[error] ' + e.message + '\r\n');
      setStatus('failed', true);
    } finally {
      running = false;
      runBtn.disabled = false;
      ioPendingWake = null;
      if (!ioDialog || !ioDialog.open) {
        setStatus('idle');
      }
    }
  }

  function boot() {
    initIoModal();
    const cm = initEditor();
    initTerminal();
    clearBtn.addEventListener('click', () => term.clear());
    runBtn.addEventListener('click', () => onRun(cm));
    loadWasm().then((m) => {
      wasmModule = m;
      if (m) showBanner('');
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();
