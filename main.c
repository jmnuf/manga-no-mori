#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define NOB_FREE(ptr) if (ptr) free(ptr)
#define NOB_STRIP_PREFIX
#include "nob.h"

#include "ext_sv.h"
#include "ansi_term.h"

#define MORI_FILE_NAME "mori-mori"
#define MORI_VERSION 0
#define MORI_FULL_VERSION "0.1.0"

#define list_remove(l, index)                                \
do {                                                         \
  if ((l)->count > 0 && index < (l)->count && index >= 0) {  \
    for (size_t i = index; i < (l)->count - 1; ++i) {        \
      (l)->items[i] = (l)->items[i + 1];                     \
    }                                                        \
    (l)->count--;                                            \
  }                                                          \
} while (0)

#define flush() fflush(stdout)

typedef unsigned char byte_t;

typedef struct {
  Nob_String_View name;
  Nob_String_View url;
  union {
    uint32_t chapter;
    char chapter_bytes[sizeof(uint32_t)];
  };
  union {
    uint32_t volume;
    char volume_bytes[sizeof(uint32_t)];
  };
} Mori_Tree;

typedef struct {
  Mori_Tree *items;
  size_t count;
  size_t capacity;
} Mori_Mori;

#define MORI_HEADER_SIZE 6
const byte_t const mori_header[MORI_HEADER_SIZE] = { 'M', 'O', 'R', 'I', 0x69, MORI_VERSION };

Mori_Mori mori = {0};

const char *get_morimori_file_path() {
  const char *home_path = getenv("HOME");
  return nob_temp_sprintf("%s/.config/%s", home_path, MORI_FILE_NAME);
}

bool get_v0_mori_tree_from_bytes(byte_t **bytes, size_t *bytes_len, Mori_Tree *tree) {
  if (*bytes_len == 0) return false;

  byte_t num_buf[sizeof(uint32_t)];

  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (*bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected name length as a ui32 at the start of a mori_tree");
      return false;
    }
    num_buf[i] = shift(*bytes, *bytes_len);
  }
  uint32_t name_len = *(uint32_t *)num_buf;
  if (*bytes_len == 0 || *bytes_len < name_len) {
      nob_log(ERROR, "Malformed Mori Tree: Not enough data exists in file to read name");
    return false;
  }
  if (name_len == 0) return false;
  // printf("[DEBUG] Loaded name length '%zu' from %zu bytes\n", (size_t)name_len, sizeof(uint32_t));
  tree->name = (String_View){ .count = (size_t)name_len, .data = (const char *)*bytes };
  *bytes += name_len;
  nob_log(INFO, "Loading manga: '"SV_Fmt"'...", SV_Arg(tree->name));
  // printf("[DEBUG] Loading manga: '"SV_Fmt"'...\n", SV_Arg(tree->name));

  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (*bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected url length as a ui32 after name");
      return false;
    }
    num_buf[i] = shift(*bytes, *bytes_len);
  }
  uint32_t url_len = *(uint32_t *)num_buf;
  if (*bytes_len == 0 || *bytes_len < name_len) {
      nob_log(ERROR, "Malformed Mori Tree: Not enough data exists in file to read url");
    return false;
  }
  tree->url = (String_View){ .count = (size_t)url_len, .data = (const char *)*bytes };
  *bytes += url_len;

  
  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (*bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected chapters count as a ui32 after url");
      return false;
    }
    num_buf[i] = shift(*bytes, *bytes_len);
  }
  uint32_t chapters_count = *(uint32_t *)num_buf;
  tree->chapter = chapters_count;
  nob_log(INFO, "    Chapter: %zu", tree->chapter);

  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (*bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected volumes count as a ui32 after chapters");
      return false;
    }
    num_buf[i] = shift(*bytes, *bytes_len);
  }
  uint32_t volumes_count = *(uint32_t *)num_buf;
  tree->volume = volumes_count;
  nob_log(INFO, "    Volume: %zu", tree->volume);

  return true;
}

