#!/usr/bin/env python3
"""Generate test/cases/date/001..075 from catalog (must-pass)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CASES = ROOT / "test" / "cases" / "date"

T_SEC = 1790769600
T_MS = 1790769600000

PIPE = """suite = "date"
id = {id}
desc = "{desc}"
kind = "pipeline"
source = "program.fl"
compile = "{root}/bin/flc -o {tmp}/out.fb {src}"
run = "{run}"
expect_exit = {exit}
{extra}
"""

def w(id_, name, desc, program, exit_=0, stdout="", stderr="", run=None):
    if run is None:
        run = "printf '' | {root}/bin/flvm {tmp}/out.fb"
    d = CASES / f"{id_:03d}_{name}"
    d.mkdir(parents=True, exist_ok=True)
    (d / "program.fl").write_text(program.strip() + "\n", encoding="utf-8")
    extra = ""
    if stdout:
        extra += f'stdout_contains = "{stdout}"\n'
    if stderr:
        extra += f'stderr_contains = "{stderr}"\n'
    run_esc = run.replace("\\", "\\\\").replace('"', '\\"')
    toml = (
        f'suite = "date"\n'
        f"id = {id_}\n"
        f'desc = "{desc.replace(chr(34), chr(39))}"\n'
        f"kind = \"pipeline\"\n"
        f'source = "program.fl"\n'
        f'compile = "{{root}}/bin/flc -o {{tmp}}/out.fb {{src}}"\n'
        f'run = "{run_esc}"\n'
        f"expect_exit = {exit_}\n"
        f"{extra}"
    )
    (d / "case.toml").write_text(toml, encoding="utf-8")


def main():
    CASES.mkdir(parents=True, exist_ok=True)
    # A format 001-020
    w(1, "format_ms_default", "ISO day from ms timestamp",
      "#main\n  date.format(1790769600000) -> d\n  io.outln(d)\n  OK", stdout="2026-09-30")
    w(2, "format_ddmmyyyy", "DD.MM.YYYY from ISO day",
      "#main\n  date.format('2026-09-30', 'DD.MM.YYYY') -> d\n  io.outln(d)\n  OK", stdout="30.09.2026")
    w(3, "format_default_arg", "default format YYYY-MM-DD",
      "#main\n  date.format(1790769600000) -> d\n  io.outln(d)\n  OK", stdout="2026-09-30")
    w(4, "format_datetime_naive", "DD.MM.YYYY HH:mm naive",
      "#main\n  date.format('2026-09-30T15:30', 'DD.MM.YYYY HH:mm') -> d\n  io.outln(d)\n  OK",
      stdout="30.09.2026 15:30")
    w(5, "format_hh_mm_a", "12h PM local from Z",
      "#main\n  date.format('2026-09-30T15:30:00.000Z', 'hh:mm A') -> d\n  io.outln(d)\n  OK",
      stdout="03:30 PM")
    w(6, "format_hhU_utc", "UTC 12h tokens",
      "#main\n  date.format('2026-09-30T15:30:00.000Z', 'hhU:mmU AU') -> d\n  io.outln(d)\n  OK",
      stdout="03:30 PM")
    w(7, "format_midnight_utc", "UTC midnight edge hhU",
      "#main\n  date.format('2026-09-30T00:15:00.000Z', 'hhU:mmU AU') -> d\n  io.outln(d)\n  OK",
      stdout="12:15 AM")
    w(8, "format_iso_token", "ISO token",
      "#main\n  date.format(1790769600000, 'ISO') -> d\n  io.outln(d)\n  OK",
      stdout="2026-09-30T12:00:00.000Z")
    w(9, "format_z_token", "Z offset token at UTC",
      "#main\n  date.format('2026-09-30T12:00:00.000Z', 'Z') -> d\n  io.outln(d)\n  OK", stdout="0")
    w(10, "format_invalid_panic", "invalid source panics",
      "#main\n  date.format('not-a-date') -> d\n  OK", exit_=1, stderr="date.format")
    w(11, "format_q_silent", "? date.format continues",
      "#main\n  x = 'keep'\n  ? date.format('bad') -> d\n  io.outln(x)\n  OK", stdout="keep")
    w(12, "format_tilde_or", "~ date.format #or",
      "#main\n  ~ date.format('bad') -> d   #.bad\n  io.outln('ok')\n  OK\n\n#or.#main.bad\n  io.outln('or')\n  OK",
      stdout="or")
    w(13, "format_empty_panic", "whitespace source panic",
      "#main\n  date.format('   ') -> d\n  OK", exit_=1, stderr="date.format")
    w(14, "format_sec_local", "NUMBER sec local format",
      "#main\n  date.format(1790769600, 'YYYY-MM-DD') -> d\n  io.outln(d)\n  OK", stdout="2026-09-30")
    w(15, "format_sec_utc_tokens", "NUMBER sec UTC tokens",
      "#main\n  date.format(1790769600, 'DDU.MM.YYYY') -> d\n  io.outln(d)\n  OK", stdout="30.09.2026")
    w(16, "format_with_offset_src", "datetime +03:00 in source",
      "#main\n  date.format('2026-09-30T15:30:00+03:00', 'HH:mm') -> d\n  io.outln(d)\n  OK",
      stdout="12:30")
    w(17, "format_bad_input_panic", "non-date string panic",
      "#main\n  date.format('abc', 'YYYY-MM-DD') -> d\n  OK", exit_=1, stderr="date.format")
    w(18, "format_e2e_outln", "pipeline io.outln",
      "#main\n  date.format(1790769600000) -> d\n  io.outln(d)\n  OK", stdout="2026-09-30")
    w(19, "format_full_tokens", "YYYY-MM-DD HH:mm:ss",
      "#main\n  date.format(1790769600, 'YYYY-MM-DD HH:mm:ss') -> d\n  io.outln(d)\n  OK",
      stdout="2026-09-30 12:00:00")
    w(20, "format_zero_sec", "epoch format",
      "#main\n  date.format(0, 'YYYY-MM-DD') -> d\n  io.outln(d)\n  OK", stdout="1970-01-01")

    NOW_RUN = f"FLAT_DATE_NOW_SEC={T_SEC} {{root}}/bin/flvm {{tmp}}/out.fb </dev/null"

    # B parse_auto 021-035
    w(21, "parse_auto_now", "parse_auto now fixed clock", "#main\n  date.parse_auto('now') -> s\n  io.outln(s)\n  OK",
      stdout=str(T_SEC), run=NOW_RUN)
    w(22, "parse_auto_plus2d", "+2d from fixed now",
      "#main\n  date.parse_auto('+2d') -> s\n  date.format(s, 'YYYY-MM-DD') -> d\n  io.outln(d)\n  OK",
      stdout="2026-10-02", run=NOW_RUN)
    w(23, "parse_auto_chain_nospace", "chain +2d+3h",
      "#main\n  date.parse_auto('+2d+3h') -> s\n  io.outln(s)\n  OK", run=NOW_RUN)
    w(24, "parse_auto_anchor_minus_s", "ISO day -20s",
      "#main\n  date.parse_auto('2026-09-30-20s') -> s\n  io.outln(s)\n  OK", run=NOW_RUN)
    w(25, "parse_auto_mixed", "mixed units chain",
      "#main\n  date.parse_auto('+1d+2h') -> s\n  io.outln(s)\n  OK", run=NOW_RUN)
    w(26, "parse_auto_cross_month", "cross month",
      "#main\n  date.parse_auto('2026-09-30+2d') -> s\n  date.format(s, 'MM-DD') -> d\n  io.outln(d)\n  OK",
      stdout="10-02", run=NOW_RUN)
    w(27, "parse_auto_minus_months", "-1 months",
      "#main\n  date.parse_auto('2026-03-15-1months') -> s\n  date.format(s, 'YYYY-MM') -> d\n  io.outln(d)\n  OK",
      stdout="2026-02", run=NOW_RUN)
    w(28, "parse_auto_iso_day", "ISO day only",
      "#main\n  date.parse_auto('2026-09-30') -> s\n  date.format(s, 'YYYY-MM-DD') -> d\n  io.outln(d)\n  OK",
      stdout="2026-09-30")
    w(29, "parse_auto_ms", "ms number",
      "#main\n  date.parse_auto(1790769600000) -> s\n  io.outln(s)\n  OK", stdout=str(T_SEC))
    w(30, "parse_auto_sec", "sec number",
      "#main\n  date.parse_auto(1790769600) -> s\n  io.outln(s)\n  OK", stdout=str(T_SEC))
    w(31, "parse_auto_invalid", "invalid panic",
      "#main\n  date.parse_auto('not-a-date') -> s\n  OK", exit_=1, stderr="date.parse_auto")
    w(32, "parse_auto_q", "? parse_auto",
      "#main\n  n = 7\n  ? date.parse_auto('bad') -> s\n  io.outln(n)\n  OK", stdout="7")
    w(33, "parse_auto_offset", "offset +05:30",
      "#main\n  date.parse_auto('2026-01-01T00:00:00+05:30') -> s\n  io.outln(s)\n  OK", stdout="1767205800")
    w(34, "parse_auto_z", "Z suffix",
      "#main\n  date.parse_auto('2026-09-30T12:00:00Z') -> s\n  io.outln(s)\n  OK", stdout=str(T_SEC))
    w(35, "parse_auto_trailing_garbage", "trailing garbage panic",
      "#main\n  date.parse_auto('2026-09-30 +2d extra') -> s\n  OK", exit_=1, stderr="date.parse_auto")

    # C parse strict 036-042
    w(36, "parse_strict_ddmmyyyy", "strict DD.MM.YYYY",
      "#main\n  date.parse('20.10.2026', 'DD.MM.YYYY') -> s\n  date.format(s, 'YYYY-MM-DD') -> d\n  io.outln(d)\n  OK",
      stdout="2026-10-20")
    w(37, "parse_strict_mismatch", "mismatch panic",
      "#main\n  date.parse('32.13.2026', 'DD.MM.YYYY') -> s\n  OK", exit_=1, stderr="date.parse")
    w(38, "parse_strict_hhmm", "HH:mm strict",
      "#main\n  date.parse('15:30', 'HH:mm') -> s\n  date.time_to_sec('15:30:00') -> e\n  io.outln(e)\n  OK",
      stdout="55800")
    w(39, "parse_strict_empty_fmt", "empty format panic",
      "#main\n  date.parse('2026-01-01', '') -> s\n  OK", exit_=1, stderr="date.parse")
    w(40, "parse_strict_q", "? strict parse",
      "#main\n  k = 1\n  ? date.parse('x', 'DD.MM.YYYY') -> s\n  io.outln(k)\n  OK", stdout="1")
    w(41, "parse_roundtrip", "parse format roundtrip",
      "#main\n  date.parse('30.09.2026', 'DD.MM.YYYY') -> s\n  date.format(s, 'DD.MM.YYYY') -> d\n  io.outln(d)\n  OK",
      stdout="30.09.2026")
    w(42, "parse_combined", "DD.MM.YYYY HH:mm",
      "#main\n  date.parse('30.09.2026 15:30', 'DD.MM.YYYY HH:mm') -> s\n  date.format(s, 'DD.MM.YYYY HH:mm') -> d\n  io.outln(d)\n  OK",
      stdout="30.09.2026 15:30")

    # D offset 043-050
    w(43, "offset_z_same_instant", "Z vs +00 same sec",
      "#main\n  date.parse_auto('2026-09-30T12:00:00Z') -> a\n  date.parse_auto('2026-09-30T12:00:00+00:00') -> b\n  date.cmp(a, b) -> c\n  io.outln(c)\n  OK",
      stdout="0")
    w(44, "run_offset_naive", "run_offset +03 naive format",
      "#main\n  date.run_offset('+03:00')\n  date.format('2026-09-30T12:00:00', 'HH') -> d\n  io.outln(d)\n  OK",
      stdout="15")
    w(45, "run_offset_negative", "run_offset -05:30",
      "#main\n  date.run_offset('-05:30')\n  date.format(0, 'HH') -> d\n  io.outln(d)\n  OK", stdout="18")
    w(46, "today_with_offset", "today with offset",
      "#main\n  date.run_offset('+03:00')\n  date.today() -> d\n  io.outln(d)\n  OK",
      stdout="2026-09-30", run=NOW_RUN)
    w(47, "run_offset_invalid", "invalid offset panic",
      "#main\n  date.run_offset('+25:00')\n  OK", exit_=1, stderr="date.run_offset")
    w(48, "run_offset_reset", "reset offset Z",
      "#main\n  date.run_offset('+03:00')\n  date.run_offset('Z')\n  date.format(0, 'HH') -> d\n  io.outln(d)\n  OK",
      stdout="00")
    w(49, "format_local_offset", "local tokens with offset",
      "#main\n  date.run_offset('+03:00')\n  date.format(1790769600, 'HH:mm') -> d\n  io.outln(d)\n  OK",
      stdout="15:00")
    w(50, "env_run_offset", "FLAT_DATE_RUN_OFFSET env",
      "#main\n  date.format(0, 'HH') -> d\n  io.outln(d)\n  OK", stdout="03",
      run=f"FLAT_DATE_RUN_OFFSET=+03:00 FLAT_DATE_NOW_SEC={T_SEC} {{root}}/bin/flvm {{tmp}}/out.fb </dev/null")

    # E seconds 051-058
    w(51, "to_from_seconds_roundtrip", "to_seconds from_seconds",
      "#main\n  date.to_seconds('2026-09-30T12:00:00Z') -> s\n  date.from_seconds(s) -> t\n  io.outln(t)\n  OK",
      stdout="2026-09-30T12:00:00Z")
    w(52, "from_seconds_zero", "epoch ISO Z",
      "#main\n  date.from_seconds(0) -> t\n  io.outln(t)\n  OK", stdout="1970-01-01T00:00:00Z")
    w(53, "time_to_sec", "time_to_sec",
      "#main\n  date.time_to_sec('15:30:00') -> s\n  io.outln(s)\n  OK", stdout="55800")
    w(54, "sec_to_time", "sec_to_time",
      "#main\n  date.sec_to_time(55800) -> t\n  io.outln(t)\n  OK", stdout="15:30:00")
    w(55, "sec_to_time_invalid", "sec_to_time invalid",
      "#main\n  date.sec_to_time(90000) -> t\n  OK", exit_=1, stderr="date.sec_to_time")
    w(56, "to_from_ms", "to_ms from_ms",
      "#main\n  date.to_ms(1790769600) -> m\n  date.from_ms(m) -> t\n  io.outln(t)\n  OK",
      stdout="2026-09-30T12:00:00Z")
    w(57, "cmp_seconds", "date.cmp",
      "#main\n  date.cmp(1790769600, 1790769601) -> c\n  io.outln(c)\n  OK", stdout="-1")
    w(58, "add_sec_format", "add on sec then format",
      "#main\n  date.add(1790769600, '+1d') -> s\n  date.format(s, 'YYYY-MM-DD') -> d\n  io.outln(d)\n  OK",
      stdout="2026-10-01")

    # F str.to_* 059-067
    w(59, "str_to_date_iso", "str.to_date ISO",
      "#main\n  str.to_date('2026-09-30') -> d\n  io.outln(d)\n  OK", stdout="2026-09-30")
    w(60, "str_to_date_ro", "str.to_date DD.MM.YYYY",
      "#main\n  str.to_date('30.09.2026') -> d\n  io.outln(d)\n  OK", stdout="2026-09-30")
    w(61, "str_to_time", "str.to_time",
      "#main\n  str.to_time('15:30') -> t\n  io.outln(t)\n  OK", stdout="15:30:00")
    w(62, "str_to_datetime", "str.to_datetime naive",
      "#main\n  str.to_datetime('2026-09-30T15:30') -> t\n  io.outln(t)\n  OK",
      stdout="2026-09-30T15:30:00")
    w(63, "str_to_date_invalid", "str.to_date panic",
      "#main\n  str.to_date('nope') -> d\n  OK", exit_=1, stderr="str.to_date")
    w(64, "str_to_time_invalid", "str.to_time panic",
      "#main\n  str.to_time('nope') -> t\n  OK", exit_=1, stderr="str.to_time")
    w(65, "str_to_datetime_invalid", "str.to_datetime panic",
      "#main\n  str.to_datetime('nope') -> t\n  OK", exit_=1, stderr="str.to_datetime")
    w(66, "str_to_date_q", "? str.to_date",
      "#main\n  v = 'x'\n  ? str.to_date('bad') -> d\n  io.outln(v)\n  OK", stdout="x")
    w(67, "json_datetime_string", "json keeps datetime string",
      "use native obj with O\n\n#main\n  json.parse_object('{\"at\":\"2026-09-30T12:00:00Z\"}') -> o\n  O.get(o, 'at') -> at\n  io.outln(at)\n  OK",
      stdout="2026-09-30T12:00:00Z")

    # G integration 068-075
    w(68, "mmed_style_label", "iso to DD.MM.YYYY label",
      "#main\n  iso = '2026-09-30'\n  date.format(iso, 'DD.MM.YYYY') -> label\n  io.outln(label)\n  OK",
      stdout="30.09.2026")
    w(69, "cmp_in_guard", "cmp in guard",
      "#main\n  date.cmp(1, 2) -> c\n  c == -1\n  io.outln('yes')\n  OK", stdout="yes")
    w(70, "foreach_format", "foreach format batch",
      "#main\n  xs = ['2026-09-30', '2026-10-01']\n  foreach(xs, day, '#each')\n  io.outln('done')\n  OK\n\n#each\n  date.format(day, 'DD') -> dd\n  io.outln(dd)\n  OK",
      stdout="30")
    w(71, "tilde_str_to_date", "~ str.to_date #or",
      "#main\n  ~ str.to_date('bad') -> d   #.bad\n  io.outln('ok')\n  OK\n\n#or.#main.bad\n  io.outln('or')\n  OK",
      stdout="or")
    w(72, "tilde_parse_auto", "~ parse_auto #.",
      "#main\n  ~ date.parse_auto('!!!') -> s   #.bad\n  io.outln('ok')\n  OK\n\n#or.#main.bad\n  io.outln('or')\n  OK",
      stdout="or")
    w(73, "str_to_date_panic", "str.to_date invalid panics (unhandled)",
      "#main\n  str.to_date('bad') -> d\n  io.outln('bad')\n  OK",
      exit_=1, stderr="unhandled PANIC: str.to_date")
    w(74, "add_then_format", "add then format chain",
      "#main\n  date.add('2026-09-30', '+1d') -> x\n  date.format(x, 'YYYY-MM-DD') -> d\n  io.outln(d)\n  OK",
      stdout="2026-10-01")
    w(75, "run_offset_today_cmp", "run_offset today compare",
      "#main\n  date.run_offset('+03:00')\n  date.today() -> t\n  io.outln(t)\n  OK",
      stdout="2026-09-30", run=NOW_RUN)

    print(f"Wrote 75 cases under {CASES}")


if __name__ == "__main__":
    main()
