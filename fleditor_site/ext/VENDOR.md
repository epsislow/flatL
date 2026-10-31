# Vendored libraries (fleditor)

All files in this directory are committed in-repo. The browser loads them via relative URLs only (no CDN).

| File | Upstream | Version | License |
|------|----------|---------|---------|
| `codemirror.min.js`, `codemirror.min.css` | [CodeMirror 5](https://codemirror.net/5/) | 5.65.16 | MIT |
| `xterm.js`, `xterm.css` | [xterm.js](https://github.com/xtermjs/xterm.js) | 5.3.0 | MIT |

Refresh vendored copies:

```bash
./scripts/vendor-fleditor-ext.sh
```
