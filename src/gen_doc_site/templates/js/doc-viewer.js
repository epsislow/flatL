/**
 * flatL offline docs viewer — requires FlatDocData (js/doc-data.js).
 */
(function (root) {
  'use strict';

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function getData() {
    return root.FlatDocData || { title: 'flatL documentation', sections: [] };
  }

  function normalizeDocPath(href) {
    var parts = [];
    var segs = String(href || '')
      .split('/')
      .filter(function (s) {
        return s.length > 0 && s !== '.';
      });
    var i;
    for (i = 0; i < segs.length; i++) {
      if (segs[i] === '..') {
        parts.pop();
      } else {
        parts.push(segs[i]);
      }
    }
    return parts.join('/');
  }

  function sectionIdForHref(href) {
    var target = normalizeDocPath(href.replace(/#.*$/, ''));
    var data = getData();
    var sections = data.sections || [];
    var i;
    if (!target) {
      return null;
    }
    for (i = 0; i < sections.length; i++) {
      if (sections[i].file === target) {
        return sections[i].id;
      }
    }
    for (i = 0; i < sections.length; i++) {
      var f = sections[i].file || '';
      if (f.endsWith('/' + target) || f === target) {
        return sections[i].id;
      }
    }
    return null;
  }

  function inlineFormat(text) {
    var s = escapeHtml(String(text));
    s = s.replace(/`([^`]+)`/g, '<code>$1</code>');
    s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, function (_, label, href) {
      var h = String(href);
      if (/^https?:\/\//i.test(h)) {
        return (
          '<a href="' +
          escapeHtml(h) +
          '" target="_blank" rel="noopener">' +
          escapeHtml(label) +
          '</a>'
        );
      }
      if (/\.md(\#|$)/i.test(h) || h.indexOf('.md#') >= 0) {
        var sid = sectionIdForHref(h);
        if (sid) {
          return (
            '<a href="#' +
            escapeHtml(sid) +
            '" data-doc-section="' +
            escapeHtml(sid) +
            '">' +
            escapeHtml(label) +
            '</a>'
          );
        }
      }
      if (h.charAt(0) === '#') {
        return (
          '<a href="' +
          escapeHtml(h) +
          '" data-doc-hash="' +
          escapeHtml(h.slice(1)) +
          '">' +
          escapeHtml(label) +
          '</a>'
        );
      }
      return (
        '<a href="' +
        escapeHtml(h) +
        '" target="_blank" rel="noopener">' +
        escapeHtml(label) +
        '</a>'
      );
    });
    return s;
  }

  function mdToHtml(md) {
    var lines = String(md || '').replace(/\r\n/g, '\n').split('\n');
    var html = [];
    var i = 0;
    var inCode = false;
    var codeBuf = [];
    var listType = null;

    function closeList() {
      if (listType) {
        html.push(listType === 'ul' ? '</ul>' : '</ol>');
        listType = null;
      }
    }

    while (i < lines.length) {
      var line = lines[i];

      if (/^\s*<!--[\s\S]*?-->\s*$/.test(line)) {
        i++;
        continue;
      }

      if (inCode) {
        if (/^```/.test(line)) {
          html.push('<pre><code>' + escapeHtml(codeBuf.join('\n')) + '</code></pre>');
          codeBuf = [];
          inCode = false;
        } else {
          codeBuf.push(line);
        }
        i++;
        continue;
      }

      if (/^```/.test(line)) {
        closeList();
        inCode = true;
        codeBuf = [];
        i++;
        continue;
      }

      var hm = line.match(/^(#{1,3})\s+(.+)$/);
      if (hm) {
        closeList();
        var level = hm[1].length;
        var title = hm[2].trim();
        var hid = title
          .toLowerCase()
          .replace(/[^\w\u00C0-\u024f]+/g, '-')
          .replace(/^-|-$/g, '');
        html.push(
          '<h' + level + ' id="doc-' + hid + '">' + inlineFormat(title) + '</h' + level + '>'
        );
        i++;
        continue;
      }

      var ul = line.match(/^\s*[-*]\s+(.+)$/);
      if (ul) {
        if (listType !== 'ul') {
          closeList();
          html.push('<ul>');
          listType = 'ul';
        }
        html.push('<li>' + inlineFormat(ul[1]) + '</li>');
        i++;
        continue;
      }

      if (/^\s*\|/.test(line) && i + 1 < lines.length && /^\s*\|?\s*:?-+:?\s*\|/.test(lines[i + 1])) {
        closeList();
        function parseRow(row) {
          return row
            .replace(/^\s*\|/, '')
            .replace(/\|\s*$/, '')
            .split('|')
            .map(function (c) {
              return c.trim();
            });
        }
        var headers = parseRow(line);
        i += 2;
        var rows = [];
        while (i < lines.length && /^\s*\|/.test(lines[i])) {
          rows.push(parseRow(lines[i]));
          i++;
        }
        html.push(
          '<table><thead><tr>' +
            headers
              .map(function (h) {
                return '<th>' + inlineFormat(h) + '</th>';
              })
              .join('') +
            '</tr></thead><tbody>'
        );
        rows.forEach(function (r) {
          html.push(
            '<tr>' +
              r
                .map(function (c) {
                  return '<td>' + inlineFormat(c) + '</td>';
                })
                .join('') +
              '</tr>'
          );
        });
        html.push('</tbody></table>');
        continue;
      }

      var ol = line.match(/^\s*\d+\.\s+(.+)$/);
      if (ol) {
        if (listType !== 'ol') {
          closeList();
          html.push('<ol>');
          listType = 'ol';
        }
        html.push('<li>' + inlineFormat(ol[1]) + '</li>');
        i++;
        continue;
      }

      if (/^\s*$/.test(line)) {
        closeList();
        i++;
        continue;
      }

      closeList();
      html.push('<p>' + inlineFormat(line) + '</p>');
      i++;
    }
    closeList();
    if (inCode) {
      html.push('<pre><code>' + escapeHtml(codeBuf.join('\n')) + '</code></pre>');
    }
    return html.join('\n');
  }

  function bindContentLinks(content) {
    content.querySelectorAll('a[data-doc-section]').forEach(function (a) {
      a.addEventListener('click', function (ev) {
        ev.preventDefault();
        showSection(a.getAttribute('data-doc-section'));
        location.hash = a.getAttribute('data-doc-section');
      });
    });
    content.querySelectorAll('a[data-doc-hash]').forEach(function (a) {
      a.addEventListener('click', function (ev) {
        ev.preventDefault();
        var targetId = a.getAttribute('data-doc-hash');
        var el =
          document.getElementById('doc-' + targetId) || document.getElementById(targetId);
        if (el) {
          el.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
      });
    });
  }

  function showSection(id) {
    var data = getData();
    var sections = data.sections || [];
    var sec =
      sections.find(function (s) {
        return s.id === id;
      }) || sections[0];
    var toc = document.getElementById('docs-toc');
    var content = document.getElementById('docs-content');
    var title = document.getElementById('site-title');

    if (title) {
      title.textContent = data.title || 'flatL documentation';
    }
    if (toc) {
      toc.querySelectorAll('button').forEach(function (b) {
        b.classList.toggle('active', sec && b.getAttribute('data-id') === sec.id);
      });
    }
    if (!content) {
      return;
    }
    if (!sec) {
      content.innerHTML =
        '<p class="docs-stub">No sections. Run <code>make docs</code> from the repository root.</p>';
      return;
    }
    content.innerHTML = mdToHtml(sec.markdown);
    content.scrollTop = 0;
    bindContentLinks(content);
  }

  function renderToc() {
    var data = getData();
    var toc = document.getElementById('docs-toc');
    if (!toc) {
      return;
    }
    toc.innerHTML = '';
    (data.sections || []).forEach(function (sec) {
      var b = document.createElement('button');
      b.type = 'button';
      b.textContent = sec.label || sec.id;
      b.setAttribute('data-id', sec.id);
      b.onclick = function () {
        showSection(sec.id);
        location.hash = sec.id;
      };
      toc.appendChild(b);
    });
  }

  function initFromHash() {
    var h = (location.hash || '').replace(/^#/, '');
    var data = getData();
    var sections = data.sections || [];
    if (h && sections.some(function (s) { return s.id === h; })) {
      showSection(h);
    } else if (sections.length > 0) {
      showSection(sections[0].id);
    }
  }

  function init() {
    renderToc();
    initFromHash();
    window.addEventListener('hashchange', initFromHash);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }

  root.FlatDocViewer = { mdToHtml: mdToHtml, showSection: showSection };
})(typeof globalThis !== 'undefined' ? globalThis : this);
