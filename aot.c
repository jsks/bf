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
#include <libgccjit.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define READ_SIZE 1024 * 8
#define MAX_FILE_SIZE 1024 * 1024
#define TAPE_SIZE 30000
#define STACK_SIZE 256

#define IS_EMPTY_STACK(stack) (stack.len == 0)
#define PUSH_STACK(stack, start, end)                                          \
  do {                                                                         \
    if (stack.len == STACK_SIZE)                                               \
      errx(EXIT_FAILURE, "Nested loops exceeded stack size");                  \
    stack.data[stack.len++] = (jmp_pair){ start, end };                        \
  } while (0)
#define POP_STACK(stack) (stack.len--)
#define TOP_START(stack) (stack.data[stack.len - 1].start)
#define TOP_END(stack) (stack.data[stack.len - 1].end)

typedef struct {
  gcc_jit_block *start, *end;
} jmp_pair;

typedef struct {
  jmp_pair data[STACK_SIZE];
  size_t len;
} lifo;

typedef void (*BF_program)(uint8_t *);

static const char *progname;

static struct option longopts[] = {
  { "help",    no_argument,       NULL, 'h' },
  { "dump",    no_argument,       NULL, 'd' },
  { "execute", no_argument,       NULL, 'e' },
  { "outfile", required_argument, NULL, 'o' },
  { "version", no_argument,       NULL, 'v' },
  { NULL,      no_argument,       NULL, 0   }
};

void version(void) {
  printf("-[----->+<]>---.--.++.--.+++.----.+++[->++<]>+.++++++++++"
         "+ +++.+++++.>++++++++++.\n");
}

void usage(FILE *stream) {
  fprintf(stream, "Usage: %s [option] [-o outfile] [infile]\n", progname);
}

void help(void) {
  usage(stdout);
  printf("\n");
  printf("Ahead-of-time brainfuck compiler using libgccjit.\n\n"
         "Options:\n"
         "  -d, --dump\t\t\t Dump assembly\n"
         "  -e, --execute\t\t\t JIT interpret without creating executable\n"
         "  -h, --help\t\t\t Useless help message\n"
         "  -o, --outfile FILENAME\t Target executable filename\n"
         "  -v, --version\t\t\t Print version number\n");
}

void gen_instructions(gcc_jit_context *ctx, gcc_jit_function *program,
                      char *s) {
  gcc_jit_lvalue *cell;
  gcc_jit_type *int_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT);
  gcc_jit_type *cell_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT8_T);

  gcc_jit_block *current_block = gcc_jit_function_new_block(program, "entry");

  gcc_jit_rvalue *tape =
      gcc_jit_param_as_rvalue(gcc_jit_function_get_param(program, 0));
  gcc_jit_lvalue *index =
      gcc_jit_function_new_local(program, NULL, int_type, "index");
  gcc_jit_block_add_assignment(current_block, NULL, index,
                               gcc_jit_context_zero(ctx, int_type));

  gcc_jit_param *putchar_arg =
      gcc_jit_context_new_param(ctx, NULL, int_type, "c");
  gcc_jit_function *builtin_putchar =
      gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_IMPORTED,
                                   int_type, "putchar", 1, &putchar_arg, 0);
  gcc_jit_function *builtin_getchar =
      gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_IMPORTED,
                                   int_type, "getchar_unlocked", 0, NULL, 0);

  gcc_jit_rvalue *call;
  gcc_jit_rvalue *arg;

  lifo jmp_stack = { 0 };

  int ch;
  while ((ch = *s++)) {
    switch (ch) {
      case '>':
        gcc_jit_block_add_assignment_op(current_block, NULL, index,
                                        GCC_JIT_BINARY_OP_PLUS,
                                        gcc_jit_context_one(ctx, int_type));
        break;
      case '<':
        gcc_jit_block_add_assignment_op(current_block, NULL, index,
                                        GCC_JIT_BINARY_OP_MINUS,
                                        gcc_jit_context_one(ctx, int_type));
        break;
      case '+':
        cell = gcc_jit_context_new_array_access(
            ctx, NULL, tape, gcc_jit_lvalue_as_rvalue(index));
        gcc_jit_block_add_assignment_op(current_block, NULL, cell,
                                        GCC_JIT_BINARY_OP_PLUS,
                                        gcc_jit_context_one(ctx, cell_type));
        break;
      case '-':
        cell = gcc_jit_context_new_array_access(
            ctx, NULL, tape, gcc_jit_lvalue_as_rvalue(index));
        gcc_jit_block_add_assignment_op(current_block, NULL, cell,
                                        GCC_JIT_BINARY_OP_MINUS,
                                        gcc_jit_context_one(ctx, cell_type));
        break;
      case '.':
        cell = gcc_jit_context_new_array_access(
            ctx, NULL, tape, gcc_jit_lvalue_as_rvalue(index));
        arg = gcc_jit_context_new_cast(
            ctx, NULL, gcc_jit_lvalue_as_rvalue(cell),
            gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT));
        call = gcc_jit_context_new_call(ctx, NULL, builtin_putchar, 1, &arg);
        gcc_jit_block_add_eval(current_block, NULL, call);
        break;
      case ',':
        cell = gcc_jit_context_new_array_access(
            ctx, NULL, tape, gcc_jit_lvalue_as_rvalue(index));
        call = gcc_jit_context_new_call(ctx, NULL, builtin_getchar, 0, NULL);
        gcc_jit_block_add_assignment(
            current_block, NULL, cell,
            gcc_jit_context_new_cast(ctx, NULL, call, cell_type));
        break;
      case '[': {
        gcc_jit_block *loop_cond =
            gcc_jit_function_new_block(program, "loop_cond");
        gcc_jit_block *loop_body =
            gcc_jit_function_new_block(program, "loop_body");
        gcc_jit_block *loop_end =
            gcc_jit_function_new_block(program, "loop_end");

        gcc_jit_block_end_with_jump(current_block, NULL, loop_cond);
        cell = gcc_jit_context_new_array_access(
            ctx, NULL, tape, gcc_jit_lvalue_as_rvalue(index));
        gcc_jit_rvalue *cond = gcc_jit_context_new_comparison(
            ctx, NULL, GCC_JIT_COMPARISON_EQ, gcc_jit_lvalue_as_rvalue(cell),
            gcc_jit_context_zero(ctx, cell_type));

        gcc_jit_block_end_with_conditional(loop_cond, NULL, cond, loop_end,
                                           loop_body);
        current_block = loop_body;
        PUSH_STACK(jmp_stack, loop_cond, loop_end);
        break;
      }
      case ']':
        if (IS_EMPTY_STACK(jmp_stack))
          errx(EXIT_FAILURE, "Missing opening '['");

        gcc_jit_block_end_with_jump(current_block, NULL, TOP_START(jmp_stack));
        current_block = TOP_END(jmp_stack);

        POP_STACK(jmp_stack);
        break;
      default:
        break;
    }
  }

  if (!IS_EMPTY_STACK(jmp_stack))
    errx(EXIT_FAILURE, "Missing closing ']'");

  gcc_jit_block_end_with_void_return(current_block, NULL);
}

