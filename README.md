Simple Brainfuck Interpreter and JIT Compiler
---

```sh
$ make
$ ./bf <(./bf --version)
$ ./jit <(./jit --version)
$ ./aot -e <(./aot --version)
```

Three programs are included in this repository:

1. `bf.c` is a simple brainfuck interpreter implementing some fairly
   trivial optimizations.

2. `jit.c` is a JIT compiler using `GNU libjit`.

3. `aot.c` is an ahead-of-time compiler / JIT interpreter using `libgccjit`.

Run `./<program> --help` to get started. Only tested on Linux amd64.

For some fun, we can run a [brainfuck
interpreter](https://esolangs.org/wiki/Dbfi) written in brainfuck that
executes itself, which in turn runs a brainfuck program that outputs
the string ["Hello World!"](https://sv.wikipedia.org/wiki/Brainfuck#Hello_World!).

```sh
$ ./jit dfbi.bf < <(sed -s '$a!' {dbfi,hello}.bf)
```

## Benchmarking

Using [hyperfine](https://github.com/sharkdp/hyperfine) and the
classic
[mandelbrot.bf](http://esoteric.sange.fi/brainfuck/utils/mandelbrot/):

| Command | Mean [s] | Min [s] | Max [s] | Relative | Note |
|:---|---:|---:|---:|---:|:---|
| `./bf programs/mandelbrot.bf` | 2.698 ± 0.003 | 2.694 | 2.704 | 3.25 ± 0.05 | |
| `./jit programs/mandelbrot.bf` | 0.983 ± 0.003 | 0.980 | 0.990 | 1.18 ± 0.02 | |
| `./aot -e programs/mandelbrot.bf` | 2.953 ± 0.016 | 2.932 | 2.987 | 3.55 ± 0.05 | JIT interpreted |
| `./mandelbrot` | 0.831 ± 0.012 | 0.814 | 0.848 | 1.00 | AOT compiled |
