TARGET = XINFO
COMPILER6502 = cl65
CCFLAGS = -t cx16 -O -Oi -Or -Os -I ./src/include
ASSEMBLER6502 = ca65
ASFLAGS = -t cx16
LINKER6502 = ld65
LDFLAGS = -t cx16

PROG = $(TARGET).PRG
ZIPFILE = $(TARGET).zip
SOURCEDIR = src/source
NAME = main_xinfo
MAIN = $(SOURCEDIR)/$(NAME).c
SOURCES = $(MAIN)

all: $(PROG)


$(PROG): $(MAIN)
	$(COMPILER6502) $(CCFLAGS) -o $(PROG) $(MAIN)
	mv $(PROG) bin/START/

run: all
	(cd bin/START/; x16emu -prg $(PROG) -scale 2 -debug)

$(ZIPFILE): all
	(cd bin/START/; zip ../../$(ZIPFILE) *)

zip: $(ZIPFILE)

clean:
	rm -f bin/START/$(PROG) \
	rm -f $(ZIPFILE)
	

