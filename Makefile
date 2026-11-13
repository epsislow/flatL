ifeq ($(origin CC),default)
CC      = gcc
endif
CFLAGS  ?= -Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE
LDFLAGS ?=

# Optional: FLC_LDFLAGS=-Wl,--stack,8388608 if an old flc ever overflows stack (heap IR is default).
FLC_LDFLAGS ?=

BINDIR  = bin
ifeq ($(OS),Windows_NT)
EXEEXT = .exe
PORT_SRC = src/port/flat_port_win.c
else
EXEEXT =
PORT_SRC = src/port/flat_port_posix.c
endif
STR_SRC = src/common/flat_str.c
PORT_LIB = $(STR_SRC) $(PORT_SRC)
FB_SRC  = src/common/fb_format.c
FB_TOOL_SRC = $(FB_SRC) src/common/flat_assign.c src/common/flat_schema.c \
              src/common/flat_slot_type.c src/common/flat_number.c src/common/value_pool.c \
              src/common/value_collection.c src/common/value_live.c
DISASM_SRC = src/common/flat_disasm.c
FBDUMP_SRCS = src/fbdump/main.c $(DISASM_SRC) $(FB_TOOL_SRC) $(NATIVE_FLC_SRC)
NATIVE_VM_PLUGIN = src/lib/native_plugin_host.c
ifeq ($(OS),Windows_NT)
NATIVE_VM_MATH = src/lib/mod_math_trig.c src/lib/mod_math_stats.c src/lib/mod_math.c \
                 src/lib/flat_math_calc.c src/lib/mod_list.c src/lib/mod_obj.c
VM_EXTRA_LIBS =
PLUGIN_TARGET =
else
NATIVE_VM_MATH =
VM_EXTRA_LIBS = -ldl
PLUGIN_TARGET = $(BINDIR)/libflat_math_trig.so $(BINDIR)/libflat_math_stats.so \
                $(BINDIR)/libflat_math.so $(BINDIR)/libflat_list.so $(BINDIR)/libflat_obj.so
PLUGIN_COMMON_SRCS = src/lib/native_util.c src/common/value_parse.c src/common/value_ops.c \
               src/common/value_json.c src/common/flat_number.c src/common/value_pool.c \
               src/common/value_collection.c src/common/value_live.c src/common/flat_assign.c \
               src/common/flat_obj_path.c src/common/flat_schema.c src/common/flat_slot_type.c \
               src/common/flat_str.c
PLUGIN_TRIG_SRCS = src/lib/plugin_math_trig_entry.c src/lib/mod_math_trig.c $(PLUGIN_COMMON_SRCS)
PLUGIN_STATS_SRCS = src/lib/plugin_math_stats_entry.c src/lib/mod_math_stats.c $(PLUGIN_COMMON_SRCS)
PLUGIN_MATH_SRCS = src/lib/plugin_math_entry.c src/lib/mod_math.c src/lib/flat_math_calc.c \
               $(PLUGIN_COMMON_SRCS)
PLUGIN_LIST_SRCS = src/lib/plugin_list_entry.c src/lib/mod_list.c $(PLUGIN_COMMON_SRCS)
PLUGIN_OBJ_SRCS = src/lib/plugin_obj_entry.c src/lib/mod_obj.c $(PLUGIN_COMMON_SRCS)
endif
NATIVE_VM_SRC = src/lib/native_registry.c src/lib/native_util.c src/lib/native_dispatch.c \
                src/lib/flat_date.c src/lib/mod_io.c src/lib/mod_str.c src/lib/mod_json.c \
                src/lib/mod_date.c src/lib/mod_mem.c src/lib/mod_file.c src/lib/mod_vcx.c \
                src/lib/mod_vcx_dschema.c $(NATIVE_VM_MATH) $(NATIVE_VM_PLUGIN)
