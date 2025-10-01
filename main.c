#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


#define NOB_FREE(ptr) do { if (ptr) { free((void*)ptr); ptr = NULL; } } while(0)
#define NOB_STRIP_PREFIX
#include "nob.h"

#undef nob_sb_free
#undef sb_free
#define nob_sb_free(sb) sb_free(&sb)

void sb_free(Nob_String_Builder *sb) {
  if (sb->items) free(sb->items);
  memset(sb, 0, sizeof(Nob_String_Builder));
}

#include "ext_sv.h"
#include "ansi_term.h"

#define MORI_FILE_NAME "mori-mori"
#define MORI_VERSION 0
#define MORI_FULL_VERSION "0.1.0"

#define list_remove(l, index)                                \
do {                                                         \
  if ((l)->count > 0 && index < (l)->count) {  \
    for (size_t i = index; i < (l)->count - 1; ++i) {        \
      (l)->items[i] = (l)->items[i + 1];                     \
    }                                                        \
    (l)->count--;                                            \
  }                                                          \
} while (0)

#define flush() fflush(stdout)

typedef unsigned char byte_t;

typedef struct {
  String_Builder *buffer;
  size_t index;
  size_t length;
} Buffered_String_View;

#define BufSV_Fmt      "%.*s"
#define BufSV_Arg(bsv) (int) (bsv).length, ((bsv).buffer->items + (bsv).index)
#define BufSV_Arg_Clamp(bsv, max) (int) ((bsv).length > (max) ? (max) : (bsv).length), ((bsv).buffer->items + (bsv).index)

#define bufsv_to_sv(bsv) nob_sv_from_parts((bsv).buffer->items + (bsv).index, (bsv).length)

typedef struct {
  Buffered_String_View name;
  Buffered_String_View url;
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
  Nob_String_Builder buffer;

  Mori_Tree *items;
  size_t count;
  size_t capacity;
} Mori_Mori;

#define MORI_HEADER_SIZE 6
const byte_t mori_header[MORI_HEADER_SIZE] = { 'M', 'O', 'R', 'I', 69, MORI_VERSION };

Mori_Mori mori = {0};

const char *get_morimori_file_path() {
  const char *home_path = getenv("HOME");
  return nob_temp_sprintf("%s/.config/%s", home_path, MORI_FILE_NAME);
}

