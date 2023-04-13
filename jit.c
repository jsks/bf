/*
 * Copyright (c) 2023, Joshua Krusell
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <jit/jit.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define READ_SIZE 1024 * 8
#define MAX_FILE_SIZE 1024 * 1024
#define TAPE_SIZE 30000
#define STACK_SIZE 256

#define OP_ARG(fn, x) jit_value_create_nint_constant(fn, jit_type_ubyte, 1 + x)

#define IS_EMPTY_STACK(stack) (stack.len == 0)
#define ADD_JMP(stack)                                                         \
  do {                                                                         \
    if (stack.len == STACK_SIZE)                                               \
      errx(EXIT_FAILURE, "Nested loops exceeded stack size");                  \
    stack.data[stack.len++] = (jmp_pair){ .start = jit_label_undefined,        \
                                          .end = jit_label_undefined };        \
  } while (0)
#define POP_JMP(stack) (stack.len--)
#define LAST_FWD(stack) (stack.data[stack.len - 1].start)
#define LAST_BCK(stack) (stack.data[stack.len - 1].end)

typedef struct {
  jit_label_t start, end;
} jmp_pair;

typedef struct {
  jmp_pair data[STACK_SIZE];
  size_t len;
} lifo;

typedef void (*BF_program)(void *);

static const char *progname;

static struct option longopts[] = {
  {"help",     no_argument, NULL, 'h'},
  { "print",   no_argument, NULL, 'p'},
  { "version", no_argument, NULL, 'v'},
  { NULL,      no_argument, NULL, 0  }
};

void version(void) {
  printf("-[----->+<]>---.--.++.--.+++.----.---[-->+++++<]>+.-.++++"
         "+++++++.>++++++++++.\n");
}

void usage(FILE *stream) {
  fprintf(stream, "Usage: %s [option] [infile]\n", progname);
}

void help(void) {
  usage(stdout);
  printf("\n");
  printf("A simple brainfuck JIT compiler.\n\n"
         "Options:\n"
         "  -h, --help\t\t Useless help message\n"
         "  -p, --print\t\t Print libjit instructions\n"
         "  -v, --version\t\t Print version number\n");
}

bool is_valid_token(char ch) {
  return ch == '+' || ch == '-' || ch == '>' || ch == '<' || ch == '.' ||
         ch == ',' || ch == '[' || ch == ']';
}

bool is_repeatable_token(char ch) {
  return ch == '>' || ch == '<' || ch == '+' || ch == '-';
}

char *peek(char *s) {
  int ch;
  while ((ch = *(++s))) {
    if (!is_valid_token(ch))
      continue;

    return s;
  }

  return NULL;
}

void compile_bf(jit_function_t fn, char *s) {
  jit_type_t putchar_params[1] = { jit_type_int };
  jit_type_t putchar_sig = jit_type_create_signature(
      jit_abi_cdecl, jit_type_int, putchar_params, 1, 1);

  jit_type_t getchar_sig =
      jit_type_create_signature(jit_abi_cdecl, jit_type_int, NULL, 0, 1);

  jit_value_t zero = jit_value_create_nint_constant(fn, jit_type_ubyte, 0);
  jit_value_t tape = jit_value_get_param(fn, 0);
  jit_value_t cell, result;

  lifo jmp_stack = { 0 };

  char *next_token;
  int repeated = 0;
  int ch;
  while ((ch = *s++)) {
    if (!is_valid_token(ch))
      continue;

    if (ch == *s && is_repeatable_token(ch)) {
      repeated++;
      continue;
    }

    switch (ch) {
      case '>':
        result = jit_insn_add(fn, tape, OP_ARG(fn, repeated));
        jit_insn_store(fn, tape, result);
        break;
      case '<':
        result = jit_insn_sub(fn, tape, OP_ARG(fn, repeated));
        jit_insn_store(fn, tape, result);
        break;
      case '+':
        cell = jit_insn_load_relative(fn, tape, 0, jit_type_ubyte);
        result = jit_insn_add(fn, cell, OP_ARG(fn, repeated));

        // Note: addition coerces ubyte into int
        result = jit_insn_convert(fn, result, jit_type_ubyte, 0);
        jit_insn_store_relative(fn, tape, 0, result);
        break;
      case '-':
        cell = jit_insn_load_relative(fn, tape, 0, jit_type_ubyte);
        result = jit_insn_sub(fn, cell, OP_ARG(fn, repeated));
        result = jit_insn_convert(fn, result, jit_type_ubyte, 0);
        jit_insn_store_relative(fn, tape, 0, result);
        break;
      case '.':
        cell = jit_insn_load_relative(fn, tape, 0, jit_type_ubyte);
        jit_insn_call_native(fn, "putchar", putchar, putchar_sig, &cell, 1,
                             JIT_CALL_NOTHROW);
        break;
      case ',':
        result = jit_insn_call_native(fn, "getchar", getchar, getchar_sig, NULL,
                                      0, JIT_CALL_NOTHROW);
        jit_insn_store_relative(fn, tape, 0, result);
        break;
      case '[':
        if (*s == '-' && (next_token = peek(s)) && *next_token == ']') {
          jit_insn_store_relative(fn, tape, 0, zero);
          s = next_token + 1;
        } else {
          ADD_JMP(jmp_stack);
          jit_insn_label(fn, &LAST_FWD(jmp_stack));
          cell = jit_insn_load_relative(fn, tape, 0, jit_type_ubyte);
          jit_insn_branch_if_not(fn, cell, &LAST_BCK(jmp_stack));
        }
        break;
      case ']':
        if (IS_EMPTY_STACK(jmp_stack))
          errx(EXIT_FAILURE, "Missing opening '['");

        cell = jit_insn_load_relative(fn, tape, 0, jit_type_ubyte);
        jit_insn_branch_if(fn, cell, &LAST_FWD(jmp_stack));
        jit_insn_label(fn, &LAST_BCK(jmp_stack));

        POP_JMP(jmp_stack);
        break;
      default:
        break;
    }

    repeated = 0;
  }

  if (!IS_EMPTY_STACK(jmp_stack))
    errx(EXIT_FAILURE, "Missing closing ']'");

  jit_type_free(putchar_sig);
  jit_type_free(getchar_sig);

  jit_insn_return(fn, NULL);
}

void read_file(char *file, char *buffer) {
  int fd;
  if ((fd = open(file, O_RDONLY)) < 0)
    err(EXIT_FAILURE, NULL);

  size_t len = 0;
  ssize_t bytes_read = 0;
  while ((bytes_read = read(fd, buffer + len, READ_SIZE)) > 0) {
    len += bytes_read;

    if (len == MAX_FILE_SIZE - 1)
      errx(EXIT_FAILURE, "File %s exceeds read limits", file);
  }

  if (bytes_read < 0 || close(fd) < 0)
    err(EXIT_FAILURE, NULL);
}

int main(int argc, char *argv[]) {
  progname = basename(argv[0]);

  bool debug_instructions = false;
  int opt;
  if ((opt = getopt_long(argc, argv, "hpv", longopts, NULL)) != -1) {
    switch (opt) {
      case 'h':
        help();
        exit(EXIT_SUCCESS);
      case 'v':
        version();
        exit(EXIT_SUCCESS);
      case 'p':
        debug_instructions = true;
        break;
      default:
        usage(stderr);
        exit(EXIT_FAILURE);
    }
  }

  if (!(optind < argc)) {
    usage(stderr);
    errx(EXIT_FAILURE, "No input file");
  }

  char buffer[MAX_FILE_SIZE] = { 0 };
  read_file(argv[optind], buffer);

  jit_context_t ctx = jit_context_create();
  jit_context_build_start(ctx);

  jit_type_t params[1] = { jit_type_void_ptr };
  jit_type_t sig =
      jit_type_create_signature(jit_abi_cdecl, jit_type_void, params, 1, 1);
  jit_function_t program = jit_function_create(ctx, sig);

  compile_bf(program, buffer);
  jit_function_compile(program);

  jit_context_build_end(ctx);

  if (debug_instructions)
    jit_dump_function(stdout, program, "bf");

  uint8_t tape[TAPE_SIZE] = { 0 };
  BF_program fn = jit_function_to_closure(program);
  fn(tape);

#ifdef DEBUG
  jit_function_abandon(program);
  jit_type_free(sig);
  jit_context_destroy(ctx);
#endif

  return 0;
}
