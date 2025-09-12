#ifndef _ANSI_TERM_H
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "nob.h"

#ifndef ANSI_TERM_READ_BUFFER_SIZE
#  define ANSI_TERM_READ_BUFFER_SIZE 1024
#endif // ANSI_TERM_READ_BUFFER_SIZE

#define ANSI_TERM_ENABLE_ALT_BUFFER   "\x1b[?1049h"
#define ANSI_TERM_DISABLE_ALT_BUFFER  "\x1b[?1049l"

#define ANSI_TERM_MOVE_CURSOR_TO_LINE_START  "\r"
#define ANSI_TERM_MOVE_CURSOR_TO_HOME        "\x1b[H"

#define ANSI_TERM_SAVE_CURSOR_POSITION     "\x1b 7"
#define ANSI_TERM_RESTORE_CURSOR_POSITION  "\x1b 8"

#define ANSI_TERM_CLEAR_FROM_CURSOR_TO_LINE_END    "\x1b[0K"
#define ANSI_TERM_CLEAR_FROM_CURSOR_TO_LINE_START  "\x1b[1K"
#define ANSI_TERM_CLEAR_CURSOR_CURRENT_LINE        "\x1b[2K"

#define ANSI_TERM_CLEAR_FROM_CURSOR_TO_SCREEN_END    "\x1b[0J"
#define ANSI_TERM_CLEAR_FROM_CURSOR_TO_SCREEN_START  "\x1b[1J"
#define ANSI_TERM_CLEAR_ENTIRE_SCREEN                "\x1b[2J"

static inline void ansi_term_start();
static inline void ansi_term_end();

static inline void ansi_term_clear_screen();
void ansi_term_printfn(const char *fmt, ...);
static inline void ansi_term_printn(const char *message);

void ansi_term_move_cursor(int x, int y);

bool ansi_term_read(Nob_String_View *read_data);
bool ansi_term_read_line(Nob_String_View *read_data);

#endif // _ANSI_TERM_H




#ifdef ANSI_TERM_IMPLEMENTATION

static bool ansi_term_alt_buffer_enabled = false;

static inline void ansi_term_start() {
  printf(ANSI_TERM_ENABLE_ALT_BUFFER ANSI_TERM_CLEAR_ENTIRE_SCREEN ANSI_TERM_MOVE_CURSOR_TO_HOME);
  fflush(stdout);
  ansi_term_alt_buffer_enabled = true;
}

static inline void ansi_term_end() {
  printf(ANSI_TERM_DISABLE_ALT_BUFFER);
  fflush(stdout);
  ansi_term_alt_buffer_enabled = false;
}

static inline void ansi_term_clear_screen() {
  printf(ANSI_TERM_CLEAR_ENTIRE_SCREEN ANSI_TERM_MOVE_CURSOR_TO_HOME);
}

void ansi_term_printfn(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stdout, fmt, args);
  va_end(args);
  if (ansi_term_alt_buffer_enabled) printf("\x1b[1E");
  else printf("\n");
}

static inline void ansi_term_printn(const char *message) {
  if (ansi_term_alt_buffer_enabled) printf("%s\x1b[1E", message);
  else printf("%s\n", message);
}

void ansi_term_move_cursor(int x, int y) {
  size_t save_point = nob_temp_save();
  char x_code = 'C';
  char y_code = 'B';

  if (x < 0) {
    x_code = 'D';
    x *= -1;
  }

  if (y < 0) {
    y_code = 'A';
    y *= -1;
  }

  const char *ansi_code = NULL;
  if (x != 0 && y != 0) {
    ansi_code = nob_temp_sprintf("\x1b[%d%c\x1b[%d%c", x, x_code, y, y_code);
  } else if (x != 0) {
    ansi_code = nob_temp_sprintf("\x1b[%d%c", x, x_code);
  } else {
    ansi_code = nob_temp_sprintf("\x1b[%d%c", y, y_code);
  }

  printf(ansi_code);
}

bool ansi_term_read(Nob_String_View *read_data) {
  static char buf[ANSI_TERM_READ_BUFFER_SIZE];
  static size_t offset = 0;
  errno = 0;
  int n = read(STDIN_FILENO, buf, ANSI_TERM_READ_BUFFER_SIZE);

  if (n < 0) {
    nob_log(NOB_ERROR, "Read failed: %s", strerror(errno));
    return false;
  }

  if (read_data) *read_data = nob_sv_trim((Nob_String_View) { .data = buf, .count = n });

  return true;
}

bool ansi_term_read_line(Nob_String_View *read_data) {
  char *line = NULL;
  size_t cap = 0;
  errno = 0;
  size_t nread = getline(&line, &cap, stdin);
  if (errno != 0) {
    if (line) free(line);
    nob_log(NOB_ERROR, "LineRead failed: %s", strerror(errno));
    return false;
  }

  if (read_data) *read_data = (Nob_String_View) { .data = line, .count = nread };
  else if (line) free(line);

  return true;
}

#endif // ANSI_TERM_IMPLEMENTATION
