CFLAGS += -Wall -Wextra -O3 -march=native -pipe

all: aot bf jit
.PHONY: clean debug fmt

clean:
	rm -f aot bf jit

debug: CFLAGS += -DDEBUG -O0 -g3 -fsanitize=address
debug: clean aot bf jit

fmt:
	clang-format -i --Werror --style=file aot.c bf.c jit.c

aot: LDFLAGS += -lgccjit
jit: LDFLAGS += -ljit