bool get_v0_mori_tree_from_bytes(String_Builder *buffer, size_t *offset, Mori_Tree *tree, bool *errored) {
  *errored = false;
  if (*offset > buffer->count) {
    nob_log(ERROR, "Internal Error: Attempting to reach outside of the allocated buffer");
    *errored = true;
    return false;
  }
  size_t bytes_len = buffer->count - (*offset);
  if (bytes_len == 0) return false;
  // nob_log(INFO, "Reading leftover bytes: %zu", bytes_len);
  if (bytes_len == 1) {
    char b = buffer->items[*offset];
    nob_log(INFO, "Hit a single byte: %d -> '%c'", b, b);
    if (b == '\n') {
      buffer->count--;
      return false;
    }
  }

  union {
    char buf[sizeof(uint32_t)];
    uint32_t val;
  } num;
  num.val = 0;

  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected name length as a ui32 at the start of a mori_tree");
      *errored = true;
      return false;
    }
    num.buf[i] = buffer->items[(*offset) + i];
    bytes_len--;
  }
  memmove(buffer->items + (*offset), buffer->items + (*offset + sizeof(uint32_t)), bytes_len);
  buffer->count -= sizeof(uint32_t);

  uint32_t name_len = num.val;
  if (bytes_len == 0 || bytes_len < name_len) {
    nob_log(ERROR, "Malformed Mori Tree: Not enough data exists in file to read name");
    *errored = true;
    return false;
  }
  if (name_len == 0) return false;
  tree->name = (Buffered_String_View) { .buffer = buffer, .index = *offset, .length = (size_t)name_len };
  *offset += name_len;
  bytes_len -= name_len;
  nob_log(INFO, "Loading manga: '"BufSV_Fmt"'...", BufSV_Arg(tree->name));

  num.val = 0;
  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected url length as a ui32 after name");
      *errored = true;
      return false;
    }
    num.buf[i] = buffer->items[(*offset) + i];
    bytes_len--;
  }
  memmove(buffer->items + (*offset), buffer->items + (*offset + sizeof(uint32_t)), bytes_len);
  buffer->count -= sizeof(uint32_t);

  uint32_t url_len = num.val;
  if (bytes_len == 0 || bytes_len < name_len) {
    nob_log(ERROR, "Malformed Mori Tree: Not enough data exists in file to read url");
    *errored = true;
    return false;
  }
  tree->url = (Buffered_String_View) { .buffer = buffer, .index = *offset, .length = (size_t)url_len };
  *offset += url_len;
  bytes_len -= url_len;


  num.val = 0;
  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected chapters count as a ui32 after url");
      *errored = true;
      return false;
    }
    num.buf[i] = buffer->items[(*offset) + i];
    bytes_len--;
  }
  memmove(buffer->items + (*offset), buffer->items + (*offset + sizeof(uint32_t)), bytes_len);
  buffer->count -= sizeof(uint32_t);

  uint32_t chapters_count = num.val;
  tree->chapter = chapters_count;
  nob_log(INFO, "    Chapter: %u", tree->chapter);

  num.val = 0;
  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (bytes_len == 0) {
      nob_log(ERROR, "Malformed Mori Tree: Expected volumes count as a ui32 after chapters");
      *errored = true;
      return false;
    }
    num.buf[i] = buffer->items[(*offset) + i];
    bytes_len--;
  }
  memmove(buffer->items + (*offset), buffer->items + (*offset + sizeof(uint32_t)), bytes_len);
  buffer->count -= sizeof(uint32_t);

  uint32_t volumes_count = num.val;
  tree->volume = volumes_count;
  nob_log(INFO, "    Volume: %u", tree->volume);

  return true;
}

bool read_morimori_file(String_Builder *sb, const char *morimori_file_path) {
  nob_log(INFO, "Reading morimori file...");
  sb->count = 0;
  if (!read_entire_file(morimori_file_path, sb)) {
    nob_log(WARNING, "Failed to read morimori file! Data in it will be ignored");
    return false;
  }
  nob_log(INFO, "Bytes read: %zu", sb->count);

  if (sb->count < MORI_HEADER_SIZE) {
    nob_log(ERROR, "morimori file is missing header");
    return false;
  }

  for (size_t i = 0; i < MORI_HEADER_SIZE - 1; ++i) {
    byte_t b = sb->items[i];
    if (b != mori_header[i]) {
      nob_log(ERROR, "Invalid morimori header");
      return false;
    }
  }

  byte_t v = *(sb->items + (MORI_HEADER_SIZE - 1));
  if (v > MORI_VERSION) {
    nob_log(ERROR, "Invalid version in header");
    return false;
  }

  memmove(sb->items, sb->items + MORI_HEADER_SIZE, sb->count - MORI_HEADER_SIZE);
  sb->count -= MORI_HEADER_SIZE;
  size_t offset = 0;

  switch (v) {
  case 0:
    nob_log(INFO, "Loading morimori v0...");
    while (sb->count - offset) {
      Mori_Tree tree = {0};
      bool errored = false;
      if (!get_v0_mori_tree_from_bytes(sb, &offset, &tree, &errored)) {
	if (errored) nob_log(NOB_ERROR, "Failed to read v0 mori tree bytes");
	return !errored;
      }
      if (tree.name.length) da_append(&mori, tree);
    }
    return true;

  default:
    nob_log(ERROR, "Unhandled version %d", (int)v);
    return false;
  }
}

