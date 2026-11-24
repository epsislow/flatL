ifeq ($(origin CC),default)
CC      = gcc
endif
CFLAGS  ?= -Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE
LDFLAGS ?=

# Optional modules (lowercase). Examples: make rebuild http=0 | dom_compile=0 | dom=0
# Prefer `make rebuild http=0` (clean+build) when toggling — do not mix object caches.
http ?= 1
# Default: bundle Lexbor from external/lexbor (already in repo).
# dom_compile=0 → no DOM unless you also pass dom=1 (system Lexbor).
# Explicit dom=0 wins even when dom_compile stays at its default 1.
dom_compile ?= 1
ifeq ($(dom_compile),1)
dom ?= 1
else
dom ?= 0
endif
ifeq ($(dom),0)
CFLAGS += -DFLAT_WITH_DOM=0
else
CFLAGS += -DFLAT_WITH_DOM=1
ifeq ($(dom_compile),1)
DOM_SRC = src/lib/mod_dom.c
DOM_CFLAGS = -DFLAT_DOM_BUNDLED
DOM_LIBS =
else
DOM_SRC = src/lib/mod_dom.c
DOM_CFLAGS ?= $(shell pkg-config --cflags lexbor 2>/dev/null)
DOM_LIBS ?= $(shell pkg-config --libs lexbor 2>/dev/null || echo -llexbor)
endif
endif
ifeq ($(http),0)
CFLAGS += -DFLAT_WITH_HTTP=0
endif
env ?= 1
ifeq ($(env),0)
CFLAGS += -DFLAT_WITH_ENV=0
endif
path ?= 1
ifeq ($(path),0)
CFLAGS += -DFLAT_WITH_PATH=0
endif
fs ?= 1
ifeq ($(fs),0)
CFLAGS += -DFLAT_WITH_FS=0
endif
enc ?= 1
ifeq ($(enc),0)
CFLAGS += -DFLAT_WITH_ENC=0
endif
# Default: bundle PCRE2 from external/pcre2 (like dom/dom_compile).
# re_compile=0 → no re unless re=1 (system libpcre2-8).
# Explicit re=0 wins even when re_compile stays at its default 1.
re_compile ?= 1
ifeq ($(re_compile),1)
re ?= 1
else
re ?= 0
endif
ifeq ($(re),0)
CFLAGS += -DFLAT_WITH_RE=0
RE_SRC =
RE_CFLAGS =
RE_LIBS =
else
CFLAGS += -DFLAT_WITH_RE=1
ifeq ($(re_compile),1)
RE_SRC = src/lib/mod_re.c src/lib/flat_re_pcre2.c
RE_CFLAGS = -DFLAT_RE_BUNDLED
RE_LIBS =
else
RE_SRC = src/lib/mod_re.c
RE_CFLAGS ?= $(shell pkg-config --cflags libpcre2-8 2>/dev/null)
RE_LIBS ?= $(shell pkg-config --libs libpcre2-8 2>/dev/null || echo -lpcre2-8)
endif
endif
hash ?= 1
ifeq ($(hash),0)
CFLAGS += -DFLAT_WITH_HASH=0
HASH_SRC =
HASH_CFLAGS =
else
HASH_SRC = src/lib/mod_hash.c src/common/flat_hash.c \
	external/hash/WjCryptLib_Md5.c external/hash/WjCryptLib_Sha1.c \
	external/hash/WjCryptLib_Sha256.c external/hash/WjCryptLib_Sha512.c
HASH_CFLAGS = -Iexternal/hash
endif

# Quiet progress (default). V=1 → classic Make command echo + no step labels.
V ?= 0
ifneq ($(V),1)
MAKEFLAGS += -s --no-print-directory
endif

# Recipe marks: name+ = bundled / in-tree sources; name* = system library link;
# bare name = optional with no third-party link mode. Disabled modules are omitted.
empty :=
space := $(empty) $(empty)
comma := ,
_mod_http := $(if $(filter 1,$(http)),http*)
_mod_dom := $(if $(filter 1,$(dom)),$(if $(filter 1,$(dom_compile)),dom+,dom*))
_mod_env := $(if $(filter 1,$(env)),env)
_mod_path := $(if $(filter 1,$(path)),path)
_mod_fs := $(if $(filter 1,$(fs)),fs)
_mod_enc := $(if $(filter 1,$(enc)),enc+)
_mod_re := $(if $(filter 1,$(re)),$(if $(filter 1,$(re_compile)),re+,re*))
_mod_hash := $(if $(filter 1,$(hash)),hash+)
RECIPE_TOKENS := $(strip $(_mod_http) $(_mod_dom) $(_mod_env) $(_mod_path) $(_mod_fs) $(_mod_enc) $(_mod_re) $(_mod_hash))
RECIPE := $(if $(RECIPE_TOKENS),$(subst $(space),$(comma),$(RECIPE_TOKENS)),(none))

