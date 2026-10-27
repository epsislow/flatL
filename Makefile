CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -Iinclude
LDFLAGS ?=

BINDIR  = bin
FB_SRC  = src/common/fb_format.c
VM_SRC  = src/common/vm.c src/common/value_pool.c src/common/value_ops.c
FGIR_SRC = src/common/parse.c src/common/compile.c

MIN_OK_FB = test/cases/flvm/001_min_ok/min_ok.fb examples/min_ok.fb

.PHONY: all clean test fixtures

all: $(BINDIR)/flvm $(BINDIR)/flc $(BINDIR)/run_tests $(BINDIR)/mkfb fixtures

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

$(BINDIR):
	mkdir -p $(BINDIR)

fixtures: $(MIN_OK_FB)

test/cases/flvm/001_min_ok/min_ok.fb: $(BINDIR)/mkfb
	$(BINDIR)/mkfb --min-ok -o $@

examples/min_ok.fb: $(BINDIR)/mkfb
	$(BINDIR)/mkfb --min-ok -o $@

test: all
	./$(BINDIR)/run_tests

clean:
	rm -rf $(BINDIR)
	rm -f $(MIN_OK_FB)