void read_morimori_file(String_Builder *sb, const char *morimori_file_path) {
  nob_log(INFO, "Reading morimori file...");
  if (!read_entire_file(morimori_file_path, sb)) {
    nob_log(WARNING, "Failed to read morimori file! Data in it will be ignored");
    return;
  }
  if (sb->count < MORI_HEADER_SIZE) {
    nob_log(ERROR, "morimori file is missing header");
    return;
  }

  for (size_t i = 0; i < MORI_HEADER_SIZE - 1; ++i) {
    byte_t b = (byte_t)shift(sb->items, sb->count);
    if (b != mori_header[i]) {
      nob_log(ERROR, "Header is invalid, ignoring data");
      return;
    }
  }

  byte_t v = (byte_t)shift(sb->items, sb->count);
  if (v > MORI_VERSION) {
    nob_log(ERROR, "Invalid version in header");
    return;
  }

  byte_t *bytes = sb->items;
  size_t len = sb->count;

  switch (v) {
  case 0:
    nob_log(INFO, "Loading morimori v0...");
    while (len) {
      Mori_Tree tree = {0};
      if (!get_v0_mori_tree_from_bytes(&bytes, &len, &tree)) return;
      if (tree.name.count) da_append(&mori, tree);
    }
    break;

  default:
    nob_log(ERROR, "Unhandled version %d", (int)v);
    break;
  }
}

// TODO: Write directly to a file instead of throwing everything into the heap before writing everything at once
bool write_morimori_file(const char *file_path) {
  Nob_String_Builder sb = {0};
  bool result = true;

  sb_append_buf(&sb, mori_header, MORI_HEADER_SIZE);

  da_foreach(Mori_Tree, it, &mori) {
    char *nbytes = NULL;
    uint32_t value = 0;

    value = (uint32_t)it->name.count;
    nbytes = (char*)&value;
    sb_append_buf(&sb, nbytes, sizeof(uint32_t));
    if (it->name.count > 0) sb_append_buf(&sb, it->name.data, it->name.count);

    value = (uint32_t)it->url.count;
    nbytes = (char*)&value;
    sb_append_buf(&sb, nbytes, sizeof(uint32_t));
    if (it->url.count > 0) sb_append_buf(&sb, it->url.data, it->url.count);

    sb_append_buf(&sb, it->chapter_bytes, sizeof(uint32_t));

    sb_append_buf(&sb, it->volume_bytes, sizeof(uint32_t));
  }

  result = write_entire_file(file_path, sb.items, sb.count);

  sb_free(sb);
  return result;
}

bool read_index_from_stdin(const char *prompt, size_t *value) {
  const size_t cap = 1024;
  char buf[cap];
  int n = 0;

  printf("%s ", prompt);
  flush();

  n = read(STDIN_FILENO, buf, cap);
  if (n < 0) {
    nob_log(ERROR, "Read failed: %s", strerror(errno));
    return false;
  }
  size_t len = n >= cap ? cap : n;
  buf[n >= cap ? cap - 1 : n] = 0;

  if (buf[0] == 'q') return false;

  errno = 0;
  size_t i = (size_t)strtoul(buf, NULL, 10);
  if (errno != 0) {
    nob_log(ERROR, "Invalid input: %s", strerror(errno));
    return false;
  }

  *value = i;
  return true;
}

void display_tree_short(size_t i, const char *prefix) {
  Mori_Tree *tree = mori.items + i;

  if (!prefix) {
    ansi_term_printfn("Mori_Tree :: struct {");
    ansi_term_printfn("  .Index   = %zu;", i);
    ansi_term_printfn("  .Name    = \""SV_Fmt"\";", SV_Arg(tree->name));
    ansi_term_printfn("  .Chapter = %zu;", tree->chapter);
    ansi_term_printfn("}");
    return;
  }

  ansi_term_printfn("%sMori_Tree :: struct {", prefix);
  ansi_term_printfn("%s  .Index   = %zu;", prefix, i);
  ansi_term_printfn("%s  .Name    = \""SV_Fmt"\";", prefix, SV_Arg(tree->name));
  ansi_term_printfn("%s  .Chapter = %zu;", prefix, tree->chapter);
  ansi_term_printfn("%s}", prefix);
}