# Recurse with the same feature flags + verbosity.
MAKE_REC = $(MAKE) V=$(V) http=$(http) dom=$(dom) dom_compile=$(dom_compile) \
	env=$(env) path=$(path) fs=$(fs) enc=$(enc) re=$(re) re_compile=$(re_compile) hash=$(hash)

ifeq ($(V),1)
step_begin = :
step_end = :
step_skip = :
announce = :
done_msg = :
else
step_begin = printf '  %-20s ' "$(1)"
step_end = echo OK
step_skip = printf '  %-20s ' "$(1)"; echo SKIP
announce = echo "$(1)"
done_msg = echo "done: $(1)"
endif

# MinGW default stack (~1MB) is too small for current flvm/flatl on Windows:
# STATUS_STACK_OVERFLOW (0xC00000FD) is reported by MSYS2 as exit 127.
ifeq ($(OS),Windows_NT)
FLC_LDFLAGS ?= -Wl,--stack,8388608
else
FLC_LDFLAGS ?=
endif

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
              src/common/flat_slot_type.c src/common/flat_list_typed.c src/common/flat_list_shape.c \
              src/common/flat_number.c \
              src/common/value_pool.c \
              src/common/value_collection.c src/common/value_live.c
DISASM_SRC = src/common/flat_disasm.c
FBDUMP_SRCS = src/fbdump/main.c $(DISASM_SRC) $(FB_TOOL_SRC) $(NATIVE_FLC_SRC)
NATIVE_VM_PLUGIN = src/lib/native_plugin_host.c
ifeq ($(OS),Windows_NT)
NATIVE_VM_MATH = src/lib/mod_math_trig.c src/lib/mod_math_stats.c src/lib/mod_math_mdim.c \
                 src/lib/mod_math.c src/lib/flat_math_calc.c src/lib/mod_list.c \
                 src/lib/mod_list_values_to.c src/lib/mod_obj.c \
                 src/lib/mod_csv.c
VM_EXTRA_LIBS =
PLUGIN_TARGET =
else
NATIVE_VM_MATH =
VM_EXTRA_LIBS = -ldl
PLUGIN_TARGET = $(BINDIR)/libflat_math_trig.so $(BINDIR)/libflat_math_stats.so \
                $(BINDIR)/libflat_math_mdim.so $(BINDIR)/libflat_math.so $(BINDIR)/libflat_list.so \
                $(BINDIR)/libflat_obj.so $(BINDIR)/libflat_csv.so
PLUGIN_COMMON_SRCS = src/lib/native_util.c src/lib/native_parse_rhs.c src/common/flat_native_tag_common.c \
               src/common/flat_native_tag_runtime.c src/common/value_parse.c src/common/value_ops.c \
               src/common/flat_lit.c \
               src/common/value_json.c src/common/flat_number.c src/common/value_pool.c \
               src/common/value_collection.c src/common/value_live.c src/common/flat_assign.c \
               src/common/flat_obj_path.c src/common/flat_schema.c src/common/flat_slot_type.c \
               src/common/flat_list_typed.c src/common/flat_list_shape.c src/common/flat_str.c
PLUGIN_TRIG_SRCS = src/lib/plugin_math_trig_entry.c src/lib/mod_math_trig.c $(PLUGIN_COMMON_SRCS)
PLUGIN_STATS_SRCS = src/lib/plugin_math_stats_entry.c src/lib/mod_math_stats.c $(PLUGIN_COMMON_SRCS)
PLUGIN_MDIM_SRCS = src/lib/plugin_math_mdim_entry.c src/lib/mod_math_mdim.c $(PLUGIN_COMMON_SRCS)
# flat_rng lives in flvm/flatl (shared process RNG for math.random + hash.randomBytes).
# Do not link a second copy into the math plugin.
PLUGIN_MATH_SRCS = src/lib/plugin_math_entry.c src/lib/mod_math.c src/lib/flat_math_calc.c \
               $(PLUGIN_COMMON_SRCS)
