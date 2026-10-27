VERSION := 0.2.0

# CFLAGS and LDFLAGS may be replaced (rpmbuild sets its own); the language
# and warning flags are always used, and WERROR= turns off -Werror
CC      ?= gcc
CFLAGS  ?= -O2 -g
WERROR  ?= -Werror
ALL_CFLAGS = -std=c11 -Wall -Wextra $(WERROR) $(CPPFLAGS) $(CFLAGS) -DPLM09_VERSION='"$(VERSION)"'
BUILD   := build

PLMC_SRCS := $(wildcard src/*.c)
SIM_SRCS  := $(wildcard sim/*.c)
PLMC_OBJS := $(PLMC_SRCS:src/%.c=$(BUILD)/src/%.o)
SIM_OBJS  := $(SIM_SRCS:sim/%.c=$(BUILD)/sim/%.o)

PLMC := $(BUILD)/plmc
SIM  := $(BUILD)/sim6809
S2F  := $(BUILD)/srec2flex

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
DATADIR ?= $(PREFIX)/share
RUNTIME_DIRS := $(sort $(dir $(wildcard runtime/*/plm_hooks.asm)))
MANPAGES := docs/man/plmc.1 docs/man/sim6809.1 docs/man/srec2flex.1

N ?= 200
OS9N ?= 25
FLEXN ?= 25

.PHONY: all install rpm srpm docs test man-test sim-test lex-test parse-test err-test run-test coco-test xroar-test os9-test nitros9-test flex-test dragonflex-test simon6809-test kswichit-test searle6809-test fuzz-test run clean

all: $(PLMC) $(SIM) $(S2F)

$(PLMC): $(PLMC_OBJS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^

$(SIM): $(SIM_OBJS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^

$(S2F): tools/srec2flex.c | $(BUILD)/src
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD)/src/%.o: src/%.c | $(BUILD)/src
	$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/sim/%.o: sim/%.c | $(BUILD)/sim
	$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/src $(BUILD)/sim:
	mkdir -p $@

-include $(PLMC_OBJS:.o=.d) $(SIM_OBJS:.o=.d)

# the programs that print VERSION (--version) are rebuilt when it changes
$(BUILD)/src/main.o $(BUILD)/sim/main.o $(S2F): Makefile

# install plmc, sim6809 and srec2flex in BINDIR (default $(PREFIX)/bin, PREFIX
# default /usr/local), their man pages in MANDIR/man1 (default
# $(PREFIX)/share/man) and the runtime in DATADIR/plm09/runtime (default
# $(PREFIX)/share); DESTDIR prefixes them all for staged installs
install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)/man1
	install -m 755 $(PLMC) $(SIM) $(S2F) $(DESTDIR)$(BINDIR)
	install -m 644 $(MANPAGES) $(DESTDIR)$(MANDIR)/man1
	install -d $(DESTDIR)$(DATADIR)/plm09/runtime
	install -m 644 runtime/*.asm $(DESTDIR)$(DATADIR)/plm09/runtime
	for d in $(RUNTIME_DIRS); do \
		install -d $(DESTDIR)$(DATADIR)/plm09/$$d && \
		install -m 644 $$d*.asm $(DESTDIR)$(DATADIR)/plm09/$$d || exit 1; \
	done

# Fedora RPMs from the committed tree (HEAD), in build/rpm
RPMTOP := $(abspath $(BUILD)/rpm)
TARBALL := $(RPMTOP)/SOURCES/plm09-$(VERSION).tar.gz

$(TARBALL): .FORCE
	mkdir -p $(RPMTOP)/SOURCES
	git archive --format=tar.gz --prefix=plm09-$(VERSION)/ -o $@ HEAD

# the patch plm09.spec applies: the documentation's runtime paths changed to
# the directory the spec installs it in (make install DATADIR=%{_datadir})
RPM_DATADIR := $(shell rpm --eval '%{_datadir}' 2>/dev/null)
RPMDOCS := README.md docs/plmc.md docs/sim6809.md docs/man/plmc.1 docs/man/srec2flex.1
DOCPATCH := $(RPMTOP)/SOURCES/plm09-$(VERSION)-datadir.patch

$(DOCPATCH): $(TARBALL)
	rm -rf $(RPMTOP)/docpatch && mkdir -p $(RPMTOP)/docpatch/a $(RPMTOP)/docpatch/b
	tar -xzf $(TARBALL) -C $(RPMTOP)/docpatch/a --strip-components=1
	tar -xzf $(TARBALL) -C $(RPMTOP)/docpatch/b --strip-components=1
	cd $(RPMTOP)/docpatch/b && python3 -I tools/rpmdocs.py $(RPM_DATADIR) $(RPMDOCS)
	cd $(RPMTOP)/docpatch && { diff -ruN a b > $@; [ $$? -eq 1 ]; }
	rm -rf $(RPMTOP)/docpatch

rpm: $(TARBALL) $(DOCPATCH)
	rm -rf $(RPMTOP)/RPMS $(RPMTOP)/SRPMS
	rpmbuild -ba --define "_topdir $(RPMTOP)" plm09.spec

srpm: $(TARBALL) $(DOCPATCH)
	rpmbuild -bs --define "_topdir $(RPMTOP)" plm09.spec

.FORCE:

test: sim-test man-test lex-test parse-test err-test run-test coco-test xroar-test os9-test nitros9-test flex-test dragonflex-test simon6809-test kswichit-test searle6809-test fuzz-test

sim-test man-test lex-test parse-test err-test run-test coco-test xroar-test: all
	tests/run_tests.sh $(@:-test=)

fuzz-test: all
	tests/run_tests.sh fuzz $(N)

os9-test: all
	tests/run_tests.sh os9 $(OS9N)

nitros9-test: all
	tests/run_tests.sh nitros9

flex-test: all
	tests/run_tests.sh flex $(FLEXN)

dragonflex-test: all
	tests/run_tests.sh dragonflex

simon6809-test kswichit-test searle6809-test: all
	tests/run_tests.sh $(@:-test=)

run: all
	@test -n "$(F)" || { echo "usage: make run F=file.plm" >&2; exit 2; }
	tests/run_tests.sh one $(F)

# PDF manuals.  docs/tex supplies a stand-in for footnotehyper.sty, which
# pandoc's LaTeX template needs but this TeX installation lacks.
DOCS := docs/sim6809.pdf docs/plmc.pdf

docs: $(DOCS)

docs/%.pdf: docs/%.md docs/tex/footnotehyper.sty
	cd docs && TEXINPUTS=./tex//: pandoc $*.md -o $*.pdf --pdf-engine=xelatex

clean:
	rm -rf $(BUILD)
