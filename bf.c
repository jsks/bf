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
#include <libgen.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define READ_SIZE 1024 * 8
#define MAX_FILE_SIZE 1024 * 1024 * 8
#define TAPE_SIZE 30000
#define STACK_SIZE 256
#define PROGRAM_SIZE 4096

#ifdef _BF_STRICT_CHECKS
#define BOUNDS_CHECK(i)                                                        \
  if (i < 0 || i >= TAPE_SIZE)                                                 \
    errx(EXIT_FAILURE, "Out-of-bounds memory access at position %d", i);
#define OVERFLOW_CHECK(arr, pos, x)                                            \
  if ((arr[pos]) >= INT8_MAX - x)                                              \
    errx(EXIT_FAILURE, "Integer overflow at position %d", pos);
#define UNDERFLOW_CHECK(arr, pos, x)                                           \
  if ((arr[pos]) <= INT8_MIN + x)                                              \
    errx(EXIT_FAILURE, "Integer underflow at position %d", pos);
#else
#define BOUNDS_CHECK(i)
#define OVERFLOW_CHECK(arr, pos, x)
#define UNDERFLOW_CHECK(arr, pos, x)
#endif

#define IS_EMPTY_STACK(stack) (stack.len == 0)
#define POP_STACK(stack) stack.data[--stack.len]
#define PUSH_STACK(stack, x)                                                   \
  do {                                                                         \
    if (stack.len == STACK_SIZE)                                               \
      errx(EXIT_FAILURE, "Nested loops exceeded stack size");                  \
    stack.data[stack.len++] = x;                                               \
  } while (0)

static const char *op_strings[] = { "ZERO",    "ZEROSEEK", "ADD",
                                    "MINUS",   "READ",     "PUT",
                                    "JMP_FWD", "JMP_BCK",  "END" };

typedef enum {
  ZERO,
  ZEROSEEK,
  ADD,
  MINUS,
  READ,
  PUT,
  JMP_FWD,
  JMP_BCK,
  END
} op_code;

#ifdef DEBUG
#include <locale.h>

#define TRACE(op) ncalls[op] += 1
#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))

static int ncalls[LEN(op_strings)] = { 0 };
#else
#define TRACE(op)
#endif

typedef struct {
  op_code code;
  ssize_t arg, offset;
} op;

typedef struct {
  op *ops;
  size_t n, len;
} program_t;

typedef struct {
  ptrdiff_t data[STACK_SIZE];
  size_t len;
} lifo;

static const char *progname;

static struct option longopts[] = {
  {"help",       no_argument, NULL, 'h'},
  { "print-ast", no_argument, NULL, 'p'},
  { "version",   no_argument, NULL, 'v'},
  { NULL,        no_argument, NULL, 0  }
};

void version(void) {
  printf("-[----->+<]>---.--.++.--.+++.----.[--->++++"
         "+++<]>.+++++.++++++.+++[->+++<]>.++++++++++"
         "+++.--.++.-------------.[--->+<]>---.+++[->"
         "+++<]>.+++++++++++++.>++++++++++.\n");
}

void usage(FILE *stream) {
  fprintf(stream, "Usage: %s [option] [infile]\n", progname);
}

void help(void) {
  usage(stdout);
  printf("\n");
  printf("A simple brainfuck interpreter.\n\n"
         "Options:\n"
         "  -h, --help\t\t Useless help message\n"
         "  -p, --print-ast\t Print parsed AST without executing infile\n"
         "  -v, --version\t\t Print version number\n");
}

program_t *init_program(size_t capacity) {
  program_t *p;
  if (!(p = malloc(sizeof(program_t))) ||
      !(p->ops = malloc(capacity * sizeof(op))))
    err(EXIT_FAILURE, NULL);

  p->n = 0;
  p->len = capacity;

  return p;
}

void resize_program(program_t *program) {
  program->len *= 2;
  if (!(program->ops = reallocarray(program->ops, program->len, sizeof(op))))
    err(EXIT_FAILURE, NULL);
}

void add_op(program_t *program, op_code code, ssize_t arg, ssize_t offset) {
  if (program->n == program->len)
    resize_program(program);

  program->ops[program->n] = (op){ .code = code, .arg = arg, .offset = offset };
  program->n++;
}

void pop_op(program_t *program) {
  if (program->n > 0)
    program->n--;
}

op *last_op(program_t *program) {
  return (program->n > 0) ? &program->ops[program->n - 1] : NULL;
}