PLUGIN_LIST_SRCS = src/lib/plugin_list_entry.c src/lib/mod_list.c src/lib/mod_list_values_to.c \
               $(PLUGIN_COMMON_SRCS)
PLUGIN_OBJ_SRCS = src/lib/plugin_obj_entry.c src/lib/mod_obj.c $(PLUGIN_COMMON_SRCS)
PLUGIN_CSV_SRCS = src/lib/plugin_csv_entry.c src/lib/mod_csv.c src/common/flat_csv.c \
               src/common/flat_csv_runtime.c src/common/flat_file.c $(PLUGIN_COMMON_SRCS)
endif
ifeq ($(http),0)
HTTP_SRC =
HTTP_CFLAGS =
HTTP_LIBS =
ENSURE_LIBZ_1 =
else
HTTP_SRC = src/lib/mod_http.c src/http/flat_http_curl.c
HTTP_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null)
HTTP_LIBS ?= $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
# MSYS2 UCRT64: libcurl PE imports libz-1.dll; mingw zlib package ships zlib1.dll.
ENSURE_LIBZ_1 =
ifeq ($(MSYSTEM),UCRT64)
ENSURE_LIBZ_1 = ensure-libz-1
endif
ifneq ($(findstring ucrt64,$(MINGW_PREFIX)),)
ENSURE_LIBZ_1 = ensure-libz-1
endif
endif
ifeq ($(env),0)
ENV_SRC =
else
ENV_SRC = src/lib/mod_env.c src/common/flat_env.c
endif
ifeq ($(path),0)
PATH_SRC =
else
PATH_SRC = src/lib/mod_path.c
endif
ifeq ($(fs),0)
FS_SRC =
else
FS_SRC = src/lib/mod_fs.c src/common/flat_fs.c
endif
ifeq ($(enc),0)
ENC_SRC =
else
ENC_SRC = src/lib/mod_enc.c
endif
# flat_path always linked (file.sep + path/fs); mod_path optional
FLAT_PATH_SRC = src/common/flat_path.c
# flat_enc always linked (http.basic/form + optional enc); mod_enc optional
FLAT_ENC_SRC = src/common/flat_enc.c
NATIVE_MODS_SRC = src/lib/native_mods.c
NATIVE_VM_SRC = src/lib/native_registry.c src/lib/native_util.c src/lib/native_parse_rhs.c \
                src/lib/native_dispatch.c \
                src/lib/flat_date.c src/lib/mod_io.c src/lib/mod_str.c src/lib/mod_json.c src/lib/mod_json_query.c \
                src/lib/mod_date.c src/lib/mod_mem.c src/lib/mod_file.c src/lib/mod_vcx.c \
                src/lib/mod_vcx_dschema.c src/lib/mod_executor.c $(HTTP_SRC) $(DOM_SRC) $(ENV_SRC) $(PATH_SRC) $(FS_SRC) $(ENC_SRC) $(RE_SRC) $(HASH_SRC) $(FLAT_PATH_SRC) $(FLAT_ENC_SRC) $(NATIVE_MODS_SRC) $(NATIVE_VM_MATH) $(NATIVE_VM_PLUGIN)
NATIVE_FLC_SRC = src/lib/native_registry.c src/lib/native_util.c $(NATIVE_MODS_SRC) src/common/flat_native_tag_common.c
VM_SRC  = src/common/flat_time.c src/common/vm.c src/common/flat_func_tag.c \
          src/common/doc_lookup.c src/common/flat_number.c \
          src/common/value_pool.c \
          src/common/value_collection.c src/common/value_live.c src/common/flat_assign.c \
          src/common/flat_slot_type.c src/common/flat_list_typed.c src/common/flat_list_shape.c \
          src/common/flat_schema.c \
          src/common/flat_dynamic_bind.c \
          src/common/value_ops.c src/common/flat_lit.c src/common/flat_rng.c src/common/flat_obj_path.c \
          src/common/flat_vcx.c src/common/flat_executor.c \
          src/common/value_parse.c src/common/value_format.c src/common/value_io.c \
          src/common/flat_native_tag_common.c src/common/flat_native_tag_runtime.c \
          src/common/value_json.c           src/common/flat_file.c src/common/flat_csv.c src/common/flat_csv_runtime.c \
          src/common/flat_io_ctx.c src/common/flat_memstream.c \
          $(STR_SRC) $(NATIVE_VM_SRC)
