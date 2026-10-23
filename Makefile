CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2 -g
BUILD   := build

PLMC_SRCS := $(wildcard src/*.c)
SIM_SRCS  := $(wildcard sim/*.c)
PLMC_OBJS := $(PLMC_SRCS:src/%.c=$(BUILD)/src/%.o)
SIM_OBJS  := $(SIM_SRCS:sim/%.c=$(BUILD)/sim/%.o)

PLMC := $(BUILD)/plmc
SIM  := $(BUILD)/sim6809

N ?= 200

.PHONY: all test sim-test lex-test parse-test err-test run-test coco-test xroar-test fuzz-test run clean

all: $(PLMC) $(SIM)

$(PLMC): $(PLMC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(SIM): $(SIM_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/src/%.o: src/%.c | $(BUILD)/src
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/sim/%.o: sim/%.c | $(BUILD)/sim
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/src $(BUILD)/sim:
	mkdir -p $@

-include $(PLMC_OBJS:.o=.d) $(SIM_OBJS:.o=.d)

test: sim-test lex-test parse-test err-test run-test coco-test xroar-test fuzz-test

sim-test lex-test parse-test err-test run-test coco-test xroar-test: all
	tests/run_tests.sh $(@:-test=)

fuzz-test: all
	tests/run_tests.sh fuzz $(N)

run: all
	@test -n "$(F)" || { echo "usage: make run F=file.plm" >&2; exit 2; }
	tests/run_tests.sh one $(F)

clean:
	rm -rf $(BUILD)
