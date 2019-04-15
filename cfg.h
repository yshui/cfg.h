// SPDX-License-Identifier: BSL-1.0
#pragma once

#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================== Utilities ===================================

#define auto __auto_type
#define min(a, b) ((a) < (b) ? (a) : (b))

struct string_fragment {
	struct string_fragment *next;
	long capacity;
	long len;
	char data[];
};

struct string_builder {
	struct string_fragment *head, *tail;
	long len;
};

struct string_fragment *string_fragment_new(long cap) {
	struct string_fragment *f =
	    (struct string_fragment *)calloc(1, sizeof(*f) + cap);
	f->capacity = cap;
	f->len = 0;
	f->data[0] = 0;
	f->next = NULL;
	return f;
}

void string_builder_push(struct string_builder *sb, const char *str, long len) {
	sb->len += len;

	if (!sb->head) {
		sb->head = sb->tail = string_fragment_new(len > 64 ? len : 64);
	}

	int pos = 0;
	if (sb->tail->len < sb->tail->capacity) {
		pos = min(len, sb->tail->capacity - sb->tail->len);
		memcpy(sb->tail->data + sb->tail->len, str, pos);
		len -= pos;
		sb->tail->len += pos;
	}

	if (len == 0) {
		return;
	}

	sb->tail->next = string_fragment_new(len > 64 ? len : 64);
	sb->tail = sb->tail->next;
	sb->tail->len = len;
	memcpy(sb->tail->data, str + pos, len);
}

char *string_builder_finish(struct string_builder *sb) {
	auto ret = (char *)malloc(sb->len + 1);
	long pos = 0;
	auto i = sb->head;
	while (i) {
		auto next = i->next;
		memcpy(ret + pos, i->data, i->len);
		pos += i->len;
		free(i);
		i = next;
	}
	ret[sb->len] = 0;
	sb->head = sb->tail = NULL;
	sb->len = 0;
	return ret;
}

void string_builder_destroy(struct string_builder *sb) {
	auto i = sb->head;
	while (i) {
		auto next = i->next;
		free(i);
		i = next;
	}
}

#define scoped_string_builder                                                       \
	struct string_builder __attribute__((cleanup(string_builder_destroy)))

// =================================== I/O ====================================

struct cfg_io_handle {
	int (*getchar)(struct cfg_io_handle *handle);
	int (*seek)(struct cfg_io_handle *handle, long offset, int whence);
	int error;
	bool eof : 1;
};

/// An io handle for reading from libc FILE *
struct cfg_file_io_handle {
	struct cfg_io_handle base;
	FILE *f;
};

int cfg_file_io_getchar(struct cfg_io_handle *h) {
	struct cfg_file_io_handle *fh = (void *)h;
	int ret = fgetc(fh->f);
	h->error = ferror(fh->f) ? EIO : 0;
	h->eof = feof(fh->f);
	clearerr(fh->f);
	return ret == EOF ? -1 : ret;
}

int cfg_file_io_seek(struct cfg_io_handle *h, long offset, int origin) {
	struct cfg_file_io_handle *fh = (void *)h;
	int ret = fseek(fh->f, offset, origin);
	h->eof = feof(fh->f);
	if (ret) {
		h->error = errno;
	} else {
		h->error = 0;
	}
	clearerr(fh->f);
	return ret;
}

struct cfg_string_io_handle {
	struct cfg_io_handle base;
	long offset;
	long length;
	char str[];
};

int cfg_string_io_getchar(struct cfg_io_handle *h) {
	auto sh = (struct cfg_string_io_handle *)h;
	if (sh->offset >= sh->length) {
		h->eof = true;
		return -1;
	}
	return sh->str[sh->offset++];
}

int cfg_string_io_seek(struct cfg_io_handle *h, long offset, int origin) {
	auto sh = (struct cfg_string_io_handle *)h;
	long base = 0;
	switch (origin) {
	case SEEK_CUR:
		base = sh->offset;
		break;
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_END:
		base = sh->length;
		break;
	default:
		h->error = EINVAL;
		return -1;
	}
	if (base + offset < 0) {
		h->error = EINVAL;
		return -1;
	}
	if (base + offset > sh->length) {
		sh->offset = sh->length;
	} else {
		sh->offset = base + offset;
	}
	h->eof = false;
	return 0;
}