void read_file(char *file, char *buffer) {
  int fd;
  if ((fd = open(file, O_RDONLY)) < 0)
    err(EXIT_FAILURE, NULL);

  posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);

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
  unsetenv("POSIXLY_CORRECT");
  progname = basename(argv[0]);

  gcc_jit_context *ctx = gcc_jit_context_acquire();

  gcc_jit_context_set_int_option(ctx, GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL, 3);
  char *outfile = "bf.out";
  bool interpret = false;

  int opt;
  while ((opt = getopt_long(argc, argv, "hdevo:", longopts, NULL)) != -1) {
    switch (opt) {
      case 'h':
        help();
        exit(EXIT_SUCCESS);
      case 'v':
        version();
        exit(EXIT_SUCCESS);
      case 'd':
        gcc_jit_context_set_bool_option(
            ctx, GCC_JIT_BOOL_OPTION_DUMP_GENERATED_CODE, 1);
        break;
      case 'e':
        interpret = true;
        break;
      case 'o':
        outfile = optarg;
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

  gcc_jit_type *return_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_VOID);
  gcc_jit_type *cell_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT8_T);
  gcc_jit_type *tape_type = gcc_jit_type_get_pointer(cell_type);

  gcc_jit_param *params[1] = { gcc_jit_context_new_param(ctx, NULL, tape_type,
                                                         "tape") };

  gcc_jit_function *program =
      gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_EXPORTED,
                                   return_type, "bf_program", 1, params, 0);

  gen_instructions(ctx, program, buffer);

  if (interpret) {
    gcc_jit_result *result = gcc_jit_context_compile(ctx);
    BF_program fn = (BF_program) gcc_jit_result_get_code(result, "bf_program");

    uint8_t tape[TAPE_SIZE] = { 0 };
    fn(tape);

#ifdef DEBUG
    gcc_jit_result_release(result);
#endif

  } else {
    gcc_jit_type *int_type = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT);
    gcc_jit_function *main = gcc_jit_context_new_function(
        ctx, NULL, GCC_JIT_FUNCTION_EXPORTED, int_type, "main", 0, NULL, 0);

    gcc_jit_block *main_block =
        gcc_jit_function_new_block(main, "program_entry");

    gcc_jit_lvalue *tape = gcc_jit_function_new_local(
        main, NULL,
        gcc_jit_context_new_array_type(ctx, NULL, cell_type, TAPE_SIZE),
        "tape");

    gcc_jit_lvalue *cell = gcc_jit_context_new_array_access(
        ctx, NULL, gcc_jit_lvalue_as_rvalue(tape),
        gcc_jit_context_one(ctx, int_type));
    gcc_jit_rvalue *ptr = gcc_jit_lvalue_get_address(cell, NULL);

    gcc_jit_rvalue *args[1] = { ptr };
    gcc_jit_rvalue *call =
        gcc_jit_context_new_call(ctx, NULL, program, 1, args);
    gcc_jit_block_add_eval(main_block, NULL, call);
    gcc_jit_block_end_with_return(main_block, NULL,
                                  gcc_jit_context_zero(ctx, int_type));

    gcc_jit_context_compile_to_file(ctx, GCC_JIT_OUTPUT_KIND_EXECUTABLE,
                                    outfile);
  }

#ifdef DEBUG
  gcc_jit_context_release(ctx);
#endif

  return 0;
}
