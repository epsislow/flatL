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
NATIVE_VM_SRC = src/lib/native_registry.c src/lib/native_util.c src/lib/native_dispatch.c \
                src/lib/mod_io.c src/lib/mod_str.c src/lib/mod_json.c
NATIVE_FLC_SRC = src/lib/native_registry.c src/lib/native_util.c
VM_SRC  = src/common/vm.c src/common/value_pool.c src/common/value_collection.c \
          src/common/value_ops.c src/common/value_parse.c src/common/value_format.c \
          src/common/value_io.c src/common/value_json.c $(STR_SRC) $(NATIVE_VM_SRC)
FGIR_SRC = src/common/parse.c src/common/compile.c $(STR_SRC) $(NATIVE_FLC_SRC)
FLATL_SRC = src/flatl/main.c src/common/parse.c src/common/compile.c $(FB_SRC) $(VM_SRC) $(PORT_SRC)

MIN_OK_FB = test/cases/flvm/001_min_ok/min_ok.fb examples/min_ok.fb

# Default: compile only (does not run tests)
.DEFAULT_GOAL := all

.PHONY: all help clean test fixtures tools flvm flc flatl mkfb run_tests io_generate_tests json_generate_tests rebuild

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
	@echo "  make rebuild      make clean && make all (force full recompile)"
	@echo ""
	@echo "Note: if sources unchanged, make skips compile (up to date)."
	@echo "      Targets still print 'OK: …' so you know nothing failed."
	@echo ""
	@echo "Test:"
	@echo "  make test         make all, then ./bin/run_tests"
	@echo "  bin/run_tests -h  filters, -v, -e, -w (see docs/TOOLCHAIN.md)"
	@echo ""
	@echo "Clean:"
	@echo "  make clean        remove bin/ and generated min_ok.fb"
	@echo ""
	@echo "Docs: docs/TOOLCHAIN.md  docs/LANGUAGE_TYPES.md"

# Full toolchain binaries + bytecode fixtures (still does not run tests)
all: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/run_tests$(EXEEXT) $(BINDIR)/port_check$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT) fixtures
	@echo "OK: bin/flc bin/flvm bin/flatl bin/mkfb bin/run_tests bin/port_check (+ min_ok.fb fixtures)"

tools: $(BINDIR)/flvm$(EXEEXT) $(BINDIR)/flc$(EXEEXT) $(BINDIR)/flatl$(EXEEXT) $(BINDIR)/mkfb$(EXEEXT)
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
	$(CC) $(CFLAGS) -o $@ src/flvm/main.c $(FB_SRC) $(VM_SRC) $(LDFLAGS)

$(BINDIR)/flc$(EXEEXT): src/flc/main.c $(FGIR_SRC) $(FB_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(LDFLAGS) $(FLC_LDFLAGS)

$(BINDIR)/flatl$(EXEEXT): $(FLATL_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(FLATL_SRC) $(LDFLAGS) $(FLC_LDFLAGS)

$(BINDIR)/mkfb$(EXEEXT): src/mkfb/main.c $(FB_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/mkfb/main.c $(FB_SRC) $(LDFLAGS)

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

clean:
	rm -rf $(BINDIR)
	rm -f $(MIN_OK_FB)
