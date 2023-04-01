Simple Brainfuck Interpreter
---

```sh
$ make
$ ./bf --version
$ ./bf <(./bf --version)
```

To enable strict overflow/underflow and bounds checking, compile with
`-D_BF_STRICT_CHECKS`. Note, some programs like `mandelbrot.bf` will
no longer work.

```sh
$ CFLAGS=-D_BF_STRICT_CHECKS make
```