NATIVE_FLC_SRC = src/lib/native_registry.c src/lib/native_util.c
VM_SRC  = src/common/flat_time.c src/common/vm.c src/common/doc_lookup.c src/common/flat_number.c \
          src/common/value_pool.c \
          src/common/value_collection.c src/common/value_live.c src/common/flat_assign.c \
          src/common/flat_slot_type.c src/common/flat_schema.c src/common/flat_dynamic_bind.c \
          src/common/value_ops.c src/common/flat_obj_path.c \
          src/common/flat_vcx.c \
          src/common/value_parse.c src/common/value_format.c src/common/value_io.c \
          src/common/value_json.c src/common/flat_file.c src/common/flat_io_ctx.c \
          src/common/flat_memstream.c \
          $(STR_SRC) $(NATIVE_VM_SRC)
FLAT_TIME_SRC = src/common/flat_time.c
FGIR_SRC = src/common/parse.c src/common/link.c src/common/compile.c src/common/flat_func.c \
               src/common/flat_assign.c src/common/flat_slot_type.c src/common/flat_schema.c \
               src/common/flat_number.c src/common/value_pool.c src/common/value_collection.c \
               src/common/value_live.c \
               src/common/line_label.c src/common/flat_jump_block.c \
           src/common/native_use.c \
           $(FLAT_TIME_SRC) \
           $(STR_SRC) $(NATIVE_FLC_SRC)
FLATL_SRC = src/flatl/main.c src/common/parse.c src/common/link.c src/common/compile.c \
           src/common/flat_func.c src/common/line_label.c src/common/flat_jump_block.c \
            src/common/native_use.c $(FB_SRC) $(VM_SRC) $(PORT_SRC)

MIN_OK_FB = test/cases/flvm/001_min_ok/min_ok.fb examples/min_ok.fb

# Default: compile only (does not run tests)
.DEFAULT_GOAL := all

.PHONY: all help clean test test-all test-bench test-flatl fixtures tools flvm flc flatl mkfb fbdump run_tests gen_doc_site docs io_generate_tests json_generate_tests date_generate_tests rebuild rebuild-native fleditor-wasm fleditor-node-check fleditor-serve fleditor-stop fleditor-serve-forced fleditor-smoke fleditor-layout-smoke fleditor-theme-smoke fleditor-project-smoke fleditor-hex-smoke fleditor-examples-smoke fleditor-wasm-smoke fleditor-wasm-multifile-smoke fleditor-rebuild

help:
	@echo "flatL Makefile — run from repo root"
	@echo ""
	@echo "Build (compile only, no tests executed):"
	@echo "  make              same as make all"
	@echo "  make all          bin/flc, bin/flvm, bin/flatl, bin/run_tests, bin/port_check, … + fixtures"
	@echo "  make tools        bin/flc, bin/flvm, bin/flatl, bin/mkfb, bin/fbdump"
	@echo "  make fbdump       bytecode disassembler only"
	@echo "  make flvm         rebuild VM only"
	@echo "  make flc          rebuild compiler only"
	@echo "  make flatl        rebuild driver (compile/run/crun) only"
	@echo "  make mkfb         rebuild fixture helper only"
	@echo "  make run_tests    rebuild test runner only"
	@echo "  make fixtures     regenerate min_ok.fb examples"
	@echo "  make rebuild      clean + all; fleditor-wasm if emcc on PATH"
	@echo "  make rebuild-native  clean + all only (never runs emcc)"
	@echo ""
	@echo "Test generators (rewrite committed cases under test/cases/):"
	@echo "  make io_generate_tests    bin/io_generate_tests → test/cases/io/*"
	@echo "  make json_generate_tests  bin/json_generate_tests → test/cases/json/*"
	@echo "  make date_generate_tests  scripts/gen_date_tests.py → test/cases/date/*"
	@echo ""
	@echo "Docs site (doc_src/index.json → docs/doc_site/, incl. fleditor Docs link):"
	@echo "  make docs | make gen_doc_site   run bin/gen_doc_site"
	@echo ""
	@echo "Fleditor (browser playground under docs/fleditor_site/):"
	@echo "  make fleditor-wasm                 Emscripten → docs/fleditor_site/wasm/ (needs emcc)"
	@echo "  make fleditor-rebuild              same as make rebuild (+ refresh browser tip)"
	@echo "  make fleditor-node-check           diagnose Node (WSL1 / Windows node.exe fallback)"
	@echo "  make fleditor-serve                static server 127.0.0.1:8765 (env FLEDITOR_PORT)"
	@echo "  make fleditor-stop                 stop serve.js on that port"
	@echo "  make fleditor-serve-forced         fleditor-stop, then serve"
	@echo "  make fleditor-smoke                HTTP smoke (serve)"
	@echo "  make fleditor-layout-smoke         layout smoke (Node)"
	@echo "  make fleditor-theme-smoke          theme smoke (Node)"
	@echo "  make fleditor-project-smoke        project UI smoke (Node)"
	@echo "  make fleditor-hex-smoke            hex editor smokes (Node)"
	@echo "  make fleditor-examples-smoke       examples picker smoke (Node)"
	@echo "  make fleditor-wasm-smoke           WASM smoke (needs fleditor-wasm)"
	@echo "  make fleditor-wasm-multifile-smoke WASM multifile smoke (needs fleditor-wasm)"
	@echo "  make fleditor-wasm-order-flow-smoke   WASM user def (order_flow; needs fleditor-wasm)"
	@echo ""
	@echo "Note: if sources unchanged, make skips compile (up to date)."
	@echo "      Targets still print 'OK: …' so you know nothing failed."
	@echo ""
	@echo "Test:"
	@echo "  make test         all + run_tests --categ=default (excludes bench/slow)"
	@echo "  make test-all     all + run_tests --categ=all"
	@echo "  make test-bench   all + run_tests --categ=bench"
	@echo "  make test-flatl     all + run_tests --flatl --categ=default"
	@echo "  bin/run_tests -h  filters, -v, -e, -t, --list, -w, --flatl (see doc_src/TOOLCHAIN.md)"
	@echo ""
	@echo "Clean:"
	@echo "  make clean        remove bin/ and generated min_ok.fb"
	@echo ""
	@echo "Docs: doc_src/TOOLCHAIN.md  doc_src/FMOD_MODULES.md  |  Pages: docs/"