FLAT_TIME_SRC = src/common/flat_time.c
FGIR_SRC = src/common/parse.c src/common/link.c src/common/compile.c src/common/flat_func.c \
               src/common/flat_func_tag.c src/common/flat_assign.c src/common/flat_slot_type.c \
               src/common/flat_list_typed.c src/common/flat_list_shape.c src/common/flat_schema.c \
               src/common/flat_number.c src/common/flat_lit.c \
               src/common/value_pool.c \
               src/common/value_collection.c src/common/value_live.c \
               src/common/line_label.c src/common/flat_jump_block.c \
           src/common/native_use.c src/common/flat_native_tag_validate.c \
           $(FLAT_TIME_SRC) \
           $(STR_SRC) $(NATIVE_FLC_SRC)
FLATL_SRC = src/flatl/main.c src/common/parse.c src/common/link.c src/common/compile.c \
           src/common/flat_func.c src/common/line_label.c src/common/flat_jump_block.c \
            src/common/native_use.c src/common/flat_native_tag_validate.c $(FB_SRC) $(VM_SRC) $(PORT_SRC)

MIN_OK_FB = test/cases/flvm/001_min_ok/min_ok.fb examples/min_ok.fb

# Default: compile only (does not run tests)
.DEFAULT_GOAL := all

.PHONY: all help clean test test-all test-bench test-perf test-flatl fixtures tools flvm flc flatl mkfb fbdump run_tests gen_doc_site docs io_generate_tests json_generate_tests date_generate_tests rebuild rebuild-native fleditor-wasm fleditor-node-check fleditor-serve fleditor-stop fleditor-serve-forced fleditor-smoke fleditor-layout-smoke fleditor-theme-smoke fleditor-project-smoke fleditor-hex-smoke fleditor-csv-smoke fleditor-jsonl-smoke fleditor-jsonl-adv-filter-smoke fleditor-wasm-csv-smoke fleditor-examples-smoke fleditor-block-map-smoke fleditor-wasm-smoke fleditor-wasm-multifile-smoke fleditor-rebuild ensure-libz-1

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
	@echo "Verbosity:"
	@echo "  make / make rebuild     quiet progress (recipe + OK/SKIP steps)"
	@echo "  make V=1 …              show full compiler / tool command lines"
	@echo "  recipe marks: name+ bundled/in-tree, name* system lib, bare=no link mode"
	@echo ""
	@echo "Optional modules (pass on the same make line; lowercase):"
	@echo "  http=0            build without native http / libcurl"
	@echo "  env=0 path=0 fs=0 enc=0 re=0 hash=0  omit optional embedded modules"
	@echo "  make rebuild http=0"
	@echo "  make flvm http=0   (prefer rebuild when toggling http)"
	@echo "  dom_compile=1     default: bundle Lexbor from external/lexbor"
	@echo "  dom_compile=0     build without DOM (or dom=1 for system Lexbor)"
	@echo "  re_compile=1      default: bundle PCRE2 from external/pcre2"
	@echo "  re_compile=0      build without re (or re=1 for system libpcre2-8)"
	@echo "  make rebuild | make rebuild dom_compile=0 | make rebuild dom=0"
	@echo "  flc|flvm|flatl --mods   list modules built into the binary"
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
	@echo "  make fleditor-csv-smoke            CSV table editor smokes (Node)"
	@echo "  make fleditor-jsonl-smoke          JSONL records editor smokes (Node)"
	@echo "  make fleditor-jsonl-adv-filter-smoke  JSONL Adv.Filter query engine (Node)"
	@echo "  make fleditor-wasm-csv-smoke       CSV bytes on MEMFS via WASM (needs fleditor-wasm)"
	@echo "  make fleditor-examples-smoke       examples picker smoke (Node)"
	@echo "  make fleditor-block-map-smoke      Code Map analyzer smoke (Node)"
	@echo "  make fleditor-wasm-smoke           WASM smoke (needs fleditor-wasm)"
	@echo "  make fleditor-wasm-multifile-smoke WASM multifile smoke (needs fleditor-wasm)"
	@echo "  make fleditor-wasm-order-flow-smoke   WASM user def (order_flow; needs fleditor-wasm)"
	@echo ""
	@echo "Note: if sources unchanged, make skips that compile step (no line)."
	@echo "      Failed steps print FAIL (or the compiler error) — use V=1 for full cmds."
	@echo ""
	@echo "Test:"
	@echo "  make test         all + run_tests --categ=default (excludes bench/slow)"
	@echo "  make test-all     all + run_tests --categ=all"
	@echo "  make test-bench   all + run_tests --categ=bench"
	@echo "  make test-perf    all + run_tests --each --categ=perf (informational performance baselines)"
	@echo "  make test-flatl     all + run_tests --flatl --categ=default"
	@echo "  bin/run_tests -h  filters, -v, -e, -t, --list, -w, --flatl (see doc_src/TOOLCHAIN.md)"
	@echo ""
	@echo "Clean:"
	@echo "  make clean        remove bin/ and generated min_ok.fb"
	@echo ""
	@echo "Docs: doc_src/TOOLCHAIN.md  doc_src/FMOD_MODULES.md  |  Pages: docs/"