void display_tree_full(size_t i, const char *prefix) {
  Mori_Tree *tree = mori.items + i;

  if (!prefix) {
    ansi_term_printfn("Mori_Tree :: struct {");
    ansi_term_printfn("  .Index   = %zu;", i);
    ansi_term_printfn("  .Name    = \""SV_Fmt"\";", SV_Arg(tree->name));
    ansi_term_printfn("  .Url     = \""SV_Fmt"\";", SV_Arg(tree->url));
    ansi_term_printfn("  .Chapter = %zu;", tree->chapter);
    ansi_term_printfn("  .Volume  = %zu;", tree->volume);
    ansi_term_printfn("}");
    return;
  }

  ansi_term_printfn("%sMori_Tree :: struct {", prefix);
  ansi_term_printfn("%s  .Index   = %zu;", prefix, i);
  ansi_term_printfn("%s  .Name    = \""SV_Fmt"\";", prefix, SV_Arg(tree->name));
  ansi_term_printfn("%s  .Url     = \""SV_Fmt"\";", prefix, SV_Arg(tree->url));
  ansi_term_printfn("%s  .Chapter = %zu;", prefix, tree->chapter);
  ansi_term_printfn("%s  .Volume  = %zu;", prefix, tree->volume);
  ansi_term_printfn("%s}", prefix);
}

void display_mori_tree_short_list() {
  ansi_term_printn("╓─<Your Manga Forest>");
  for (size_t i = 0; i < mori.count; ++i) {
    ansi_term_printfn("╟─ Index %zu", i);
    display_tree_short(i, "║    ");
  }
  ansi_term_printfn("╙─ Mori_Tree mori[%zu];", mori.count);
  flush();
}

void display_mori_tree_full_list() {
  ansi_term_printn("╓─<Your Manga Forest>");
  for (size_t i = 0; i < mori.count; ++i) {
    ansi_term_printfn("╟─ Index %zu", i);
    display_tree_full(i, "║    ");
  }
  ansi_term_printfn("╙─ Mori_Tree mori[%zu];", mori.count);
  flush();
}

#define GLOBAL_CMD_INIT_CAP 16
Nob_Cmd cmd = {0};

void display_actions_menu() {
  ansi_term_printn("╓─Actions:");
  ansi_term_printn("║ ╞ l - Lists all saved items");
  ansi_term_printn("║ ╞ s - Search for an item by their name");
  ansi_term_printn("║ ╞ c - Create new item");
  ansi_term_printn("║ ╞ d - Delete an existing item");
  ansi_term_printn("║ ╞ e - Edit existing item");
  ansi_term_printn("║ ╞ x - Copy url of item or the name if the url is missing");
  ansi_term_printn("║ ╞ i - Get full info of an item");
  ansi_term_printn("║ ╘ q - Quit out of program");
  printf(          "╙──\x1b[32m森\x1b[0m> ");
  flush();
}

