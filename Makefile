# A tool is src/NAME.c including termbox2.h, then prelude.c. Add NAME to TOOLS.

TOOLS   =

CC      ?= cc
WARNS   = -std=c17 -Wall -Wextra -Wconversion -Wdouble-promotion \
          -Wno-sign-conversion -Wno-unused-parameter -Wno-unused-function
CFLAGS  ?= $(WARNS) -O2
UBSAN   = -g3 -fsanitize=undefined -fsanitize-trap
ITERS   = 200000

TB        = vendor/termbox2.h
TB_OPTS   = -DTB_OPT_ATTR_W=32
TB_CFLAGS = -std=c17 -O2 $(TB_OPTS) -DTB_IMPL -x c -c
DEPS      = src/prelude.c $(TB)

# iSH: plain i686, static musl.
CROSS_CC = zig cc -target x86-linux-musl -mcpu=i686
DEST    ?= $(HOME)/Library/Mobile Documents/com~apple~CloudDocs/logs/bin

B  = build
BX = $(B)/i686

all: $(TOOLS:%=$(B)/%)
debug: $(TOOLS:%=$(B)/debug/%)
cross: $(TOOLS:%=$(BX)/%)

# make 3.81 takes the first matching pattern: specific directories first.
$(B)/debug/%: src/%.c $(DEPS) $(B)/termbox2.o
	@mkdir -p $(@D)
	$(CC) $(WARNS) $(UBSAN) $(TB_OPTS) -o $@ $< $(B)/termbox2.o

$(BX)/%: src/%.c $(DEPS) $(BX)/termbox2.o
	@mkdir -p $(@D)
	$(CROSS_CC) $(WARNS) -O2 $(TB_OPTS) -static -s -o $@ $< $(BX)/termbox2.o

$(B)/%: src/%.c $(DEPS) $(B)/termbox2.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TB_OPTS) $(LDFLAGS) -o $@ $< $(B)/termbox2.o

$(B)/termbox2.o: $(TB)
	@mkdir -p $(@D)
	$(CC) $(TB_CFLAGS) -o $@ $(TB)

$(BX)/termbox2.o: $(TB)
	@mkdir -p $(@D)
	$(CROSS_CC) $(TB_CFLAGS) -o $@ $(TB)

$(B)/prelude-test: src/prelude.c
	@mkdir -p $(@D)
	$(CC) $(WARNS) $(UBSAN) -DPRELUDE_TEST -o $@ src/prelude.c

test: $(B)/prelude-test
	./$(B)/prelude-test $(ITERS)

deploy: cross
	for t in $(TOOLS); do cp $(BX)/$$t "$(DEST)/"; done

clean:
	rm -rf $(B)

.PHONY: all debug test cross deploy clean