# Full toolchain binaries + bytecode fixtures (still does not run tests)
all: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/run_tests$(EXEEXT) $(BINDIR)/port_check$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT) $(BINDIR)/fbdump$(EXEEXT) $(BINDIR)/gen_doc_site$(EXEEXT) $(PLUGIN_TARGET) fixtures
	@echo "OK: bin/flc bin/flvm bin/flatl bin/mkfb bin/fbdump bin/gen_doc_site bin/run_tests bin/port_check (+ min_ok.fb fixtures)"

tools: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT) $(BINDIR)/fbdump$(EXEEXT) $(PLUGIN_TARGET)
	@echo "OK: bin/flc bin/flvm bin/flatl bin/mkfb bin/fbdump"

flvm: $(BINDIR)/flvm$(EXEEXT)
	@echo "OK: bin/flvm"

flc: $(BINDIR)/flc$(EXEEXT)
	@echo "OK: bin/flc"

flatl: $(BINDIR)/flatl$(EXEEXT)
	@echo "OK: bin/flatl"

mkfb: $(BINDIR)/mkfb$(EXEEXT)
	@echo "OK: bin/mkfb"

fbdump: $(BINDIR)/fbdump$(EXEEXT)
	@echo "OK: bin/fbdump"

run_tests: $(BINDIR)/run_tests$(EXEEXT)
	@echo "OK: bin/run_tests"

io_generate_tests: $(BINDIR)/io_generate_tests$(EXEEXT)
	$(BINDIR)/io_generate_tests$(EXEEXT)

json_generate_tests: $(BINDIR)/json_generate_tests$(EXEEXT)
	find test/cases/json -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
	$(BINDIR)/json_generate_tests$(EXEEXT)

date_generate_tests:
	python3 scripts/gen_date_tests.py
	@echo "OK: test/cases/date/ (75 cases)"

rebuild: clean all fleditor-wasm-optional

rebuild-native: clean all
	@echo "OK: rebuild-native (bin/* only; run make fleditor-wasm when emcc is on PATH)"

.PHONY: fleditor-wasm-optional
fleditor-wasm-optional:
	@if command -v emcc >/dev/null 2>&1; then \
		$(MAKE) -f Makefile.wasm fleditor-wasm; \
		echo "OK: rebuild (bin/* + docs/fleditor_site/wasm/)"; \
	else \
		echo "OK: rebuild (bin/*; skipped fleditor-wasm — no emcc on PATH)"; \
	fi

