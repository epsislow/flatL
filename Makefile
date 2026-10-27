CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -Iinclude
LDFLAGS ?=

BINDIR  = bin

.PHONY: all clean test

all: $(BINDIR)/flvm $(BINDIR)/flc $(BINDIR)/run_tests

$(BINDIR)/flvm: src/flvm/main.c include/flat.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flvm/main.c $(LDFLAGS)

$(BINDIR)/flc: src/flc/main.c include/flat.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ src/flc/main.c $(LDFLAGS)

RUN_TESTS_SRCS = \
	src/run_tests/main.c \
	src/run_tests/discover.c \
	src/run_tests/case.c \
	src/common/repo.c

$(BINDIR)/run_tests: $(RUN_TESTS_SRCS) include/flat.h include/repo.h include/casefile.h include/discover.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(RUN_TESTS_SRCS) $(LDFLAGS)

$(BINDIR):
	mkdir -p $(BINDIR)

test: all
	./$(BINDIR)/run_tests

clean:
	rm -rf $(BINDIR)
