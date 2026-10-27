CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -Iinclude
LDFLAGS ?=

BINDIR  = bin
FB_SRC  = src/common/fb_format.c

MIN_OK_FB = test/cases/flvm/001_min_ok/min_ok.fb examples/min_ok.fb

.PHONY: all clean test fixtures

all: $(BINDIR)/flvm $(BINDIR)/flc $(BINDIR)/run_tests $(BINDIR)/mkfb fixtures

$(BINDIR)/flvm: src/flvm/main.c $(FB_SRC) include/flat.h include/fb_format.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flvm/main.c $(FB_SRC) $(LDFLAGS)

$(BINDIR)/flc: src/flc/main.c include/flat.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flc/main.c $(LDFLAGS)

$(BINDIR)/mkfb: src/mkfb/main.c $(FB_SRC) include/fb_format.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/mkfb/main.c $(FB_SRC) $(LDFLAGS)

RUN_TESTS_SRCS = \
	src/run_tests/main.c \
	src/run_tests/discover.c \
	src/run_tests/case.c \
	src/common/repo.c

$(BINDIR)/run_tests: $(RUN_TESTS_SRCS) include/flat.h include/repo.h include/casefile.h include/discover.h | $(BINDIR)
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
