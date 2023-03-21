/*
 * Copyright (c) 2020, Joshua Krusell
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

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define MAX_FILE_SIZE 8 * 1024 * 1024
#define TAPE_SIZE 30000
#define STACK_SIZE 256

#define BOUNDS_CHECK(i) assert(i >= 0 || i < TAPE_SIZE)
#define OVERFLOW_CHECK(x) assert(x != INT8_MAX)
#define UNDERFLOW_CHECK(x) assert(x != INT8_MIN)

#define TOP_STACK(stack) stack.data[stack.len - 1]
#define IS_EMPTY_STACK(stack) (stack.len == 0)
#define POP_STACK(stack) do {                   \
    assert(stack.len > 0);                      \
    stack.len--;                                \
} while (0)
#define PUSH_STACK(stack, p) do {               \
    assert(stack.len < STACK_SIZE);             \
    stack.data[stack.len++] = p;                \
} while (0)

typedef struct {
  char *data[STACK_SIZE];
  size_t len;
} LIFO;

LIFO jump_stack = { 0 };
int8_t tape[TAPE_SIZE] = { 0 };

void run(char *s) {
  int ch, i = 0;

  while ((ch = *s++)) {
    switch (ch) {
      case '+':
        OVERFLOW_CHECK(tape[i]);
        tape[i]++;
        break;
      case '-':
        UNDERFLOW_CHECK(tape[i]);
        tape[i]--;
        break;
      case '>':
        i++;
        BOUNDS_CHECK(i);
        break;
      case '<':
        i--;
        BOUNDS_CHECK(i);
        break;
      case '.':
        putchar(tape[i]);
        break;
      case ',':
        tape[i] = getchar();
        break;
      case '[':
        if (tape[i] != 0) {
          PUSH_STACK(jump_stack, s);
        } else {
          size_t inner_depth = 0;
          while ((ch = *s++)) {
            if (ch == ']' && inner_depth-- == 0)
              break;
            else if (ch == '[')
              inner_depth++;
          }
        }

        break;
      case ']':
        if (IS_EMPTY_STACK(jump_stack))
          errx(EXIT_FAILURE, "Missing opening '['");

        if (tape[i] != 0)
          s = TOP_STACK(jump_stack);
        else
          POP_STACK(jump_stack);

        break;
    }
  }

  if (!IS_EMPTY_STACK(jump_stack))
    errx(EXIT_FAILURE, "Missing closing '['");
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
    errx(EXIT_FAILURE, "./bf <program.bf>");

  char *instructions = read_file(argv[1]);
  run(instructions);
  free(instructions);
}
