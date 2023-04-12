Simple Brainfuck Interpreter and JIT Compiler
---

```sh
$ make
$ ./bf <(./bf --version)
$ ./jit <(./jit --version)
```

Two programs are included in this repository: `bf.c` is a simple
brainfuck interpreter implementing some fairly trivial optimizations
and `jit.c` is a JIT compiler using `GNU libjit`. Run `./bf --help` or
`./jit --help` to get started.

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

```sh
$ hyperfine --warmup 2 './jit mandelbrot.bf' './bf mandelbrot.bf'
Benchmark 1: ./jit mandelbrot.bf
  Time (mean ± σ):     825.4 ms ±   1.2 ms    [User: 823.9 ms, System: 1.5 ms]
  Range (min … max):   824.1 ms … 827.2 ms    10 runs

Benchmark 2: ./bf mandelbrot.bf
  Time (mean ± σ):      2.143 s ±  0.002 s    [User: 2.142 s, System: 0.000 s]
  Range (min … max):    2.140 s …  2.147 s    10 runs

Summary
  './jit mandelbrot.bf' ran
    2.60 ± 0.00 times faster than './bf mandelbrot.bf'
```