fixtures: $(MIN_OK_FB)
	@echo "OK: $(MIN_OK_FB)"

$(BINDIR)/flvm$(EXEEXT): src/flvm/main.c $(FB_SRC) $(VM_SRC) $(PORT_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flvm/main.c $(FB_SRC) $(VM_SRC) $(PORT_SRC) $(LDFLAGS) $(VM_EXTRA_LIBS) -lm

$(BINDIR)/libflat_math_trig.so: $(PLUGIN_TRIG_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_TRIG_SRCS) $(LDFLAGS) -lm

$(BINDIR)/libflat_math_stats.so: $(PLUGIN_STATS_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_STATS_SRCS) $(LDFLAGS) -lm

$(BINDIR)/libflat_math.so: $(PLUGIN_MATH_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_MATH_SRCS) $(LDFLAGS) -lm

$(BINDIR)/libflat_list.so: $(PLUGIN_LIST_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_LIST_SRCS) $(LDFLAGS) -lm

$(BINDIR)/libflat_obj.so: $(PLUGIN_OBJ_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_OBJ_SRCS) $(LDFLAGS) -lm

$(BINDIR)/flc$(EXEEXT): src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(DISASM_SRC) $(PORT_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(DISASM_SRC) $(PORT_SRC) $(LDFLAGS) $(FLC_LDFLAGS) -lm

$(BINDIR)/fbdump$(EXEEXT): $(FBDUMP_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(FBDUMP_SRCS) $(LDFLAGS) -lm

$(BINDIR)/flatl$(EXEEXT): $(FLATL_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(FLATL_SRC) $(LDFLAGS) $(FLC_LDFLAGS) $(VM_EXTRA_LIBS) -lm

$(BINDIR)/mkfb$(EXEEXT): src/mkfb/main.c $(FB_TOOL_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/mkfb/main.c $(FB_TOOL_SRC) $(LDFLAGS) -lm

$(BINDIR)/gen_doc_site$(EXEEXT): $(GEN_DOC_SITE_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(GEN_DOC_SITE_SRCS) $(LDFLAGS)

docs gen_doc_site: $(BINDIR)/gen_doc_site$(EXEEXT)
	$(BINDIR)/gen_doc_site$(EXEEXT)
	@echo "OK: docs/doc_site/"

RUN_TESTS_SRCS = \
	src/run_tests/main.c \
	src/run_tests/discover.c \
	src/run_tests/case.c \
	src/common/repo.c \
	$(FLAT_TIME_SRC) \
	$(PORT_LIB)

$(BINDIR)/run_tests$(EXEEXT): $(RUN_TESTS_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(RUN_TESTS_SRCS) $(LDFLAGS) -lm

$(BINDIR)/port_check$(EXEEXT): src/port_check/main.c $(STR_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/port_check/main.c $(STR_SRC) $(LDFLAGS)

GEN_REPO_SRCS = src/common/repo.c $(PORT_LIB)

GEN_DOC_SITE_SRCS = src/gen_doc_site/main.c src/gen_doc_site/index_json.c src/gen_doc_site/emit.c \
                      src/gen_doc_site/fs_util.c $(GEN_REPO_SRCS)

$(BINDIR)/io_generate_tests$(EXEEXT): src/io_generate_tests/main.c $(GEN_REPO_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/io_generate_tests/main.c $(GEN_REPO_SRCS) $(LDFLAGS)

$(BINDIR)/json_generate_tests$(EXEEXT): src/json_generate_tests/main.c $(GEN_REPO_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/json_generate_tests/main.c $(GEN_REPO_SRCS) $(LDFLAGS)

$(BINDIR):
	mkdir -p $(BINDIR)

test/cases/flvm/001_min_ok/min_ok.fb: $(BINDIR)/mkfb$(EXEEXT)
	$(BINDIR)/mkfb$(EXEEXT) --min-ok -o $@

examples/min_ok.fb: $(BINDIR)/mkfb$(EXEEXT)
	$(BINDIR)/mkfb$(EXEEXT) --min-ok -o $@

test: all
	./$(BINDIR)/run_tests$(EXEEXT) --categ=default
	@echo "OK: make test finished"

test-all: all
	./$(BINDIR)/run_tests$(EXEEXT) --categ=all
	@echo "OK: make test-all finished"

test-bench: all
	./$(BINDIR)/run_tests$(EXEEXT) --categ=bench
	@echo "OK: make test-bench finished"

test-flatl: all
	./$(BINDIR)/run_tests$(EXEEXT) --flatl --categ=default
	@echo "OK: make test-flatl finished"

# Fleditor needs a Linux Node (not a WSL shim to Windows node.exe — breaks on UNC paths).
# Override: make fleditor-serve NODE=/path/to/linux/node
_FLEDITOR_LINUX_NODE := $(shell sh scripts/fleditor_linux_node.sh 2>/dev/null || true)
ifeq ($(origin NODE),command)
# keep NODE from: make NODE=/usr/bin/node fleditor-serve
else
override NODE := $(_FLEDITOR_LINUX_NODE)
endif
FLEDITOR_PORT ?= 8765

define FLEDITOR_NODE_HINT
No Linux Node for fleditor-serve. On WSL, ~/.local/bin/node often wraps Windows node.exe
and fails with ERR_INVALID_PACKAGE_CONFIG / UNC wsl$$ paths.

  make fleditor-node-check
  # apt nodejs 12.x is old; for smokes prefer: nvm install --lts
  make fleditor-serve-forced NODE=/usr/bin/node

endef
export FLEDITOR_NODE_HINT

fleditor-wasm:
	$(MAKE) -f Makefile.wasm fleditor-wasm

fleditor-rebuild: rebuild
	@echo "Tip: hard-refresh the browser after compiler changes (Ctrl+Shift+R)."

fleditor-node-check:
	@echo "=== uname ==="
	@uname -a
	@echo "=== /usr/bin/node (apt) ==="
	@/usr/bin/node --version 2>&1 || echo "FAIL: apt node does not run (common on WSL1 → Exec format error)"
	@echo "=== Linux picker ==="
	@sh "$(CURDIR)/scripts/fleditor_linux_node.sh" 2>/dev/null || echo "(none)"
	@echo "=== Windows node.exe ==="
	@ls "/mnt/c/Program Files/nodejs/node.exe" 2>/dev/null && \
	  "/mnt/c/Program Files/nodejs/node.exe" --version 2>&1 || echo "(not found)"
	@echo "OK: make fleditor-serve-forced (Linux node, else Windows node.exe + wslpath)"

fleditor-serve:
	sh "$(CURDIR)/scripts/fleditor_run_serve.sh" $(FLEDITOR_PORT)

fleditor-stop:
	sh "$(CURDIR)/scripts/fleditor_stop.sh" $(FLEDITOR_PORT)

fleditor-serve-forced: fleditor-stop
	sh "$(CURDIR)/scripts/fleditor_run_serve.sh" $(FLEDITOR_PORT)

fleditor-smoke:
	$(NODE) test/fleditor/serve_smoke.js

fleditor-layout-smoke:
	$(NODE) test/fleditor/layout_smoke.mjs

fleditor-theme-smoke:
	$(NODE) test/fleditor/theme_smoke.mjs

fleditor-project-smoke:
	$(NODE) test/fleditor/project_smoke.mjs

fleditor-hex-smoke:
	$(NODE) test/fleditor/hex_roundtrip.mjs
	$(NODE) test/fleditor/hex_project_smoke.mjs
	$(NODE) test/fleditor/hex_grid.mjs

fleditor-examples-smoke:
	$(NODE) test/fleditor/examples_smoke.mjs

fleditor-wasm-smoke: fleditor-wasm
	$(NODE) test/fleditor/wasm_smoke.mjs

fleditor-wasm-multifile-smoke: fleditor-wasm
	$(NODE) test/fleditor/wasm_multifile_smoke.mjs

fleditor-wasm-order-flow-smoke: fleditor-wasm
	$(NODE) test/fleditor/wasm_order_flow_smoke.mjs

fleditor-wasm-var-smoke: fleditor-wasm
	$(NODE) test/fleditor/wasm_var_smoke.mjs

clean:
	rm -rf $(BINDIR)
	rm -f $(MIN_OK_FB)
