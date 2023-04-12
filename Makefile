CFLAGS += -Wall -Wextra -O2 -march=native -pipe

all: bf jit
.PHONY: clean debug fmt

clean:
	rm -f bf jit

debug: CFLAGS += -DDEBUG -O0 -g -fsanitize=address
debug: clean bf jit

fmt:
	clang-format -i --Werror --style=file bf.c jit.c

jit: LDFLAGS += -ljit
jit: jit.c