bool handle_action(String_Builder *sb, char action) {
  switch (action) {
  case 'q':
    return true;

    case 'd': {
      ansi_term_clear_screen();

      size_t i = 0;
      if (!read_index_from_stdin("Delete_Index =", &i)) break;

      if (mori.count <= i) {
	nob_log(ERROR, "OutOfBounds: Trying to access index %zu in list of length %zu", i, mori.count);
	break;
      }

      nob_log(INFO, "Chopping tree %zu", i);

      list_remove(&mori, i);
    } break;

    case 'x': {
      ansi_term_clear_screen();

      size_t i = 0;
      if (!read_index_from_stdin("Copy_Index =", &i)) break;

      if (mori.count <= i) {
	nob_log(ERROR, "OutOfBounds: Trying to access index %zu in list of length %zu", i, mori.count);
	break;
      }

      Mori_Tree *tree = mori.items + i;
      size_t save_point = nob_temp_save();
      bool ok, url;
      if (tree->url.count) {
	url = true;
	cmd_append(&cmd, "wl-copy", nob_temp_sprintf(SV_Fmt, SV_Arg(tree->url)));
	ok = cmd_run(&cmd);
      } else {
	url = false;
	cmd_append(&cmd, "wl-copy", nob_temp_sprintf(SV_Fmt, SV_Arg(tree->name)));
	ok = cmd_run(&cmd);
      }
      nob_temp_rewind(save_point);

      if (ok) {
	nob_log(INFO, "Copied %s for "SV_Fmt, url ? "url" : "name", SV_Arg(tree->name));
      } else {
	nob_log(ERROR, "Failed to copy %s for "SV_Fmt, url ? "url" : "name", SV_Arg(tree->name));
      }
    } break;

    case 'c': {
      ansi_term_clear_screen();

      Mori_Tree tree = {0};
      String_View trimmed = {0};
      const size_t cap = 1024*2;
      char buf[cap];
      int n = 0;
      printf("Name :: ");
      flush();
      n = read(STDIN_FILENO, buf, cap) - 1;
      if (n <= 0) {
	nob_log(INFO, "Action cancelled");
	break;
      }
      trimmed = sv_trim((String_View) { .count = n, .data = buf });
      sb_append_buf(sb, trimmed.data, trimmed.count);
      tree.name = sv_trim((String_View) { .count = trimmed.count, .data = (sb->items + sb->count - trimmed.count) });

      printf("Url :: ");
      flush();
      n = read(STDIN_FILENO, buf, cap) - 1;
      if (n > 0) {
	trimmed = sv_trim((String_View) { .count = n, .data = buf });
	sb_append_buf(sb, trimmed.data, trimmed.count);
	tree.url = sv_trim((String_View) { .count = trimmed.count, .data = (sb->items + sb->count - trimmed.count) });
      }

      printf("Chapter :: ");
      flush();
      n = read(STDIN_FILENO, buf, cap);
      buf[n >= cap ? cap - 1 : n] = 0;
      tree.chapter = (uint32_t) atoi(buf);

      if (tree.chapter > 4) {
	printf("Volume :: ");
	flush();
	n = read(STDIN_FILENO, buf, cap);
	buf[n >= cap ? cap - 1 : n] = 0;
	tree.volume = (uint32_t) atoi(buf);
      } else {
	tree.volume = 1;
      }

      da_append(&mori, tree);
    } break;

    case 'e': {
      ansi_term_clear_screen();

      size_t i = 0;
      if (!read_index_from_stdin("Edit_Index =", &i)) break;
      if (mori.count <= i) {
	nob_log(ERROR, "OutOfBounds: Trying to access index %zu in list of length %zu", i, mori.count);
	break;
      }

      Mori_Tree *tree = mori.items + i;
      const size_t cap = 256;
      char buf[cap];
      int n = 0;
      size_t save_point = nob_temp_save();
      String_View read_data = {0}, trimmed = {0};
      char *last_error = NULL;
      while (true) {
	ansi_term_clear_screen();

	if (last_error) printf("ERROR: %s", last_error);
	nob_temp_rewind(save_point);
	last_error = NULL;
	if (read_data.data) {
	  free((void*)read_data.data);
	  read_data.data = NULL;
	  read_data.count = 0;
	}

	display_tree_full(i, NULL);
	printf("Edit_Field_Name = ");
	flush();

	if (!ansi_term_read_line(&read_data)) {
	  sleep(5);
	  continue;
	}
	trimmed = sv_trim(read_data);

	if (trimmed.count == 0) continue;

	const char *field = ntemp_sv_ascii_to_lower(trimmed);
	free((void*)read_data.data);
	read_data.data = NULL; read_data.count = 0;

	if (strcmp(field, "quit") == 0 || strcmp(field, "q") == 0) {
	  nob_temp_rewind(save_point);
	  break;
	}

	if (strcmp(field, "name") == 0) {
	  printf("tree->name = ");
	  flush();

	  if (!ansi_term_read_line(&read_data)) {
	    sleep(5);
	    continue;
	  }
	  trimmed = sv_trim(read_data);

	  if (trimmed.count) {
	    sb_append_buf(sb, trimmed.data, trimmed.count);
	    tree->name = sv_from_parts(sb->items + sb->count - trimmed.count, trimmed.count);
	  }

	  continue;
	}

	if (strcmp(field, "url") == 0) {
	  printf("tree->url = ");
	  flush();

	  if (!ansi_term_read_line(&read_data)) {
	    sleep(5);
	    continue;
	  }

	  trimmed = sv_trim(read_data);
	  if (trimmed.count) {
	    sb_append_buf(sb, trimmed.data, trimmed.count);
	    tree->url = sv_from_parts(sb->items + sb->count - trimmed.count, trimmed.count);
	  } else {
	    tree->url.count = 0;
	  }

	  continue;
	}

	if (strcmp(field, "chapter") == 0 || strcmp(field, "chapters") == 0) {
	  printf("tree->chapter = ");
	  flush();

	  if (!ansi_term_read_line(&read_data)) {
	    sleep(5);
	    continue;
	  }

	  trimmed = sv_trim(read_data);
	  if (trimmed.count) {
	    errno = 0;
	    uint32_t chapter = (uint32_t)strtoul(trimmed.data, (char ** restrict)(trimmed.data + trimmed.count), 10);
	    if (errno != 0) {
	      printf("%s", strerror(errno));
	      sleep(5);
	      continue;
	    }

	    tree->chapter = chapter;
	  }

	  continue;
	}

	if (strcmp(field, "volume") == 0 || strcmp(field, "volumes") == 0) {
	  printf("tree->volume = ");
	  flush();

	  if (!ansi_term_read_line(&read_data)) {
	    sleep(5);
	    continue;
	  }

	  trimmed = sv_trim(read_data);
	  if (trimmed.count) {
	    errno = 0;
	    char *end_ptr = (char*)(trimmed.data + trimmed.count);
	    uint32_t volume = (uint32_t)strtoul(trimmed.data, &end_ptr, 10);
	    if (errno != 0) {
	      printf("%s", strerror(errno));
	      sleep(5);
	      continue;
	    }

	    tree->volume = volume;
	  }

	  continue;
	}

	last_error = nob_temp_sprintf("Unknown field: %s", field);
      }
      
    } break;

    case 'l': {
      ansi_term_clear_screen();
      display_mori_tree_short_list();
    } break;

    case 'i': {
      ansi_term_clear_screen();

      size_t i = 0;
      if (!read_index_from_stdin("Index =", &i)) break;
      if (mori.count <= i) {
	nob_log(ERROR, "OutOfBounds: Trying to access index %zu in list of length %zu", i, mori.count);
	break;
      }

      display_tree_full(i, NULL);
    } break;

  case 's': {
    ansi_term_clear_screen();

    String_View sv = {0};

    if (!ansi_term_read_line(&sv)) {
      break;
    }
    size_t save_point = temp_save();
    const char *search = ntemp_sv_ascii_to_lower(sv_trim(sv));
    size_t found = 0;
    ansi_term_printn("╓<Search_Results>");
    for (size_t i = 0; i < mori.count; ++i) {
      Mori_Tree *tree = mori.items + i;
      const char *lowered_name = ntemp_sv_ascii_to_lower(tree->name);
      if (zstr_includes_zstr(lowered_name, search)) {
	ansi_term_printfn("╟──◈ Index %zu", i);
	display_tree_short(i, "║      ");
	found++;
      }
    }

    // Free memory
    free((void*)sv.data);
    temp_rewind(save_point);

    ansi_term_printfn("╙ Mori_Tree found[%zu];", found);
  } break;

  case ' ':
  case '\t':
  case '\n':
    break;

    default: {
      if (action != '\n') ansi_term_printfn("Unknown command: '%c'", action);
    } break;
  }

  flush();
  ansi_term_read_line(NULL);

  return false;
}