# Full toolchain binaries + bytecode fixtures (still does not run tests)
all: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/run_tests$(EXEEXT) $(BINDIR)/port_check$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT) $(BINDIR)/fbdump$(EXEEXT) $(BINDIR)/gen_doc_site$(EXEEXT) $(PLUGIN_TARGET) fixtures
ifeq ($(PLUGIN_TARGET),)
	@$(call step_skip,plugins)
endif
	@$(call done_msg,all)

tools: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT) $(BINDIR)/fbdump$(EXEEXT) $(PLUGIN_TARGET)
	@$(call done_msg,tools)

flvm: $(BINDIR)/flvm$(EXEEXT)

flc: $(BINDIR)/flc$(EXEEXT)

flatl: $(BINDIR)/flatl$(EXEEXT)

mkfb: $(BINDIR)/mkfb$(EXEEXT)

fbdump: $(BINDIR)/fbdump$(EXEEXT)

run_tests: $(BINDIR)/run_tests$(EXEEXT)

io_generate_tests: $(BINDIR)/io_generate_tests$(EXEEXT)
	@$(call step_begin,io_generate_tests)
	$(BINDIR)/io_generate_tests$(EXEEXT)
	@$(call step_end)

json_generate_tests: $(BINDIR)/json_generate_tests$(EXEEXT)
	@$(call step_begin,json_generate_tests)
	find test/cases/json -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
	$(BINDIR)/json_generate_tests$(EXEEXT)
	@$(call step_end)

date_generate_tests:
	@$(call step_begin,date_generate_tests)
	python3 scripts/gen_date_tests.py
	@$(call step_end)

# Sequential clean→build so `make -j` cannot delete bin/ while compiling.
rebuild:
	@$(call announce,recipe: $(RECIPE))
	$(MAKE_REC) clean
	$(MAKE_REC) all
	$(MAKE_REC) fleditor-wasm-optional
	@$(call done_msg,rebuild)

rebuild-native:
	@$(call announce,recipe: $(RECIPE))
	$(MAKE_REC) clean
	$(MAKE_REC) all
	@$(call done_msg,rebuild-native)

.PHONY: fleditor-wasm-optional
fleditor-wasm-optional:
	@if command -v emcc >/dev/null 2>&1; then \
		$(MAKE) -f Makefile.wasm V=$(V) http=$(http) dom=$(dom) dom_compile=$(dom_compile) \
			env=$(env) path=$(path) fs=$(fs) enc=$(enc) re=$(re) re_compile=$(re_compile) hash=$(hash) fleditor-wasm-clean; \
		$(MAKE) -f Makefile.wasm V=$(V) http=$(http) dom=$(dom) dom_compile=$(dom_compile) \
			env=$(env) path=$(path) fs=$(fs) enc=$(enc) re=$(re) re_compile=$(re_compile) hash=$(hash) fleditor-wasm; \
	else \
		if [ "$(V)" = "1" ]; then echo "skipped fleditor-wasm — no emcc on PATH"; \
		else printf '  %-20s ' "fleditor-wasm"; echo SKIP; fi; \
	fi

fixtures: $(MIN_OK_FB)

# Shim for MSYS2 UCRT64 curl↔zlib DLL name mismatch (safe no-op if already present).
ensure-libz-1:
	@zdir="$(or $(MINGW_PREFIX),/ucrt64)/bin"; \
	if [ -f "$$zdir/zlib1.dll" ] && [ ! -e "$$zdir/libz-1.dll" ]; then \
		printf '  %-20s ' "libz-1 shim"; \
		(ln -sf zlib1.dll "$$zdir/libz-1.dll" 2>/dev/null) || cp -f "$$zdir/zlib1.dll" "$$zdir/libz-1.dll"; \
		echo OK; \
	fi

