.PHONY: all clean

all: hello sum

TARGET ?= sim
OBJ_FORMAT ?= srec

#
# Supported TARGET values are "os9" and "sim" (the default)
#
ifeq ($(TARGET), os9)
OBJ_FORMAT = os9
PLMC_ARGS = --os9
SIM6809_ARGS = --os9
else ifneq ($(TARGET), sim)
$(error unknown TARGET: $(TARGET))
endif

PLM09_SHARE = /usr/share/plm09
PLMC ?= plmc
SIM6809 ?= sim6809
LWASM ?= lwasm -9

%.asm: %.plm
	$(PLMC) $(PLMC_ARGS) -o $@ $<

%.s19: %.asm
	$(LWASM) -f $(OBJ_FORMAT) -I $(PLM09_SHARE)/runtime -I $(PLM09_SHARE)/runtime/$(TARGET) -o $@ $<

%: %.s19
	$(SIM6809) $(SIM6809_ARGS) $<

clean:
	rm -f *.s19 *.asm
