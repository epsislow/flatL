CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -Iinclude
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
NATIVE_VM_PLUGIN = src/lib/native_plugin_host.c
ifeq ($(OS),Windows_NT)
NATIVE_VM_MATH = src/lib/mod_math_trig.c src/lib/mod_math_stats.c src/lib/mod_math.c \
                 src/lib/mod_list.c src/lib/mod_obj.c
VM_EXTRA_LIBS =
PLUGIN_TARGET =
else
NATIVE_VM_MATH =
VM_EXTRA_LIBS = -ldl
PLUGIN_TARGET = $(BINDIR)/libflat_math_trig.so $(BINDIR)/libflat_math_stats.so \
                $(BINDIR)/libflat_math.so $(BINDIR)/libflat_list.so $(BINDIR)/libflat_obj.so
PLUGIN_COMMON_SRCS = src/lib/native_util.c src/common/value_parse.c src/common/value_ops.c \
               src/common/value_pool.c src/common/value_collection.c src/common/value_live.c \
               src/common/flat_str.c
PLUGIN_TRIG_SRCS = src/lib/plugin_math_trig_entry.c src/lib/mod_math_trig.c $(PLUGIN_COMMON_SRCS)
PLUGIN_STATS_SRCS = src/lib/plugin_math_stats_entry.c src/lib/mod_math_stats.c $(PLUGIN_COMMON_SRCS)
PLUGIN_MATH_SRCS = src/lib/plugin_math_entry.c src/lib/mod_math.c $(PLUGIN_COMMON_SRCS)
PLUGIN_LIST_SRCS = src/lib/plugin_list_entry.c src/lib/mod_list.c $(PLUGIN_COMMON_SRCS)
PLUGIN_OBJ_SRCS = src/lib/plugin_obj_entry.c src/lib/mod_obj.c $(PLUGIN_COMMON_SRCS)
endif
NATIVE_VM_SRC = src/lib/native_registry.c src/lib/native_util.c src/lib/native_dispatch.c \
                src/lib/mod_io.c src/lib/mod_str.c src/lib/mod_json.c src/lib/mod_mem.c \
                $(NATIVE_VM_MATH) $(NATIVE_VM_PLUGIN)
NATIVE_FLC_SRC = src/lib/native_registry.c src/lib/native_util.c
VM_SRC  = src/common/vm.c src/common/value_pool.c src/common/value_collection.c \
          src/common/value_live.c src/common/value_ops.c src/common/value_parse.c \
          src/common/value_format.c src/common/value_io.c src/common/value_json.c $(STR_SRC) \
          $(NATIVE_VM_SRC)
FGIR_SRC = src/common/parse.c src/common/link.c src/common/compile.c src/common/native_use.c \
           $(STR_SRC) $(NATIVE_FLC_SRC)
FLATL_SRC = src/flatl/main.c src/common/parse.c src/common/link.c src/common/compile.c \
            src/common/native_use.c $(FB_SRC) $(VM_SRC) $(PORT_SRC)

MIN_OK_FB = test/cases/flvm/001_min_ok/min_ok.fb examples/min_ok.fb

# Default: compile only (does not run tests)
.DEFAULT_GOAL := all

.PHONY: all help clean test test-flatl fixtures tools flvm flc flatl mkfb run_tests gen_doc_site docs io_generate_tests json_generate_tests rebuild

help:
	@echo "flatL Makefile — run from repo root"
	@echo ""
	@echo "Build (compile only, no tests executed):"
	@echo "  make              same as make all"
	@echo "  make all          bin/flc, bin/flvm, bin/flatl, bin/run_tests + fixtures"
	@echo "  make tools        bin/flc, bin/flvm, bin/flatl, bin/mkfb only"
	@echo "  make flvm         rebuild VM only"
	@echo "  make flc          rebuild compiler only"
	@echo "  make flatl        rebuild driver (compile/run/crun) only"
	@echo "  make mkfb         rebuild fixture helper only"
	@echo "  make run_tests    rebuild test runner only"
	@echo "  make io_generate_tests   rebuild bin/io_generate_tests and regenerate test/cases/io/*"
	@echo "  make json_generate_tests rebuild bin/json_generate_tests and regenerate test/cases/json/*"
	@echo "  make fixtures     regenerate min_ok.fb examples"
	@echo "  make docs         bin/gen_doc_site → doc_site/ (offline HTML docs)"
	@echo "  make rebuild      make clean && make all (force full recompile)"
	@echo ""
	@echo "Note: if sources unchanged, make skips compile (up to date)."
	@echo "      Targets still print 'OK: …' so you know nothing failed."
	@echo ""
	@echo "Test:"
	@echo "  make test         make all, then ./bin/run_tests (flc + flvm pipelines)"
	@echo "  make test-flatl   make all, then ./bin/run_tests --flatl"
	@echo "  bin/run_tests -h  filters, -v, -e, -w, --flatl (see docs/TOOLCHAIN.md)"
	@echo ""
	@echo "Clean:"
	@echo "  make clean        remove bin/ and generated min_ok.fb"
	@echo ""
	@echo "Docs: docs/TOOLCHAIN.md  docs/LANGUAGE_TYPES.md"

