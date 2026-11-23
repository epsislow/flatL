(function (mod) {
  if (typeof CodeMirror === 'undefined') return;
  mod(CodeMirror);
})(function (CodeMirror) {
  CodeMirror.defineMode('flatl', function () {
    return {
      startState: function () {
        return { inBlockComment: false };
      },
      token: function (stream, state) {
        if (state.inBlockComment) {
          if (stream.sol()) {
            stream.eatSpace();
            if (stream.match(/^-->/)) {
              state.inBlockComment = false;
              stream.skipToEnd();
              return 'fl-comment';
            }
          }
          stream.skipToEnd();
          return 'fl-comment';
        }

        if (stream.sol()) {
          stream.eatSpace();
          if (stream.match(/^[\?~](?=\s|$)/)) return 'fl-modifier';
          if (stream.match(/^--</)) {
            state.inBlockComment = true;
            stream.skipToEnd();
            return 'fl-comment';
          }
          if (stream.match(/^--/)) {
            stream.skipToEnd();
            return 'fl-comment';
          }
        }

        if (stream.eatSpace()) return null;

        if (stream.match(/^#\S*/)) return 'fl-block';
        if (stream.match(/^'(?:\\.|[^'\\])*'/)) return 'fl-string';
        if (stream.match(/^"(?:\\.|[^"\\])*"/)) return 'fl-string';
        if (
          stream.match(
            /^(?:OK|FAIL|call|foreach|for|next|return|free|null|true|false|use|native|with|def|const|freeze|jump)\b/
          )
        ) {
          return 'fl-keyword';
        }
        if (stream.match(/^\d+(?:\.\d+)?/)) return 'fl-number';
        /* module.fn / alias.fn / math.trig.sin — only when followed by '(' */
        if (stream.match(/^[A-Za-z_][\w]*(?:\.[A-Za-z_][\w]*)+(?=\()/)) {
          return 'fl-native';
        }
        if (stream.match(/^->/)) return 'fl-assign';
        if (stream.match(/^(?:==|!=|<=|>=)/)) return 'fl-compare';
        if (stream.match(/^[<>]/)) return 'fl-compare';
        if (stream.match(/^=/)) return 'fl-assign';

        stream.next();
        return null;
      },
    };
  });
  CodeMirror.defineMIME('text/x-flatl', 'flatl');
});
