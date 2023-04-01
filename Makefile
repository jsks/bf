CFLAGS += -Wall -Wextra -Wno-implicit-fallthrough -pedantic -O2 -pipe

all: bf
.PHONY: clean debug fmt

clean:
	rm -f bf

debug: CFLAGS += -DDEBUG -O0 -g -fsanitize=address
debug: clean bf

fmt:
	clang-format -i --Werror --style=file bf.c