void destroy_program(program_t **program) {
  free((*program)->ops);
  free(*program);
  *program = NULL;
}

void print_ast(program_t *program) {
  for (op *p = program->ops; p && p->code != END; p++)
    printf("%s(%ld, %ld)\n", op_strings[p->code], p->arg, p->offset);

  printf("END\n\n");
}

bool is_valid_token(char ch) {
  return ch == '+' || ch == '-' || ch == '>' || ch == '<' || ch == '.' ||
         ch == ',' || ch == '[' || ch == ']';
}

bool is_repeatable_token(char ch) {
  return ch == '+' || ch == '-';
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

program_t *parse(char *s) {
  program_t *program = init_program(PROGRAM_SIZE);

  int ch, prev_token = 0, offset = 0, start_pos = 0;
  char *next_token = NULL;
  op *p;
  ptrdiff_t jmp_pos;
  lifo jmp_stack = { 0 };

  while ((ch = *s++)) {
    if (!is_valid_token(ch))
      continue;

    if (ch == prev_token && is_repeatable_token(ch)) {
      last_op(program)->arg++;
      continue;
    } else {
      prev_token = ch;
    }

    switch (ch) {
      case '-':
        add_op(program, MINUS, 1, offset);
        break;
      case '+':
        add_op(program, ADD, 1, offset);
        break;
      case '<':
        offset--;
        break;
      case '>':
        offset++;
        break;
      case '.':
        add_op(program, PUT, 0, offset);
        break;
      case ',':
        add_op(program, READ, 0, offset);
        break;
      case '[':
        if (*s == '-' && (next_token = peek(s)) && *next_token == ']') {
          add_op(program, ZERO, 0, offset);
          s = next_token + 1;
        } else {
          add_op(program, JMP_FWD, 0, offset);
          PUSH_STACK(jmp_stack, last_op(program) - program->ops);
        }
        break;
      case ']':
        if (IS_EMPTY_STACK(jmp_stack))
          errx(EXIT_FAILURE, "Missing opening '['");

        jmp_pos = POP_STACK(jmp_stack);
        if ((p = last_op(program)) && p->code == JMP_FWD) {
          start_pos = p->offset;
          pop_op(program);
          add_op(program, ZEROSEEK, offset, start_pos);
        } else {
          program->ops[jmp_pos].arg = last_op(program) - program->ops + 1;
          add_op(program, JMP_BCK, jmp_pos, offset);
        }
        break;
      default:
        break;
    }

    if (ch != '>' && ch != '<')
      offset = 0;
  }

  if (!IS_EMPTY_STACK(jmp_stack))
    errx(EXIT_FAILURE, "Missing closing ']'");

  add_op(program, END, 0, 0);
  return program;
}

void run(program_t *program) {
  int8_t tape[TAPE_SIZE] = { 0 };
  int i = 0;

  for (op *p = program->ops; p->code != END; p++) {
    i += p->offset;
    BOUNDS_CHECK(i);

    TRACE(p->code);
    switch (p->code) {
      case ZERO:
        tape[i] = 0;
        break;
      case ZEROSEEK:
        while (tape[i] != 0) {
          i += p->arg;
          BOUNDS_CHECK(i);
        }
        break;
      case ADD:
        OVERFLOW_CHECK(tape, i, p->arg);
        tape[i] += p->arg;
        break;
      case MINUS:
        UNDERFLOW_CHECK(tape, i, p->arg);
        tape[i] -= p->arg;
        break;
      case READ:
        tape[i] = getchar_unlocked();
        break;
      case PUT:
        putchar_unlocked(tape[i]);
        break;
      case JMP_FWD:
        if (tape[i] == 0)
          p = &program->ops[p->arg];
        break;
      case JMP_BCK:
        if (tape[i] != 0)
          p = &program->ops[p->arg];
        break;
      default:
        break;
    }
  }
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
  progname = basename(argv[0]);

  bool debug_ast = false;
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
        debug_ast = true;
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

  program_t *program = parse(buffer);

  if (debug_ast)
    print_ast(program);

  run(program);

#ifdef DEBUG
  setlocale(LC_NUMERIC, "");

  printf("\n\nCalls per instruction:\n");
  for (size_t i = 0; i < LEN(op_strings) - 1; i++)
    printf("%-10s%'d\n", op_strings[i], ncalls[i]);

  destroy_program(&program);
#endif

  return 0;
}
