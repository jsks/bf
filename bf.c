/*
 * Copyright (c) 2021, Joshua Krusell
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

#define _DEFAULT_SOURCE

#include <assert.h>
#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_FILE_SIZE 8 * 1024 * 1024
#define TAPE_SIZE 30000
#define STACK_SIZE 256
#define PROGRAM_SIZE 1024

#define SIGN(x) ((x > 0) - (x < 0))

#define BOUNDS_CHECK(i) assert(i >= 0 || i < TAPE_SIZE)
#define OVERFLOW_CHECK(x, y) assert(x < INT8_MAX - y)
#define UNDERFLOW_CHECK(x, y) assert(x > INT8_MIN - y)

#define IS_EMPTY_STACK(stack) (stack.len == 0)
#define POP_STACK(stack) stack.data[--stack.len]
#define PUSH_STACK(stack, x) do {                               \
    if (stack.len == STACK_SIZE)                                \
      errx(EXIT_FAILURE, "Nested loops exceeded stack size");   \
    stack.data[stack.len++] = x;                                \
  } while (0)

typedef enum {
  ZERO,
  ZEROSEEK,
  ADD,
  SHIFT,
  READ,
  PUT,
  JMP_FWD,
  JMP_BCK,
  END
} op_code;

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

program_t *init_program(size_t capacity) {
  program_t *p;
  if (!(p = malloc(sizeof(program_t))) || !(p->ops = malloc(capacity * sizeof(op))))
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

  program->ops[program->n].code = code;
  program->ops[program->n].arg = arg;
  program->ops[program->n].offset = offset;

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
  const char *ops[] = { "ZERO", "ADD", "SHIFT", "READ",
                        "PUT", "JMP_FWD", "JMP_BCK", "END" };
  for (op *p = program->ops; p && p->code != END; p++) {
    printf("%s(%ld, %ld)\n", ops[p->code], p->arg, p->offset);
  }

  printf("END\n");
}

bool is_valid_token(char ch) {
  return ch == '+' || ch == '-' || ch == '>' || ch == '<' ||
    ch == '.' || ch == ',' || ch == '[' || ch == ']';
}

bool is_repeatable_token(char ch) {
  return ch == '+' || ch == '-' || ch == '>' || ch == '<';
}

char peek(char *s) {
  int ch;
  while ((ch = *s++)) {
    if (!is_valid_token(ch))
      continue;

    return ch;
  }

  return '\0';
}

program_t *parse(char *s) {
  program_t *program = init_program(PROGRAM_SIZE);

  int ch, prev_token = 0;
  ptrdiff_t jmp_pos;
  lifo jmp_stack = { 0 };

  int sgn = 1;
  op *p = NULL;

  while ((ch = *s++)) {
    if (!is_valid_token(ch))
      continue;

    if (ch == prev_token && is_repeatable_token(ch)) {
      last_op(program)->arg += 1 * SIGN(last_op(program)->arg);
      continue;
    } else {
      prev_token = ch;
    }

    switch (ch) {
    case '-':
      sgn = -1;;
    case '+':
      if ((p = last_op(program)) && p->code == SHIFT) {
        pop_op(program);
        add_op(program, ADD, 1 * sgn, p->arg);
      } else {
        add_op(program, ADD, 1 * sgn, 0);
      }

      sgn = 1;
      break;
    case '<':
      sgn = -1;
    case '>':
      add_op(program, SHIFT, 1 * sgn, 0);
      sgn = 1;
      break;
    case '.':
      add_op(program, PUT, 0, 0);
      break;
    case ',':
      add_op(program, READ, 0, 0);
      break;
    case '[':
      if (*s == '-' && peek(s) == ']') {
        add_op(program, ZERO, 0, 0);
        s += 2;
      } else {
        add_op(program, JMP_FWD, 0, 0);
        PUSH_STACK(jmp_stack, last_op(program) - program->ops);
      }
      break;
    case ']':
      if (IS_EMPTY_STACK(jmp_stack))
        errx(EXIT_FAILURE, "Missing opening '['");

      jmp_pos = POP_STACK(jmp_stack);
      program->ops[jmp_pos].arg = last_op(program) - program->ops + 1;

      add_op(program, JMP_BCK, jmp_pos, 0);
      break;
    default:
      break;
    }
  }

  if (!IS_EMPTY_STACK(jmp_stack))
    errx(EXIT_FAILURE, "Missing closing ']'");

  add_op(program, END, 0, 0);
  return program;
}

int8_t run(program_t *program) {
  int8_t tape[TAPE_SIZE] = { 0 };
  int i = 0;

  for (op *p = program->ops; p->code != END; p++) {
    switch (p->code) {
    case ZERO:
      tape[i] = 0;
      break;
    case ADD:
      //      ADDITION_CHECK(tape[i], p->arg);
      i += p->offset;
      tape[i] += p->arg;
      break;
    case SHIFT:
      i += p->arg;
      BOUNDS_CHECK(i);
      break;
    case READ:
      tape[i] = getchar();
      break;
    case PUT:
      putchar(tape[i]);
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

  return tape[i];
}

off_t file_size(char *file) {
  struct stat at;
  if (stat(file, &at) != 0)
    err(EXIT_FAILURE, "%s", file);

  return at.st_size;
}

char *read_file(char *file) {
  off_t fsize;
  if ((fsize = file_size(file)) > MAX_FILE_SIZE)
    errx(EXIT_FAILURE, "File %s exceeds read limits", file);

  char *buffer;
  if (!(buffer = malloc(fsize + 1)))
    err(EXIT_FAILURE, NULL);

  FILE *fp;
  if (!(fp = fopen(file, "r")))
    err(EXIT_FAILURE, "Unable to open file %s", file);

  size_t len = fread(buffer, 1, fsize, fp);
  if (ferror(fp))
    errx(EXIT_FAILURE, "Error reading file %s", file);

  buffer[len] = '\0';
  fclose(fp);

  return buffer;
}

int main(int argc, char *argv[]) {
  if (argc != 2)
    errx(EXIT_FAILURE, "Usage: %s <program.bf>", basename(argv[0]));

  char *buffer = read_file(argv[1]);
  program_t *program = parse(buffer);
  run(program);

  destroy_program(&program);
  free(buffer);

  return 0;
}