# Full toolchain binaries + bytecode fixtures (still does not run tests)
all: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/run_tests$(EXEEXT) $(BINDIR)/port_check$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT) $(BINDIR)/gen_doc_site$(EXEEXT) $(PLUGIN_TARGET) fixtures
	@echo "OK: bin/flc bin/flvm bin/flatl bin/mkfb bin/gen_doc_site bin/run_tests bin/port_check (+ min_ok.fb fixtures)"

tools: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT) $(PLUGIN_TARGET)
	@echo "OK: bin/flc bin/flvm bin/flatl bin/mkfb"

flvm: $(BINDIR)/flvm$(EXEEXT)
	@echo "OK: bin/flvm"

flc: $(BINDIR)/flc$(EXEEXT)
	@echo "OK: bin/flc"

flatl: $(BINDIR)/flatl$(EXEEXT)
	@echo "OK: bin/flatl"

mkfb: $(BINDIR)/mkfb$(EXEEXT)
	@echo "OK: bin/mkfb"

run_tests: $(BINDIR)/run_tests$(EXEEXT)
	@echo "OK: bin/run_tests"

io_generate_tests: $(BINDIR)/io_generate_tests$(EXEEXT)
	$(BINDIR)/io_generate_tests$(EXEEXT)

json_generate_tests: $(BINDIR)/json_generate_tests$(EXEEXT)
	find test/cases/json -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
	$(BINDIR)/json_generate_tests$(EXEEXT)

rebuild: clean all

fixtures: $(MIN_OK_FB)
	@echo "OK: $(MIN_OK_FB)"

$(BINDIR)/flvm$(EXEEXT): src/flvm/main.c $(FB_SRC) $(VM_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flvm/main.c $(FB_SRC) $(VM_SRC) $(LDFLAGS) $(VM_EXTRA_LIBS) -lm

$(BINDIR)/libflat_math_trig.so: $(PLUGIN_TRIG_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_TRIG_SRCS) $(LDFLAGS) -lm

$(BINDIR)/libflat_math_stats.so: $(PLUGIN_STATS_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_STATS_SRCS) $(LDFLAGS) -lm

$(BINDIR)/libflat_math.so: $(PLUGIN_MATH_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_MATH_SRCS) $(LDFLAGS) -lm

$(BINDIR)/libflat_list.so: $(PLUGIN_LIST_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_LIST_SRCS) $(LDFLAGS)

$(BINDIR)/libflat_obj.so: $(PLUGIN_OBJ_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(PLUGIN_OBJ_SRCS) $(LDFLAGS)

$(BINDIR)/flc$(EXEEXT): src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(PORT_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(PORT_SRC) $(LDFLAGS) $(FLC_LDFLAGS)

$(BINDIR)/flatl$(EXEEXT): $(FLATL_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(FLATL_SRC) $(LDFLAGS) $(FLC_LDFLAGS) $(VM_EXTRA_LIBS) -lm

$(BINDIR)/mkfb$(EXEEXT): src/mkfb/main.c $(FB_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/mkfb/main.c $(FB_SRC) $(LDFLAGS)

$(BINDIR)/gen_doc_site$(EXEEXT): $(GEN_DOC_SITE_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(GEN_DOC_SITE_SRCS) $(LDFLAGS)

docs gen_doc_site: $(BINDIR)/gen_doc_site$(EXEEXT)
	$(BINDIR)/gen_doc_site$(EXEEXT)
	@echo "OK: doc_site/"

RUN_TESTS_SRCS = \
	src/run_tests/main.c \
	src/run_tests/discover.c \
	src/run_tests/case.c \
	src/common/repo.c \
	$(PORT_LIB)

$(BINDIR)/run_tests$(EXEEXT): $(RUN_TESTS_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(RUN_TESTS_SRCS) $(LDFLAGS)

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
	./$(BINDIR)/run_tests$(EXEEXT)
	@echo "OK: make test finished"

test-flatl: all
	./$(BINDIR)/run_tests$(EXEEXT) --flatl
	@echo "OK: make test-flatl finished"

clean:
	rm -rf $(BINDIR)
	rm -f $(MIN_OK_FB)
