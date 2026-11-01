(function (mod) {
  if (typeof CodeMirror === 'undefined') return;
  mod(CodeMirror);
})(function (CodeMirror) {
  CodeMirror.defineMode('flatl', function () {
    return {
      token: function (stream) {
        if (stream.eatSpace()) return null;
        if (stream.match(/^#.*/)) return 'fl-block';
        if (stream.match(/^'(?:\\.|[^'\\])*'/)) return 'fl-string';
        if (stream.match(/^"(?:\\.|[^"\\])*"/)) return 'fl-string';
        if (stream.match(/^(?:OK|FAIL|call|foreach|free|null|true|false)\b/)) return 'fl-keyword';
        if (stream.match(/^\d+(?:\.\d+)?/)) return 'fl-number';
        if (stream.match(/^(?:io|str|json|mem|list|obj|math(?:\.(?:trig|stats))?)\.\w+/)) {
          return 'fl-native';
        }
        stream.next();
        return null;
      },
    };
  });
  CodeMirror.defineMIME('text/x-flatl', 'flatl');
});