// TODO: Write directly to a file instead of throwing everything into the heap before writing everything at once
bool write_morimori_file(const char *file_path) {
  Nob_String_Builder content_sb = {0};
  bool result = true;

  sb_append_buf(&content_sb, mori_header, MORI_HEADER_SIZE);

  da_foreach(Mori_Tree, it, &mori) {
    char *nbytes = NULL;
    uint32_t value = 0;

    value = (uint32_t)it->name.length;
    nbytes = (char*)&value;
    sb_append_buf(&content_sb, nbytes, sizeof(uint32_t));
    if (it->name.length > 0) sb_append_buf(&content_sb, it->name.buffer->items + it->name.index, it->name.length);

    value = (uint32_t)it->url.length;
    nbytes = (char*)&value;
    sb_append_buf(&content_sb, nbytes, sizeof(uint32_t));
    if (it->url.length > 0) sb_append_buf(&content_sb, it->url.buffer->items + it->url.index, it->url.length);

    sb_append_buf(&content_sb, it->chapter_bytes, sizeof(uint32_t));

    sb_append_buf(&content_sb, it->volume_bytes, sizeof(uint32_t));
  }

  result = write_entire_file(file_path, content_sb.items, content_sb.count);

  sb_free(&content_sb);
  return result;
}

// TODO: Also pass length so the user can go from the end by providing a negative index
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
  // TODO: Transform it to an sv and trim it and handle negative sign
  buf[(size_t)n >= cap ? cap - 1 : (size_t)n] = 0;

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
  String_View name = bufsv_to_sv(tree->name);
  const size_t max_name_char_length = 35;

  if (!prefix) {
    ansi_term_printfn("Mori_Tree :: struct {");
    ansi_term_printfn("  .Index   = %zu;", i);
    if (tree->name.length > max_name_char_length) {
      ansi_term_printfn("  .Name    = \"%.*s...\";", max_name_char_length, name.data);
    } else {
      ansi_term_printfn("  .Name    = \""SV_Fmt"\";", SV_Arg(name));
    }
    ansi_term_printfn("  .Chapter = %zu;", tree->chapter);
    ansi_term_printfn("}");
    return;
  }

  ansi_term_printfn("%sMori_Tree :: struct {", prefix);
  ansi_term_printfn("%s  .Index   = %zu;", prefix, i);
  if (tree->name.length > max_name_char_length) {
    // TODO: Actually split on a space or dash or colon as this can cut mid-word for not so nice views
    ansi_term_printfn("%s  .Name    = \"%.*s...\";", prefix, max_name_char_length, name.data);
  } else {
    ansi_term_printfn("%s  .Name    = \""SV_Fmt"\";", prefix, SV_Arg(name));
  }
  ansi_term_printfn("%s  .Chapter = %zu;", prefix, tree->chapter);
  ansi_term_printfn("%s}", prefix);
}

void display_tree_full(size_t i, const char *prefix) {
  Mori_Tree *tree = mori.items + i;

  if (!prefix) {
    ansi_term_printfn("Mori_Tree :: struct {");
    ansi_term_printfn("  .Index   = %zu;", i);
    ansi_term_printfn("  .Name    = \""BufSV_Fmt"\";", BufSV_Arg(tree->name));
    if (tree->url.length == 0 || *(tree->url.buffer->items + tree->url.index) == 0) {
      ansi_term_printn("  .Url     = None;");
    } else {
      ansi_term_printfn("  .Url     = \""BufSV_Fmt"\";", BufSV_Arg(tree->url));
    }
    ansi_term_printfn("  .Chapter = %zu;", tree->chapter);
    ansi_term_printfn("  .Volume  = %zu;", tree->volume);
    ansi_term_printfn("}");
    return;
  }

  ansi_term_printfn("%sMori_Tree :: struct {", prefix);
  ansi_term_printfn("%s  .Index   = %zu;", prefix, i);
  ansi_term_printfn("%s  .Name    = \""BufSV_Fmt"\";", prefix, BufSV_Arg(tree->name));
    if (tree->url.length == 0 || *(tree->url.buffer->items + tree->url.index) == 0) {
      ansi_term_printfn("%s  .Url     = None;", prefix);
    } else {
      ansi_term_printfn("%s  .Url     = \""BufSV_Fmt"\";", prefix, BufSV_Arg(tree->url));
    }
  ansi_term_printfn("%s  .Chapter = %zu;", prefix, tree->chapter);
  ansi_term_printfn("%s  .Volume  = %zu;", prefix, tree->volume);
  ansi_term_printfn("%s}", prefix);
}