struct cfg_string_io_handle *cfg_string_io_new(const char *str, long length) {
	struct cfg_string_io_handle *ret =
	    (struct cfg_string_io_handle *)malloc(sizeof(*ret) + length);
	ret->base = (struct cfg_io_handle){.getchar = cfg_string_io_getchar,
	                                   .seek = cfg_string_io_seek,
	                                   .eof = false,
	                                   .error = 0};
	ret->length = length;
	ret->offset = 0;
	memcpy(ret->str, str, length);
	return ret;
}

/// Create an io handle from a file
struct cfg_file_io_handle cfg_open_file(const char *path) {
	FILE *ret = fopen(path, "r");
	if (!ret) {
		return (struct cfg_file_io_handle){
		    .base = {.error = errno},
		    .f = NULL,
		};
	}
	return (struct cfg_file_io_handle){
	    .base = {.error = 0,
	             .getchar = cfg_file_io_getchar,
	             .seek = cfg_file_io_seek,
	             .eof = false},
	    .f = ret,
	};
}

// ================================= Parsers ==================================

struct cfg_comment {
	int nlines;
	char **line;
};

void parse_whitespace(struct cfg_io_handle *h) {
	while (true) {
		int ch = h->getchar(h);
		if (ch < 0) {
			return;
		}
		if (ch != ' ' && ch != '\n' && ch != '\t' && ch != '\r' &&
		    ch != '\f' && ch != '\v') {
			h->seek(h, -1, SEEK_CUR);
			return;
		}
	}
}

/// Parse a newline, return true if a newline is parsed, return false otherwise or
/// on error
bool parse_newline(struct cfg_io_handle *h) {
	int ch = h->getchar(h);
	if (ch == '\n') {
		return true;
	}
	if (ch == '\r') {
		int ch2 = h->getchar(h);
		if (ch2 < 0) {
			if (h->eof) {
				h->seek(h, -1, SEEK_CUR);
			}
			return false;
		}
		if (ch2 == '\n') {
			return true;
		}
		h->seek(h, -2, SEEK_CUR);
	}
	return false;
}

bool parse_eof(struct cfg_io_handle *h) {
	int ch = h->getchar(h);
	if (h->eof) {
		return true;
	}
	if (ch >= 0) {
		h->seek(h, -1, SEEK_CUR);
	}
	return false;
}

struct cfg_comment *parse_comment(struct cfg_io_handle *h) {
	int ch = h->getchar(h);
	if (h->eof || ch < 0) {
		return NULL;
	}
	if (ch != '#') {
		h->seek(h, -1, SEEK_CUR);
		return NULL;
	}

	int line_cap = 5, nlines = 0;
	char **line = (char **)calloc(5, sizeof(*line));
	scoped_string_builder sb = {0};
	while (true) {
		__label__ newline;
		if (parse_newline(h)) {
			goto newline;
		}
		ch = h->getchar(h);
		if (ch < 0) {
			goto newline;
		}
		string_builder_push(&sb, (char[]){(char)ch}, 1);
		continue;
	newline:
		if (nlines == line_cap) {
			line = (char **)reallocarray(line, nlines + 1, sizeof(*line));
		}
		line[nlines++] = string_builder_finish(&sb);
		parse_whitespace(h);
		ch = h->getchar(h);
		if (ch >= 0 && ch != '#') {
			h->seek(h, -1, SEEK_CUR);
			break;
		}
		if (ch < 0) {
			break;
		}
	}
	struct cfg_comment *ret = (struct cfg_comment *)malloc(sizeof(*ret));
	ret->nlines = nlines;
	ret->line = line;
	return ret;
}

// Cleanup

#undef auto
#undef min
#undef _DEFAULT_SOURCE
#undef _GNU_SOURCE
