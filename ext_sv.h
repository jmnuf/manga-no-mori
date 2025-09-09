#ifndef _EXTENDED_STRING_VIEW_H
#define _EXTENDED_STRING_VIEW_H
#include <stdbool.h>
#include <string.h>
#include "nob.h"

const char *ntemp_sv_ascii_to_lower(Nob_String_View sv);
const char *ntemp_zstr_ascii_to_lower(const char * zstr);

Nob_String_View ntemp_sv_dup_zstr(const char *zstr);
Nob_String_View ntemp_sv_dup_buf(const char *buf, size_t buf_size);

Nob_String_View ntemp_sv_replace_char_with(Nob_String_View sv, char from, const char *to);

bool zstr_includes_sv(const char *base, Nob_String_View needle_sv);
#define zstr_includes_zstr(base, needle) zstr_includes_sv(base, nob_sv_from_cstr(needle))
bool sv_includes_buf(Nob_String_View sv, const char *needle, size_t needle_size);
#define sv_includes_cstr(base, needle) sv_includes_buf(base, needle, strlen(needle))
#define sv_includes_sv(base, needle) sv_includes_buf(base, (needle).data, (needle).count)

#endif // _EXTENDED_STRING_VIEW_H




#ifdef EXTENDED_SV_IMPLEMENTATION

const char *ntemp_sv_ascii_to_lower(Nob_String_View sv) {
  char *zstr = nob_temp_alloc(sv.count + 1);

  for (size_t i = 0; i < sv.count; ++i) {
    char c = sv.data[i];
    if ('A' <= c && c <= 'Z') {
      c += 32;
    }
    zstr[i] = c;
  }

  return zstr;
}

const char *ntemp_zstr_ascii_to_lower(const char *base) {
  size_t len = strlen(base);
  char *zstr = nob_temp_alloc(len + 1);

  for (size_t i = 0; i < len; ++i) {
    char c = base[i];
    if ('A' <= c && c <= 'Z') {
      c += 32;
    }
    zstr[i] = c;
  }
  zstr[len] = 0;

  return zstr;
}


Nob_String_View ntemp_sv_dup_zstr(const char *zstr) {
  return (Nob_String_View) {
    .data = nob_temp_strdup(zstr),
    .count = strlen(zstr),
  };
}

Nob_String_View ntemp_sv_dup_buf(const char *buf, size_t buf_size) {
  char *data = (char*)memcpy(nob_temp_alloc(buf_size), buf, buf_size);
  return (Nob_String_View) {
    .data = data,
    .count = buf_size,
  };
}


Nob_String_View ntemp_sv_replace_char_with(Nob_String_View sv, char from, const char *to) {
  size_t to_len = strlen(to);
  size_t final_len = 0;

  for (size_t i = 0; i < sv.count; ++i) {
    if (sv.data[i] == from) {
      final_len += to_len;
    } else {
      final_len++;
    }
  }

  char *dest = nob_temp_alloc(final_len + 1);
  size_t j = 0;
  for (size_t i = 0; i < sv.count; ++i) {
    if (sv.data[i] == from) {
      for (size_t k = 0; k < to_len; ++k) dest[j++] = to[k];
    } else {
      dest[j++] = sv.data[i];
    }
  }
  dest[final_len] = 0;

  return (Nob_String_View) {
    .data = dest,
    .count = final_len,
  };
}

Nob_String_View ntemp_sv_replace_substr_with(Nob_String_View sv, const char *from, const char *to) {
  size_t from_len = strlen(from);
  size_t to_len   = strlen(to);
  size_t final_len = 0;

  for (size_t i = 0; i < sv.count; ++i) {
    if (i + from_len <= sv.count) {
      if (memcmp(sv.data + i, from, from_len) == 0) {
	i += from_len - 1;
	final_len += to_len;
	continue;
      }
    }
    final_len++;
  }

  char *dest = nob_temp_alloc(final_len + 1);

  size_t j = 0;
  for (size_t i = 0; i < sv.count; ++i) {
    if (i + from_len <= sv.count) {
      if (memcmp(sv.data + i, from, from_len) == 0) {
	i += from_len - 1;
	for (size_t k = 0; k < to_len; ++k) dest[j++] = to[k];
	continue;
      }
    }

    dest[j++] = sv.data[i];
  }
  dest[final_len] = 0;

  return (Nob_String_View) {
    .data = dest,
    .count = j,
  };
}


bool zstr_includes_sv(const char *base, Nob_String_View needle) {
  size_t base_len = strlen(base);
  if (base_len < needle.count) return false;
  if (base_len == needle.count) {
    for (size_t i = 0; i < base_len; ++i) {
      if (base[i] != needle.data[i]) return false;
    }

    return true;
  }

  for (size_t i = 0; i < base_len - needle.count; ++i) {
    bool found = true;
    for (size_t j = 0; j < needle.count; ++j) {
      if (base[i+j] != needle.data[j]) {
	found = false;
	break;
      }
    }
    if (found) return true;
  }

  return false;
}

bool sv_includes_buf(Nob_String_View base, const char *needle, size_t needle_size) {
  if (base.count < needle_size) return false;
  if (base.count == needle_size) {
    for (size_t i = 0; i < base.count; ++i) {
      if (base.data[i] != needle[i]) return false;
    }
    return true;
  }

  for (size_t i = 0; i < base.count - needle_size; ++i) {
    bool found = true;
    for (size_t j = 0; j < needle_size; ++j) {
      if (base.data[i+j] != needle[j]) {
	found = false;
	break;
      }
    }
    if (found) return true;
  }

  return false;
}

#endif // EXTENDED_SV_IMPLEMENTATION