void load_morimori_file(String_Builder *sb, const char *file_path) {
  nob_log(INFO, "Checking for morimori file '%s'...", file_path);
  if (!nob_file_exists(file_path)) {
    write_entire_file(file_path, mori_header, MORI_HEADER_SIZE);
    nob_log(INFO, "Created base morimori file!");
  } else {
    read_morimori_file(sb, file_path);
  }
}

int main(int argc, char **argv) {
  shift(argv, argc);

  nob_minimal_log_level = NOB_WARNING;

  String_Builder sb = {0};
  const char *morimori_file_path = get_morimori_file_path();

  if (argc > 0) {
    char *arg = shift(argv, argc);
    if (strcmp(arg, "version") == 0) {
      printf("Program: v"MORI_FULL_VERSION"\n");
      printf("mori-mori: 0x%02x\n", MORI_VERSION);
      return 0;
    }

    if (strcmp(arg, "list") == 0) {
      load_morimori_file(&sb, morimori_file_path);
      display_mori_tree_short_list();
      return 0;
    }

    if (strcmp(arg, "list-full") == 0) {
      load_morimori_file(&sb, morimori_file_path);
      display_mori_tree_full_list();
      return 0;
    }

    if (strcmp(arg, "search") == 0) {
      if (argc == 0) {
	nob_log(ERROR, "Missing search term(s)");
	printf("Usage: mori search <search-terms...>\n");
	return 1;
      }

      load_morimori_file(&sb, morimori_file_path);

      String_Builder search_sb = {0};
      while (argc > 0) {
	char *term = shift(argv, argc);
	if (search_sb.count) da_append(&search_sb, ' ');
	sb_append_cstr(&search_sb, term);
      }
      String_View sv = sb_to_sv(search_sb);
      const char *search = ntemp_sv_ascii_to_lower(sv);
      size_t found = 0;
      ansi_term_printn("╓<Search_Results>");
      for (size_t i = 0; i < mori.count; ++i) {
	Mori_Tree *tree = mori.items + i;
	const char *lowered_name = ntemp_sv_ascii_to_lower(tree->name);
	if (zstr_includes_zstr(lowered_name, search)) {
	  ansi_term_printfn("╟──◈ Index %zu", i);
	  display_tree_short(i, "║      ");
	  found++;
	}
      }

      ansi_term_printfn("╙ Mori_Tree found[%zu];", found);
      flush();

      return 0;
    }
  }

  load_morimori_file(&sb, morimori_file_path);
  cmd.items = malloc(GLOBAL_CMD_INIT_CAP);
  cmd.capacity = GLOBAL_CMD_INIT_CAP;

  ansi_term_start();

  char action = 0;
  int ch;
  while (true) {
    ansi_term_clear_screen();

    display_actions_menu();
    action = 0;

    while (action == 0 || action == ' ' || action == '\t' || action == '\n') {
      String_View sv = {0};
      if (!ansi_term_read_line(&sv)) {
	printf(ANSI_TERM_DISABLE_ALT_BUFFER);
	flush();
	return 1;
      }

      String_View trimmed = sv_trim_left(sv);
      if (trimmed.count == 0) {
	action = 0;
	continue;
      }
      action = trimmed.data[0];
    }

    if (handle_action(&sb, action)) break;
  }

  ansi_term_end();

  nob_log(INFO, "Saving morimori file...");
  if (write_morimori_file(morimori_file_path)) {
    nob_log(INFO, "Saved your 森!");
  } else {
    nob_log(ERROR, "Failed to save your 森!");
  }
  
  return 0;
}

#define ANSI_TERM_IMPLEMENTATION
#include "ansi_term.h"

#define EXTENDED_SV_IMPLEMENTATION
#include "ext_sv.h"

#define NOB_IMPLEMENTATION
#include "nob.h"