#define display_mori_tree_short_list() display_mori_tree_short_list_offset(0)
void display_mori_tree_short_list_offset(size_t offset) {
  ansi_term_printn("╓─<Your Manga Forest>");
  for (size_t i = offset; i < mori.count; ++i) {
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
      if (tree->url.length) {
	url = true;
	cmd_append(&cmd, "wl-copy", nob_temp_sprintf(BufSV_Fmt, BufSV_Arg(tree->url)));
	ok = cmd_run(&cmd);
      } else {
	url = false;
	cmd_append(&cmd, "wl-copy", nob_temp_sprintf(BufSV_Fmt, BufSV_Arg(tree->name)));
	ok = cmd_run(&cmd);
      }
      nob_temp_rewind(save_point);

      if (ok) {
	nob_log(INFO, "Copied %s for "BufSV_Fmt, url ? "url" : "name", BufSV_Arg(tree->name));
      } else {
	nob_log(ERROR, "Failed to copy %s for "BufSV_Fmt, url ? "url" : "name", BufSV_Arg(tree->name));
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
      size_t buffer_index = sb->count;
      sb_append_buf(sb, trimmed.data, trimmed.count);
      tree.name = (Buffered_String_View) { .buffer = sb, .index = buffer_index, .length = trimmed.count, };

      printf("Url :: ");
      flush();
      n = read(STDIN_FILENO, buf, cap) - 1;
      if (n > 0) {
	trimmed = sv_trim((String_View) { .count = n, .data = buf });
	buffer_index = sb->count;
	sb_append_buf(sb, trimmed.data, trimmed.count);
	tree.url = (Buffered_String_View) { .buffer = sb, .index = buffer_index, .length = trimmed.count, };
      }

      printf("Chapter :: ");
      flush();
      n = read(STDIN_FILENO, buf, cap);
      buf[(size_t)n >= cap ? cap - 1 : (size_t)n] = 0;
      tree.chapter = (uint32_t) atoi(buf);

      if (tree.chapter > 4) {
	printf("Volume :: ");
	flush();
	n = read(STDIN_FILENO, buf, cap);
	buf[(size_t)n >= cap ? cap - 1 : (size_t)n] = 0;
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
      size_t save_point = nob_temp_save();
      String_View read_data = {0}, trimmed = {0};
      char *last_error = NULL;
      bool is_editing = true;
      while (is_editing) {
	ansi_term_clear_screen();

	if (last_error) {
	  ansi_term_printfn("ERROR: %s", last_error);
	  last_error = NULL;
	}
	nob_temp_rewind(save_point);
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

	if ((strcmp(field, "quit") == 0) || (strcmp(field, "q") == 0)) {
	  is_editing = false;
	  continue;
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
	    if (trimmed.count <= tree->name.length) {
	      for (size_t i = 0; i < trimmed.count; ++i) {
		tree->name.buffer->items[tree->name.index + i] = trimmed.data[i];
	      }
	      for (size_t i = trimmed.count; i < tree->name.length; ++i) {
		tree->name.buffer->items[tree->name.index + i] = 0;
	      }
	    } else {
	      size_t buffer_index = sb->count;
	      sb_append_buf(sb, trimmed.data, trimmed.count);
	      tree->name = (Buffered_String_View){ .buffer = sb, .index = buffer_index, .length = trimmed.count };
	    }
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
	    if (trimmed.count <= tree->url.length) {
	      for (size_t i = 0; i < trimmed.count; ++i) {
		tree->url.buffer->items[tree->url.index + i] = trimmed.data[i];
	      }
	      for (size_t i = trimmed.count; i < tree->url.length; ++i) {
		tree->url.buffer->items[tree->url.index + i] = 0;
	      }
	    } else {
	      size_t buffer_index = sb->count;
	      sb_append_buf(sb, trimmed.data, trimmed.count);
	      tree->url = (Buffered_String_View){ .buffer = sb, .index = buffer_index, .length = trimmed.count };
	    }
	  } else if (tree->url.length > 0) {
	    memset(tree->url.buffer->items + tree->url.index, 0, tree->url.length);
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
      nob_temp_rewind(save_point);
      
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
      const char *lowered_name = ntemp_sv_ascii_to_lower(bufsv_to_sv(tree->name));
      if (zstr_includes_zstr(lowered_name, search)) {
	ansi_term_printfn("╟──◈ Index %zu", i);
	display_tree_short(i, "║      ");
	found++;
      }
    }

    // Free memory
    NOB_FREE(sv.data);
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

bool load_morimori_file(String_Builder *sb, const char *file_path) {
  nob_log(INFO, "Checking for morimori file '%s'...", file_path);
  if (!nob_file_exists(file_path)) {
    if (!write_entire_file(file_path, mori_header, MORI_HEADER_SIZE)) return false;
    nob_log(INFO, "Created base morimori file!");
    return true;
  }

  if (!read_morimori_file(sb, file_path)) return false;
  return true;
}

int main(int argc, char **argv) {
  int result = 0;
  shift(argv, argc);

  nob_minimal_log_level = NOB_WARNING;

  const char *morimori_file_path = get_morimori_file_path();
  // printf("Mori_Header :: ");
  // for (const byte_t *b = mori_header; b < mori_header + MORI_HEADER_SIZE; ++b) printf(" 0x%02x", *b);
  // printf("\n");

  if (argc > 0) {
    char *arg = shift(argv, argc);
    if (strcmp(arg, "version") == 0) {
      printf("Program: v"MORI_FULL_VERSION"\n");
      printf("mori-mori: 0x%02x\n", MORI_VERSION);
      nob_return_defer(0);
    }

    if (strcmp(arg, "list") == 0) {
      load_morimori_file(&mori.buffer, morimori_file_path);
      display_mori_tree_short_list();
      nob_return_defer(0);
    }

    if (strcmp(arg, "list-full") == 0) {
      load_morimori_file(&mori.buffer, morimori_file_path);
      display_mori_tree_full_list();
      nob_return_defer(0);
    }

    if (strcmp(arg, "search") == 0) {
      if (argc == 0) {
	nob_log(ERROR, "Missing search term(s)");
	printf("Usage: mori search <search-terms...>\n");
	nob_return_defer(1);
      }

      load_morimori_file(&mori.buffer, morimori_file_path);

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
	const char *lowered_name = ntemp_sv_ascii_to_lower(bufsv_to_sv(tree->name));
	if (zstr_includes_zstr(lowered_name, search)) {
	  ansi_term_printfn("╟──◈ Index %zu", i);
	  display_tree_short(i, "║      ");
	  found++;
	}
      }

      ansi_term_printfn("╙ Mori_Tree found[%zu];", found);
      flush();

      nob_return_defer(0);
    }
  }

  if (!load_morimori_file(&mori.buffer, morimori_file_path)) return 1;
  cmd.items = malloc(GLOBAL_CMD_INIT_CAP);
  cmd.capacity = GLOBAL_CMD_INIT_CAP;

  ansi_term_start();

  char action = 0;
  while (true) {
    ansi_term_clear_screen();

    display_actions_menu();
    action = 0;

    while (action == 0 || action == ' ' || action == '\t' || action == '\r' || action == '\n') {
      String_View sv = {0};
      if (!ansi_term_read_line(&sv)) {
	printf(ANSI_TERM_DISABLE_ALT_BUFFER);
	flush();
	nob_return_defer(1);
      }

      String_View trimmed = sv_trim_left(sv);
      if (trimmed.count == 0) {
	action = 0;
	continue;
      } else {
	action = trimmed.data[0];
      }
      NOB_FREE(sv.data);
    }

    if (handle_action(&mori.buffer, action)) break;
  }

  ansi_term_end();

  nob_log(INFO, "Saving morimori file...");
  if (write_morimori_file(morimori_file_path)) {
    nob_log(INFO, "Saved your 森!");
  } else {
    nob_log(ERROR, "Failed to save your 森!");
  }

defer:
  sb_free(&mori.buffer);
  return result;
}

#define ANSI_TERM_IMPLEMENTATION
#include "ansi_term.h"

#define EXTENDED_SV_IMPLEMENTATION
#include "ext_sv.h"

#define NOB_IMPLEMENTATION
#include "nob.h"