$(BINDIR)/flvm$(EXEEXT): src/flvm/main.c $(FB_SRC) $(VM_SRC) $(PORT_SRC) | $(BINDIR) $(ENSURE_LIBZ_1)
	@$(call step_begin,flvm)
	$(CC) $(CFLAGS) -o $@ src/flvm/main.c $(FB_SRC) $(VM_SRC) $(PORT_SRC) $(LDFLAGS) $(FLC_LDFLAGS) -Wl,--export-dynamic $(VM_EXTRA_LIBS) $(HTTP_CFLAGS) $(DOM_CFLAGS) $(RE_CFLAGS) $(HASH_CFLAGS) $(HTTP_LIBS) $(DOM_LIBS) $(RE_LIBS) -lm
	@$(call step_end)

$(BINDIR)/libflat_math_trig.so: $(PLUGIN_TRIG_SRCS) | $(BINDIR)
	@$(call step_begin,plugin-math-trig)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_TRIG_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/libflat_math_stats.so: $(PLUGIN_STATS_SRCS) | $(BINDIR)
	@$(call step_begin,plugin-math-stats)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_STATS_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/libflat_math_mdim.so: $(PLUGIN_MDIM_SRCS) | $(BINDIR)
	@$(call step_begin,plugin-math-mdim)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_MDIM_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/libflat_math.so: $(PLUGIN_MATH_SRCS) | $(BINDIR)
	@$(call step_begin,plugin-math)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_MATH_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/libflat_list.so: $(PLUGIN_LIST_SRCS) | $(BINDIR)
	@$(call step_begin,plugin-list)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_LIST_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/libflat_obj.so: $(PLUGIN_OBJ_SRCS) | $(BINDIR)
	@$(call step_begin,plugin-obj)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_OBJ_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/libflat_csv.so: $(PLUGIN_CSV_SRCS) | $(BINDIR)
	@$(call step_begin,plugin-csv)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_CSV_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/flc$(EXEEXT): src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(DISASM_SRC) $(PORT_SRC) | $(BINDIR)
	@$(call step_begin,flc)
	$(CC) $(CFLAGS) -o $@ src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(DISASM_SRC) $(PORT_SRC) $(LDFLAGS) $(FLC_LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/fbdump$(EXEEXT): $(FBDUMP_SRCS) | $(BINDIR)
	@$(call step_begin,fbdump)
	$(CC) $(CFLAGS) -o $@ $(FBDUMP_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/flatl$(EXEEXT): $(FLATL_SRC) | $(BINDIR) $(ENSURE_LIBZ_1)
	@$(call step_begin,flatl)
	$(CC) $(CFLAGS) -o $@ $(FLATL_SRC) $(LDFLAGS) $(FLC_LDFLAGS) -Wl,--export-dynamic $(VM_EXTRA_LIBS) $(HTTP_CFLAGS) $(DOM_CFLAGS) $(RE_CFLAGS) $(HASH_CFLAGS) $(HTTP_LIBS) $(DOM_LIBS) $(RE_LIBS) -lm
	@$(call step_end)

$(BINDIR)/mkfb$(EXEEXT): src/mkfb/main.c $(FB_TOOL_SRC) | $(BINDIR)
	@$(call step_begin,mkfb)
	$(CC) $(CFLAGS) -o $@ src/mkfb/main.c $(FB_TOOL_SRC) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/gen_doc_site$(EXEEXT): $(GEN_DOC_SITE_SRCS) | $(BINDIR)
	@$(call step_begin,gen_doc_site)
	$(CC) $(CFLAGS) -o $@ $(GEN_DOC_SITE_SRCS) $(LDFLAGS)
	@$(call step_end)

docs gen_doc_site: $(BINDIR)/gen_doc_site$(EXEEXT)
	@$(call step_begin,docs)
	$(BINDIR)/gen_doc_site$(EXEEXT)
	@$(call step_end)

RUN_TESTS_SRCS = \
	src/run_tests/main.c \
	src/run_tests/discover.c \
	src/run_tests/case.c \
	src/common/repo.c \
	$(FLAT_TIME_SRC) \
	$(PORT_LIB)

$(BINDIR)/run_tests$(EXEEXT): $(RUN_TESTS_SRCS) | $(BINDIR)
	@$(call step_begin,run_tests)
	$(CC) $(CFLAGS) -o $@ $(RUN_TESTS_SRCS) $(LDFLAGS) -lm
	@$(call step_end)

$(BINDIR)/port_check$(EXEEXT): src/port_check/main.c $(STR_SRC) | $(BINDIR)
	@$(call step_begin,port_check)
	$(CC) $(CFLAGS) -o $@ src/port_check/main.c $(STR_SRC) $(LDFLAGS)
	@$(call step_end)

GEN_REPO_SRCS = src/common/repo.c $(PORT_LIB)

GEN_DOC_SITE_SRCS = src/gen_doc_site/main.c src/gen_doc_site/index_json.c src/gen_doc_site/emit.c \
                      src/gen_doc_site/fs_util.c $(GEN_REPO_SRCS)

$(BINDIR)/io_generate_tests$(EXEEXT): src/io_generate_tests/main.c $(GEN_REPO_SRCS) | $(BINDIR)
	@$(call step_begin,io_generate_tests.bin)
	$(CC) $(CFLAGS) -o $@ src/io_generate_tests/main.c $(GEN_REPO_SRCS) $(LDFLAGS)
	@$(call step_end)

$(BINDIR)/json_generate_tests$(EXEEXT): src/json_generate_tests/main.c $(GEN_REPO_SRCS) | $(BINDIR)
	@$(call step_begin,json_generate_tests.bin)
	$(CC) $(CFLAGS) -o $@ src/json_generate_tests/main.c $(GEN_REPO_SRCS) $(LDFLAGS)
	@$(call step_end)

$(BINDIR):
	@mkdir -p $(BINDIR)

test/cases/flvm/001_min_ok/min_ok.fb: $(BINDIR)/mkfb$(EXEEXT)
	@$(call step_begin,fixture-min_ok)
	$(BINDIR)/mkfb$(EXEEXT) --min-ok -o $@
	@$(call step_end)

examples/min_ok.fb: $(BINDIR)/mkfb$(EXEEXT)
	@$(call step_begin,fixture-examples)
	$(BINDIR)/mkfb$(EXEEXT) --min-ok -o $@
	@$(call step_end)

test: all
	@$(call announce,test: categ=default)
	./$(BINDIR)/run_tests$(EXEEXT) --categ=default
	@$(call done_msg,test)

test-all: all
	@$(call announce,test: categ=all)
	./$(BINDIR)/run_tests$(EXEEXT) --categ=all
	@$(call done_msg,test-all)

test-bench: all
	@$(call announce,test: categ=bench)
	./$(BINDIR)/run_tests$(EXEEXT) --categ=bench
	@$(call done_msg,test-bench)

test-perf: all
	@$(call announce,test: categ=perf)
	./$(BINDIR)/run_tests$(EXEEXT) --each --categ=perf
	@$(call done_msg,test-perf)

test-flatl: all
	@$(call announce,test: flatl categ=default)
	./$(BINDIR)/run_tests$(EXEEXT) --flatl --categ=default
	@$(call done_msg,test-flatl)

# Fleditor needs a Linux Node (not a WSL shim to Windows node.exe — breaks on UNC paths).
# Override: make fleditor-serve NODE=/path/to/linux/node
_FLEDITOR_LINUX_NODE := $(shell sh scripts/fleditor_linux_node.sh 2>/dev/null || true)
ifeq ($(origin NODE),command line)
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
	$(MAKE) -f Makefile.wasm V=$(V) http=$(http) dom=$(dom) dom_compile=$(dom_compile) \
		env=$(env) path=$(path) fs=$(fs) enc=$(enc) re=$(re) re_compile=$(re_compile) hash=$(hash) fleditor-wasm

fleditor-rebuild: rebuild
	@$(if $(filter 1,$(V)),echo "Tip: hard-refresh the browser after compiler changes (Ctrl+Shift+R).",:)

fleditor-node-check:
	@echo "=== uname ==="
	@uname -a
	@echo "=== /usr/bin/node (apt) ==="
	@/usr/bin/node --version 2>&1 || echo "FAIL: apt node does not run (common on WSL1 → Exec format error)"
	@echo "=== Linux picker ==="
	@sh "$(CURDIR)/scripts/fleditor_linux_node.sh" 2>/dev/null || echo "(none)"
	@echo "=== PATH: command -v node / node.exe ==="
	@command -v node 2>/dev/null && node --version 2>&1 || \
	  command -v node.exe 2>/dev/null && node.exe --version 2>&1 || \
	  echo "(not on MSYS PATH — cmd may still see it; try MSYS2_PATH_TYPE=inherit)"
	@echo "=== Windows Path via where ==="
	@cmd //c "where node" 2>/dev/null | tr -d '\r' | head -n1 | \
	  { read -r w; if [ -n "$$w" ]; then echo "$$w"; cmd //c "node --version"; else echo "(not found)"; fi; }
	@echo "OK: make fleditor-serve-forced (Linux node, else Windows node.exe + wslpath)"

# Long-running servers: keep script output visible (no step_begin/end wrapper).
fleditor-serve:
	sh "$(CURDIR)/scripts/fleditor_run_serve.sh" $(FLEDITOR_PORT)

fleditor-stop:
	@$(call step_begin,fleditor-stop)
	sh "$(CURDIR)/scripts/fleditor_stop.sh" $(FLEDITOR_PORT)
	@$(call step_end)

fleditor-serve-forced: fleditor-stop
	sh "$(CURDIR)/scripts/fleditor_run_serve.sh" $(FLEDITOR_PORT)

fleditor-smoke:
	@$(call step_begin,fleditor-smoke)
	$(NODE) test/fleditor/serve_smoke.js
	@$(call step_end)

fleditor-layout-smoke:
	@$(call step_begin,fleditor-layout-smoke)
	$(NODE) test/fleditor/layout_smoke.mjs
	@$(call step_end)

fleditor-theme-smoke:
	@$(call step_begin,fleditor-theme-smoke)
	$(NODE) test/fleditor/theme_smoke.mjs
	@$(call step_end)

fleditor-project-smoke:
	@$(call step_begin,fleditor-project-smoke)
	$(NODE) test/fleditor/project_smoke.mjs
	@$(call step_end)

fleditor-hex-smoke:
	@$(call step_begin,fleditor-hex-smoke)
	$(NODE) test/fleditor/hex_roundtrip.mjs
	$(NODE) test/fleditor/hex_project_smoke.mjs
	$(NODE) test/fleditor/hex_grid.mjs
	@$(call step_end)

fleditor-csv-smoke:
	@$(call step_begin,fleditor-csv-smoke)
	$(NODE) test/fleditor/csv_parse.mjs
	$(NODE) test/fleditor/text_format.mjs
	$(NODE) test/fleditor/csv_roundtrip.mjs
	$(NODE) test/fleditor/csv_selection_stats.mjs
	$(NODE) test/fleditor/csv_vm_parity.mjs
	$(NODE) test/fleditor/csv_writer_encoding_bytes.mjs
	@$(call step_end)

fleditor-jsonl-smoke:
	@$(call step_begin,fleditor-jsonl-smoke)
	$(NODE) test/fleditor/jsonl_parse.mjs
	@$(call step_end)

fleditor-jsonl-adv-filter-smoke:
	@$(call step_begin,fleditor-jsonl-adv-filter)
	$(NODE) test/fleditor/jsonl_adv_filter.mjs
	@$(call step_end)

fleditor-examples-smoke:
	@$(call step_begin,fleditor-examples-smoke)
	$(NODE) test/fleditor/examples_smoke.mjs
	@$(call step_end)

fleditor-block-map-smoke:
	@$(call step_begin,fleditor-block-map-smoke)
	$(NODE) test/fleditor/block_analyzer.mjs
	$(NODE) test/fleditor/local_blocks.mjs
	@$(call step_end)

fleditor-wasm-csv-smoke: fleditor-wasm
	@$(call step_begin,fleditor-wasm-csv-smoke)
	$(NODE) test/fleditor/wasm_csv_smoke.mjs
	$(NODE) test/fleditor/wasm_csv_reader_smoke.mjs
	@$(call step_end)

fleditor-wasm-smoke: fleditor-wasm
	@$(call step_begin,fleditor-wasm-smoke)
	$(NODE) test/fleditor/wasm_smoke.mjs
	@$(call step_end)

fleditor-wasm-multifile-smoke: fleditor-wasm
	@$(call step_begin,fleditor-wasm-multifile)
	$(NODE) test/fleditor/wasm_multifile_smoke.mjs
	@$(call step_end)

fleditor-wasm-order-flow-smoke: fleditor-wasm
	@$(call step_begin,fleditor-wasm-order-flow)
	$(NODE) test/fleditor/wasm_order_flow_smoke.mjs
	@$(call step_end)

fleditor-wasm-var-smoke: fleditor-wasm
	@$(call step_begin,fleditor-wasm-var-smoke)
	$(NODE) test/fleditor/wasm_var_smoke.mjs
	@$(call step_end)

clean:
	@$(call step_begin,clean)
	rm -rf $(BINDIR)
	rm -f $(MIN_OK_FB)
	@$(call step_end)
