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
|:---|---:|---:|---:|---:|:--|
| `./bf programs/mandelbrot.bf` | 1.987 ± 0.013 | 1.978 | 2.014 | 2.15 ± 0.04 | |
| `./jit programs/mandelbrot.bf` | 1.023 ± 0.015 | 1.007 | 1.057 | 1.11 ± 0.03 | |
| `./aot -e programs/mandelbrot.bf` | 2.739 ± 0.039 | 2.674 | 2.791 | 2.96 ± 0.07 | Jit interpreted |
| `./mandelbrot` | 0.924 ± 0.016 | 0.904 | 0.955 | 1.00 | AOT compiled |
