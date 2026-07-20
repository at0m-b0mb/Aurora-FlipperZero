# Compiles the real scale engine for the host and asserts the bin mapping, the
# zoom clamp, the noise-floor percentile and the dither ramp. These are the
# parts a screenshot cannot vouch for - a bin that maps to the wrong frequency
# looks perfectly fine on screen - so they get verified on every push.
#
#   make -C test

CC      ?= cc
CFLAGS  := -std=gnu2x -Wall -Wextra -Werror -O1 -g
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  += -I ..

SRC := host_scale_test.c ../helpers/aur_scale.c
BIN := host_scale_test

.PHONY: all run clean
all: run

$(BIN): $(SRC) ../helpers/aur_scale.h
	$(CC) $(CFLAGS) $(SRC) -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BIN) $(BIN).dSYM
