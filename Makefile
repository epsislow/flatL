CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -Iinclude
LDFLAGS ?=

BINDIR  = bin
FB_SRC  = src/common/fb_format.c
NATIVE_VM_SRC = src/lib/native_registry.c src/lib/native_util.c src/lib/native_dispatch.c \
                src/lib/mod_io.c src/lib/mod_str.c src/lib/mod_json.c
NATIVE_FLC_SRC = src/lib/native_registry.c src/lib/native_util.c
VM_SRC  = src/common/vm.c src/common/value_pool.c src/common/value_collection.c \
          src/common/value_ops.c src/common/value_parse.c src/common/value_format.c \
          src/common/value_io.c src/common/value_json.c $(NATIVE_VM_SRC)
FGIR_SRC = src/common/parse.c src/common/compile.c $(NATIVE_FLC_SRC)

MIN_OK_FB = test/cases/flvm/001_min_ok/min_ok.fb examples/min_ok.fb

# Default: compile only (does not run tests)
.DEFAULT_GOAL := all

.PHONY: all help clean test fixtures tools flvm flc mkfb run_tests io_generate_tests json_generate_tests rebuild

help:
	@echo "flatL Makefile — run from repo root"
	@echo ""
	@echo "Build (compile only, no tests executed):"
	@echo "  make              same as make all"
	@echo "  make all          bin/flc, bin/flvm, bin/mkfb, bin/run_tests + .fb fixtures"
	@echo "  make tools        bin/flc, bin/flvm, bin/mkfb only (no run_tests, no fixtures)"
	@echo "  make flvm         rebuild VM only"
	@echo "  make flc          rebuild compiler only"
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
	@echo "  bin/run_tests -h  filters, -v, -e (see docs/TOOLCHAIN.md)"
	@echo ""
	@echo "Clean:"
	@echo "  make clean        remove bin/ and generated min_ok.fb"
	@echo ""
	@echo "Docs: docs/TOOLCHAIN.md  docs/LANGUAGE_TYPES.md"

# Full toolchain binaries + bytecode fixtures (still does not run tests)
all: $(BINDIR)/flvm $(BINDIR)/flc $(BINDIR)/run_tests $(BINDIR)/mkfb fixtures
	@echo "OK: bin/flc bin/flvm bin/mkfb bin/run_tests (+ min_ok.fb fixtures)"

tools: $(BINDIR)/flvm $(BINDIR)/flc $(BINDIR)/mkfb
	@echo "OK: bin/flc bin/flvm bin/mkfb"

flvm: $(BINDIR)/flvm
	@echo "OK: bin/flvm"

flc: $(BINDIR)/flc
	@echo "OK: bin/flc"

mkfb: $(BINDIR)/mkfb
	@echo "OK: bin/mkfb"

run_tests: $(BINDIR)/run_tests
	@echo "OK: bin/run_tests"

io_generate_tests: $(BINDIR)/io_generate_tests
	$(BINDIR)/io_generate_tests

json_generate_tests: $(BINDIR)/json_generate_tests
	find test/cases/json -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
	$(BINDIR)/json_generate_tests

rebuild: clean all

fixtures: $(MIN_OK_FB)
	@echo "OK: $(MIN_OK_FB)"

$(BINDIR)/flvm: src/flvm/main.c $(FB_SRC) $(VM_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flvm/main.c $(FB_SRC) $(VM_SRC) $(LDFLAGS)

$(BINDIR)/flc: src/flc/main.c $(FGIR_SRC) $(FB_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flc/main.c $(FGIR_SRC) $(FB_SRC) $(LDFLAGS)

$(BINDIR)/mkfb: src/mkfb/main.c $(FB_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/mkfb/main.c $(FB_SRC) $(LDFLAGS)

RUN_TESTS_SRCS = \
	src/run_tests/main.c \
	src/run_tests/discover.c \
	src/run_tests/case.c \
	src/common/repo.c

$(BINDIR)/run_tests: $(RUN_TESTS_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(RUN_TESTS_SRCS) $(LDFLAGS)

$(BINDIR)/io_generate_tests: src/io_generate_tests/main.c src/common/repo.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/io_generate_tests/main.c src/common/repo.c $(LDFLAGS)

$(BINDIR)/json_generate_tests: src/json_generate_tests/main.c src/common/repo.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/json_generate_tests/main.c src/common/repo.c $(LDFLAGS)

$(BINDIR):
	mkdir -p $(BINDIR)

test/cases/flvm/001_min_ok/min_ok.fb: $(BINDIR)/mkfb
	$(BINDIR)/mkfb --min-ok -o $@

examples/min_ok.fb: $(BINDIR)/mkfb
	$(BINDIR)/mkfb --min-ok -o $@

test: all
	./$(BINDIR)/run_tests
	@echo "OK: make test finished"

clean:
	rm -rf $(BINDIR)
	rm -f $(MIN_OK_FB)
