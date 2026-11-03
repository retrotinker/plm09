# Two PL/M modules compiled with plmc --obj, assembled into object files
# with lwasm -f obj and linked with lwlink, using an installed plm09.
.PHONY: all demo clean

all: demo

PLM09_SHARE ?= /usr/share/plm09
RUNTIME = $(PLM09_SHARE)/runtime
PLMC ?= plmc
SIM6809 ?= sim6809
LWASM ?= lwasm -9
LWLINK ?= lwlink

%.asm: %.plm
	$(PLMC) --obj -o $@ $<

# only the main module uses the hooks, but the include path does no harm
%.o: %.asm
	$(LWASM) -f obj -I $(RUNTIME) -I $(RUNTIME)/sim -o $@ $<

demo.s19: main.o util.o
	$(LWLINK) -f srec -s $(RUNTIME)/plm09.link -o $@ $^

demo: demo.s19
	$(SIM6809) $<

clean:
	rm -f *.asm *.o *.s19
