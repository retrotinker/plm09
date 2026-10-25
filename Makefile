CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2 -g
BUILD   := build

PLMC_SRCS := $(wildcard src/*.c)
SIM_SRCS  := $(wildcard sim/*.c)
PLMC_OBJS := $(PLMC_SRCS:src/%.c=$(BUILD)/src/%.o)
SIM_OBJS  := $(SIM_SRCS:sim/%.c=$(BUILD)/sim/%.o)

PLMC := $(BUILD)/plmc
SIM  := $(BUILD)/sim6809
S2F  := $(BUILD)/srec2flex

N ?= 200
OS9N ?= 25
FLEXN ?= 25

.PHONY: all docs test sim-test lex-test parse-test err-test run-test coco-test xroar-test os9-test nitros9-test flex-test dragonflex-test simon6809-test kswichit-test fuzz-test run clean

all: $(PLMC) $(SIM) $(S2F)

$(PLMC): $(PLMC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(SIM): $(SIM_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(S2F): tools/srec2flex.c | $(BUILD)/src
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/src/%.o: src/%.c | $(BUILD)/src
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/sim/%.o: sim/%.c | $(BUILD)/sim
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/src $(BUILD)/sim:
	mkdir -p $@

-include $(PLMC_OBJS:.o=.d) $(SIM_OBJS:.o=.d)

test: sim-test lex-test parse-test err-test run-test coco-test xroar-test os9-test nitros9-test flex-test dragonflex-test simon6809-test kswichit-test fuzz-test

sim-test lex-test parse-test err-test run-test coco-test xroar-test: all
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

simon6809-test kswichit-test: all
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
