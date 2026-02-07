// Copyright (c) 2026 Akritas Akritidis, see LICENSE for license details

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dirent.h>
#include <fcntl.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <fts.h>
#include <liburing.h>

#define FILE_ENTRIES 32
#define FIXED_BUFFER_SIZE 64 * 1024
#define INIT_BFS_QUEUE_CAPACITY 64 * 1024
#define GETENTS_BUFFER_CAPACITY 64 * 1024
#define BINARY_FAST_CHECK_LEN 1 * 1024
#define BINARY_CHECK_LEN 4 * 1024
#define MAX_CONTENT_PATTERNS 12
#define PATTERN_MAX_LEN 1024
#define DEFAULT_PRINT_LIMIT 300

// === types

typedef char check;
typedef char *string;
typedef __mode_t filemode;
typedef __off_t filesize;

//

typedef struct {
	char *start;
	filesize size;
	filesize capacity;
	char owned;
} file_buffer;

typedef struct {
	int fd;
	char path[PATH_MAX];
	filemode mode;
	filesize size;

	file_buffer fixed_buffer;
	file_buffer buffer;
	struct iovec iov;

	char ready;
} file_entry;

//

typedef char pattern_star;

typedef struct {
	char *arg;
	char text[PATTERN_MAX_LEN];
	int len;
} pattern_any;
typedef pattern_any pattern_start;
typedef pattern_any pattern_end;

typedef struct {
	pattern_any start;
	pattern_any end;
} pattern_wrap;

typedef struct {
	char *arg;
	regex_t regex;
} pattern_regex;

typedef struct {
	char type;
#define T_star '.'
#define T_any 'a'
#define T_start 's'
#define T_end 'e'
#define T_wrap 'w'
#define T_regex 'r'
	union {
		pattern_star star;
		pattern_any any;
		pattern_start start;
		pattern_end end;
		pattern_wrap wrap;
		pattern_regex regex;
	} as;
	int index;
	char *match_start;
	char *match_end;
} pattern;

// === state

char **roots = 0;
int roots_count = 0;
int roots_index = 0;

char *bfs_queue_buffer;
size_t bfs_queue_capacity;
char *bfs_queue_start;
char *bfs_queue_end;

file_entry files[FILE_ENTRIES];
int files_count = 0;
char *fixed_buffers;
struct io_uring ring;

pattern content_patterns[MAX_CONTENT_PATTERNS];
int content_patterns_len = 0;

int errors_count = 0;

// === utils

#include "help.c"
#include "mfg.h"

#define str_contains(s, c) (strchr(s, c))
#define str_endswith(s, c) ((s)[strlen(s) - 1] == c)
#define str_equals(s1, s2) (strcmp(s1, s2) == 0)
#define str_is_option(s) ((s)[0] == '-' && (s)[1])

#define implies(a, b) (!(a) || (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define path_dot(x) ((x)[0] == '.' && !(x)[1])
#define path_ddot(x) ((x)[0] == '.' && (x)[1] == '.' && !(x)[2])
#define path_hidden(x) ((x)[0] == '.' && (x)[1])

#define for_each(I, LEN) for (size_t I = 0; I < (LEN); I++)

#define printf_output(fmt, ...) fprintf(stdout, fmt "\n", ##__VA_ARGS__);
#define printf_error(fmt, ...) fprintf(stderr, "mfg: " fmt "\n", ##__VA_ARGS__);
#define printf_error_verbose(fmt, ...) \
	if (option_verbose) fprintf(stderr, "mfg: " fmt "\n", ##__VA_ARGS__);

#define ERROR_INPUT 1
#define ERROR_INTERNAL 2

#define COL(X) ("\e[" X "m")
#define COLOR(X) (option_plain ? "" : (COL(X)))

#define COLOR_PATH COLOR("2;95")
#define COLOR_SEP COLOR("0;34")
#define COLOR_COL COLOR("32")
#define COLOR_DIM COLOR("90")
#define COLOR_RESET COLOR("0")

const char *match_colors[] = {
	COL("1;91"), COL("1;93"), COL("1;94"), COL("1;92"), COL("1;95"), COL("1;96"), //
};
#define COLOR_MATCH_ARRAY(I) (match_colors[(I) % (sizeof(match_colors) / sizeof(void *))])
#define COLOR_MATCH(I) (option_plain ? "" : option_monochrome ? COLOR_MATCH_ARRAY(0) \
															  : COLOR_MATCH_ARRAY(I))

// === options

check option_help = 0;
check option_bfs = 0;
check option_query = 0;
check option_plain = 0;
check option_monochrome = 0;
check option_table = 0;
check option_unhidden = 0;
check option_verbose = 0;
char option_file_type = 'a';
string possible_option_file_type = "afdetb";
string mappings_option_file_type = "afdetb";
char option_name_mode = '-';
string possible_option_name_mode = "bspef";
string mappings_option_name_mode = "sssef";
string option_name_pattern = 0;
check option_name_case = 0;
check option_name_omit = 0;
check option_content_case = 0; // TODO
check option_content_omit = 0;
check option_content_only = 0;
check option_content_multiline = 0; // TODO
check option_content_around = 0;
string possible_option_content_mode = "bspewr";
string mappings_option_content_mode = "sssewr";

check skip_loading = 0;
check dump_files = 0;

// === main

int main(int argc, char *argv[]) {

	int args_error = handle_args(argc, argv);
	if (args_error) return ERROR_INPUT;

	if (option_help) {
		printf("%s", help);
		return 0;
	}

	skip_loading = content_patterns_len == 0 && !str_contains("etb", option_file_type);
	if (!skip_loading) {
		if (init_loading()) return ERROR_INTERNAL;
	}

	if (isatty(STDIN_FILENO)) {
		if (!roots_count) {
			if (paths_handle()) return ERROR_INTERNAL;
		} else {
			for_each(i, roots_count) {
				roots_index = i;
				if (change_dir(roots[i])) continue;
				if (paths_handle()) return ERROR_INTERNAL;
			}
		}
	} else {
		if (paths_read()) return ERROR_INTERNAL;
	}
	handle_last_content_loaded();

	if (errors_count) {
		printf_error("%d access errors occurred", errors_count);
	}

	return 0;
}

// === paths

int paths_handle() {
	if (!option_bfs) {
		if (paths_traverse()) return 1;
	} else {
		if (paths_bfs()) return 1;
	}
	return 0;
}

int change_dir(char *path) {
	if (chdir(path)) {
		errors_count += 1;
		printf_error_verbose("Failed to find '%s'", path);
		return 1;
	}
	return 0;
}

int paths_traverse() {

	char *paths[] = {".", 0};
	FTS *tree = fts_open(paths, FTS_NOCHDIR | (skip_loading ? FTS_NOSTAT : 0), 0);
	if (!tree) return 1;

	while (1) {
		FTSENT *node = fts_read(tree);
		if (!node) break;

		string path = node->fts_path;
		string name = node->fts_name;

		char skip = (option_unhidden && path_hidden(name));

		switch (node->fts_info) {
		case FTS_F:
			if (!skip) {
				filemode mode = skip_loading ? 0 : node->fts_statp->st_mode;
				filesize size = skip_loading ? 0 : node->fts_statp->st_size;
				handle_file(path + 2, name, mode, size);
			}
			break;
		case FTS_D:
			if (skip || skip_directory(name)) {
				fts_set(tree, node, FTS_SKIP);
			} else {
				handle_directory(path + 2, name);
			}
			break;
		case FTS_ERR:
		case FTS_DNR:
			errors_count += 1;
			printf_error_verbose("Error reading '%s'", path);
		default:
		}
	}

	// fts_close(tree);
	return 0;
}

int paths_bfs() {
	bfs_queue_capacity = INIT_BFS_QUEUE_CAPACITY;
	bfs_queue_buffer = calloc(bfs_queue_capacity, 1);
	bfs_queue_buffer[0] = 0;
	bfs_queue_start = bfs_queue_buffer;
	bfs_queue_end = bfs_queue_buffer;

	if (paths_bfs_consume(".")) return 1;
	while (!paths_bfs_deque()) {}
	return 0;
}

int paths_bfs_enqueue(string path) {
	size_t path_len = strlen(path);

	if (bfs_queue_end + path_len + 1 >= bfs_queue_buffer + bfs_queue_capacity) {
		bfs_queue_capacity *= 2;
		char *new_buffer = calloc(bfs_queue_capacity, 1);
		memcpy(new_buffer, bfs_queue_start, bfs_queue_end - bfs_queue_start);
		bfs_queue_end = new_buffer + (bfs_queue_end - bfs_queue_start);
		bfs_queue_start = new_buffer;
		free(bfs_queue_buffer);
		bfs_queue_buffer = new_buffer;
	}

	strcpy(bfs_queue_end, path);
	bfs_queue_end += path_len + 1;

	return 0;
}

int paths_bfs_deque() {
	if (bfs_queue_start[0] == 0) return 1;

	size_t path_len = strlen(bfs_queue_start);
	string path = bfs_queue_start;
	bfs_queue_start += path_len + 1;

	paths_bfs_consume(path);
	return 0;
}

int paths_bfs_consume(string path) {
	char buffer[GETENTS_BUFFER_CAPACITY];
	char path_buffer[PATH_MAX + 2];

	int fd = open(path, O_RDONLY | O_DIRECTORY);

	int root_path_len = strlen(path);
	strcpy(path_buffer, path);
	char *basename = path_buffer + root_path_len + 1;
	basename[-1] = '/';

	int nread;
	while ((nread = syscall(SYS_getdents64, fd, buffer, sizeof(buffer))) > 0) {
		for (int bpos = 0, step = 0; bpos < nread; bpos += step) {

			struct linux_dirent64 {
				unsigned long long d_ino;
				long long d_off;
				unsigned short d_reclen;
				unsigned char d_type;
				char d_name[];
			} *d = (struct linux_dirent64 *)(buffer + bpos);
			step = d->d_reclen;

			string name = d->d_name;
			if (path_dot(name) || path_ddot(name)) continue;

			char skip = (option_unhidden && path_hidden(name));
			if (skip) continue;

			// construct path_buffer
			strcpy(basename, name);

			if (d->d_type == DT_DIR) {
				if (skip || skip_directory(name)) continue;
				handle_directory(path_buffer + 2, name);
				paths_bfs_enqueue(path_buffer);

			} else if (d->d_type == DT_REG) {
				handle_path(path_buffer + 2);
			}
		}
	}
	close(fd);
	return 0;
}

int paths_read() {
	char buffer[PATH_MAX + 2];

	char *line = buffer + 2;
	size_t len = 0;
	ssize_t read;

	while ((read = getline(&line, &len, stdin)) != -1) {
		if (read > 0 && line[read - 1] == '\n') {
			line[read - 1] = '\0';
		}
		if (strlen(line) == 0) continue;

		handle_path(line);
	}

	return 0;
}

char skip_directory(const char *name) {
	return str_equals(name, ".git") ||
		   str_equals(name, "node_modules") ||
		   str_equals(name, "po") ||
		   str_equals(name, "build") ||
		   str_equals(name, "dist");
}

// === handlers

void handle_path(string path) {
	struct stat st;

	if (stat(path, &st) == -1) {
		perror("stat");
		return;
	}

	if (S_ISREG(st.st_mode)) {
		string name = basename_pointer(path);
		filemode mode = st.st_mode;
		filesize size = st.st_size;
		handle_file(path, name, mode, size);

	} else if (S_ISDIR(st.st_mode)) {
		string name = basename_pointer(path);
		handle_directory(path, name);
	}
}

inline string basename_pointer(string path) {
	string sep = strrchr(path, '/');
	if (!sep) return path;
	return sep + 1;
}

void handle_directory(string path, string name) {

	if (option_name_omit) return;
	if (!str_contains("ad", option_file_type)) return;
	if (path_dot(name)) return;
	if (!match_name(name)) return;

	if (content_patterns_len) return;

	print_match_path(path);
}

void handle_file(string path, string name, filemode mode, filesize size) {

	if (!str_contains("afetb", option_file_type)) return;
	if (!implies(option_file_type == 'e', mode & S_IXUSR)) return;
	if (!match_name(name)) return;

	if (content_patterns_len == 0 && !str_contains("tb", option_file_type)) {
		print_match_path(path);
	} else {
		handle_content(path, name, mode, size);
	}
}

// === content

int init_loading() {
	// sizeof(files);

	fixed_buffers = malloc(FILE_ENTRIES * FIXED_BUFFER_SIZE);
	if (!fixed_buffers) {
		printf_error("Out of memory");
		return 1;
	}

	io_uring_queue_init(FILE_ENTRIES, &ring, 0);

	for_each(i, FILE_ENTRIES) {

		file_buffer buffer = {
			.start = fixed_buffers + i * FIXED_BUFFER_SIZE,
			.size = 0,
			.capacity = FIXED_BUFFER_SIZE,
			.owned = 0,
		};
		file_entry file = {
			.fixed_buffer = buffer,
			.buffer = buffer,
			.ready = 1,
		};
		files[i] = file;
	}

	return 0;
}

void loading_submit_file(file_entry *file) {

	file->iov.iov_base = file->buffer.start;
	file->iov.iov_len = file->buffer.capacity;

	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	io_uring_prep_readv(sqe, file->fd, &file->iov, 1, 0);
	io_uring_sqe_set_data(sqe, file);
	io_uring_submit(&ring);
}
file_entry *loading_get_file() {

	struct io_uring_cqe *cqe;
	io_uring_wait_cqe(&ring, &cqe);

	file_entry *file = io_uring_cqe_get_data(cqe);
	file->buffer.size = cqe->res;
	io_uring_cqe_seen(&ring, cqe);

	return file;
}

file_entry *get_ready_file_entry() {
	if (files_count < FILE_ENTRIES) {
		for_each(i, FILE_ENTRIES) {
			if (files[i].ready) {
				files_count += 1;
				return files + i;
			}
		}
	}
	while (1) {
		file_entry *file = handle_content_result();
		if (file) return file;
	}
}

void handle_last_content_loaded() {
	while (files_count) {
		file_entry *file = handle_content_result();
		if (file) files_count -= 1;
	}
}

file_entry *handle_content(string path, string name, filemode mode, filesize size) {

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		errors_count += 1;
		return 0;
	}

	file_entry *file = get_ready_file_entry();
	// file_entry create
	file->ready = 0;
	file->fd = fd;
	file->mode = mode;
	file->size = size;
	file->buffer = file->fixed_buffer;

	if (!roots_count) {
		strcpy(file->path, path);
	} else {
		const char *sep = roots[roots_index][strlen(roots[roots_index]) - 1] == '/' ? "" : "/";
		sprintf(file->path, "%s%s%s", roots[roots_index], sep, path);
	}

	loading_submit_file(file);
	return file;
}

file_entry *handle_content_overflow(file_entry *file) {

	filesize capacity = file->size + 2;
	char *start = malloc(capacity);
	if (!start) {
		printf_error("Out of memory");
		return 0;
	}
	file_buffer buffer = {
		.start = start,
		.size = 0,
		.capacity = capacity,
		.owned = 1,
	};
	file->ready = 0;
	file->buffer = buffer;

	loading_submit_file(file);
	return file;
}

file_entry *handle_content_result() {

	file_entry *file = loading_get_file();

	char *content = file->buffer.start;
	int content_len = 0;

	char overflow = 0;
	if (file->buffer.size < file->buffer.capacity) {
		content_len = file->buffer.size;
	} else {
		overflow = 1;
		content_len = file->buffer.capacity - 1;
	}
	content[content_len] = 0;

	char skip_checks = file->buffer.owned;
	char binary = skip_checks ? 0 : check_binary(content, content_len);

	if (binary) {
		if (option_file_type == 'b') {
			print_match(file);
		}
	} else {
		if (content_patterns_len) {
			if (overflow) {
				if (handle_content_overflow(file)) return 0;
			} else {
				handle_search(file);
			}
		} else if (option_file_type == 't') {
			print_match(file);
		}
	}

	// file_entry dispose
	close(file->fd);
	if (file->buffer.owned) free(file->buffer.start);
	file->ready = 1;

	return file;
}

char check_binary(char *buffer, filesize len) {

	// if (check_binary1(buffer, min(len, BINARY_FAST_CHECK_LEN))) return 1;
	// if (check_binary2(buffer, min(len, BINARY_CHECK_LEN))) return 1;
	if (check_binary1(buffer, min(len, BINARY_CHECK_LEN))) return 1;
	return 0;
}

char check_binary1(char *buffer, filesize len) {
	for (size_t i = 0; i < len; i++) {
		if (buffer[i] == 0) return 1;
	}
	return 0;
}
char check_binary2(char *buffer, filesize len) {
	for (size_t i = 0; i < len; i++) {
		if (buffer[i] <= 8) return 1;
	}
	return 0;
}
char check_binary3(char *buffer, filesize len) {
	for (size_t i = 0; i < len; i++) {
		if (buffer[i] >= 14 && buffer[i] < ' ') return 1;
	}
	return 0;
}

void *memchr_end(void *s, int c, void *e) {
	void *r = memchr(s, c, e - s);
	return r ? r : e;
}

void handle_search(file_entry *file) {

	char *const text = file->buffer.start;
	filesize const text_len = file->buffer.size;
	char *const text_end = text + text_len;

	if (content_patterns_len == 1 && content_patterns[0].type == T_star) {
		if (option_query) {
			print_match(file);
		} else {
			dump_file(file);
		}
		return;
	}

	int around_lines = option_content_around * 2;

	int line_traces_capacity = around_lines * 2;
	char *line_traces[line_traces_capacity];
	int line_traces_index = 0;
	int line_traces_count = 0;

#define LINE_TRACES(X) line_traces[(X) % line_traces_capacity]

	char *cursor = text;
	filesize line = 1;
	char *line_end;
	int pending_around_lines = 0;

	char dump = 0;
	for_each(i, content_patterns_len) {
		pattern *p = content_patterns + i;

		int success = match_pattern(p, cursor, text_end);
		if (success && option_query) {
			print_match(file);
			return;
		} else if (p->type == T_star) {
			dump = 1;
		}
	}

#define ADVANCE_CURSOR                                                                \
	{                                                                                 \
		if (pending_around_lines || dump) {                                           \
			print_search_match_around(file, line, cursor, line_end);                  \
			pending_around_lines -= 1;                                                \
			line_traces_count = 0;                                                    \
		} else if (line_traces_capacity > 0) {                                        \
			LINE_TRACES(line_traces_index++) = cursor;                                \
			LINE_TRACES(line_traces_index++) = line_end;                              \
			line_traces_count = min(line_traces_count + 1, line_traces_capacity / 2); \
		}                                                                             \
		cursor = line_end + 1;                                                        \
		line += 1;                                                                    \
	}

	while (cursor < text_end) {

		pattern *first = 0;
		for_each(i, content_patterns_len) {
			pattern *p = content_patterns + i;

			while (p->match_start && p->match_start < cursor) {
				match_pattern(p, cursor, text_end);
			}

			if (p->match_start && (!first || first->match_start > p->match_start)) {
				first = p;
			}
		}
		if (!first) break;

		while (1) {
			line_end = memchr_end(cursor, '\n', text_end);
			if (line_end > first->match_start) break;
			ADVANCE_CURSOR
		}

		if (line_traces_capacity && line_traces_count) {
			for_each(i, line_traces_count) {
				int offset = line_traces_count - i;
				print_search_match_around(file, line - offset,
										  LINE_TRACES(line_traces_index - offset * 2),
										  LINE_TRACES(line_traces_index - offset * 2 + 1));
			}
			line_traces_count = 0;
		}

		print_search_match(file, line, first->match_start, first->match_end, cursor, line_end, first->index);
		pending_around_lines = around_lines;

		cursor = line_end + 1;
		line += 1;

		match_pattern(first, cursor, text_end);
	}

	while ((pending_around_lines || dump) && cursor < text_end) {
		line_end = memchr_end(cursor, '\n', text_end);
		ADVANCE_CURSOR
	}
}

inline void print_match(file_entry *file) {
	printf_output("%s", file->path);
}
inline void print_match_path(string path) {
	if (!roots_count) {
		printf_output("%s", path);
	} else {
		const char *sep = roots[roots_index][strlen(roots[roots_index]) - 1] == '/' ? "" : "/";
		printf_output("%s%s%s", roots[roots_index], sep, path);
	}
}

void print_search_match(file_entry *file, filesize line, char *result_start, char *result_end, char *line_start, char *line_end, int pi) {

	int print_limit = DEFAULT_PRINT_LIMIT;
	char line_pre[print_limit + 3], line_post[print_limit + 3];

	int pattern_len = (result_end) - (result_start);
	int line_pre_len = (result_start) - (line_start);
	int line_post_len = (line_end) - (result_end);

#define ELLIPSES "..."
#define PRINT_OMITTED(TEMPLATE, ...)                     \
	if (option_name_omit) {                              \
		printf_output(TEMPLATE, ##__VA_ARGS__);          \
	} else if (option_content_omit) {                    \
		printf_output("%s%s%s:%s%ld%s",                  \
					  COLOR_PATH, file->path, COLOR_SEP, \
					  COLOR_COL, line, COLOR_RESET);     \
	} else {                                             \
		const char *sep = option_table ? "\t" : "";      \
		printf_output("%s%s%s:%s%ld%s:%s" TEMPLATE,      \
					  COLOR_PATH, file->path, COLOR_SEP, \
					  COLOR_COL, line, COLOR_SEP,        \
					  sep, ##__VA_ARGS__);               \
	}

	if (option_content_only) {
		PRINT_OMITTED("%s%.*s%s",
					  COLOR_MATCH(pi), pattern_len, result_start,
					  COLOR_RESET)
	} else if (option_plain || line_end - line_start < print_limit) {
		memcpy(line_pre, line_start, line_pre_len);
		memcpy(line_post, line_end - line_post_len, line_post_len);

		PRINT_OMITTED("%s%.*s%s%.*s%s%.*s",
					  COLOR_RESET, line_pre_len, line_pre,
					  COLOR_MATCH(pi), pattern_len, result_start,
					  COLOR_RESET, line_post_len, line_post)
	} else if (result_end - line_start < print_limit) {
		memcpy(line_pre, line_start, line_pre_len);
		line_post_len = print_limit - (result_end - line_start);
		memcpy(line_post, line_end - line_post_len, line_post_len);

		PRINT_OMITTED("%s%.*s%s%.*s%s%.*s"
					  "%s%s%s",
					  COLOR_RESET, line_pre_len, line_pre,
					  COLOR_MATCH(pi), pattern_len, result_start,
					  COLOR_RESET, line_post_len, line_post,
					  COLOR_DIM, ELLIPSES,
					  COLOR_RESET);
	} else {
		int space_pre = (print_limit - pattern_len) / 2;
		line_pre_len = min(line_pre_len, space_pre);
		line_post_len = min(line_post_len, print_limit - pattern_len - line_pre_len);

		memcpy(line_pre, result_start - line_pre_len, line_pre_len);
		memcpy(line_post, result_end, line_post_len);

		PRINT_OMITTED("%s%s%s"
					  "%.*s%s%.*s%s%.*s"
					  "%s%s%s",
					  COLOR_DIM, ELLIPSES,
					  COLOR_RESET, line_pre_len, line_pre,
					  COLOR_MATCH(pi), pattern_len, result_start,
					  COLOR_RESET, line_post_len, line_post,
					  COLOR_DIM, ELLIPSES,
					  COLOR_RESET);
	}
}

void print_search_match_around(file_entry *file, filesize line, char *line_start, char *line_end) {
	print_search_match(file, line, line_start, line_start, line_start, line_end, 0);
}

void dump_file(file_entry *file) {

	char *const text = file->buffer.start;
	filesize const text_len = file->buffer.size;
	char *const text_end = text + text_len;

	char *cursor = text;
	filesize line = 1;
	char *line_end;

	while (cursor < text_end) {
		line_end = memchr_end(cursor, '\n', text_end);

		print_search_match_around(file, line, cursor, line_end);

		cursor = line_end + 1;
		line += 1;
	}
}

// === patterns

#define PATTERN_CAST(X)     \
	}                       \
	if (p->type == T_##X) { \
		pattern_##X *P = &p->as.X;

int init_pattern(pattern *p) {

	if (p->type == 0) {
		PATTERN_CAST(any) {
			P->len = strlen(P->arg);
			strcpy(P->text, P->arg);
		}
		PATTERN_CAST(start) {
			P->len = strlen(P->arg);
			sprintf(P->text, "\n%s", P->arg);
		}
		PATTERN_CAST(end) {
			P->len = strlen(P->arg);
			sprintf(P->text, "%s\n", P->arg);
		}
		PATTERN_CAST(wrap) {
			P->start.len = strlen(P->start.arg);
			P->end.len = strlen(P->end.arg);
			strcpy(P->start.text, P->start.arg);
			strcpy(P->end.text, P->end.arg);
		}
		PATTERN_CAST(regex) {
			int flags = REG_EXTENDED | (option_content_multiline ? 0 : REG_NEWLINE) | (option_content_case ? REG_ICASE : 0);
			int ret = regcomp(&P->regex, P->arg, flags);
			if (ret) {
				printf_error("Could not parse regex '%s'", P->arg);
				return 1;
			}
		}
	}
	return 0;
}

char match_pattern(pattern *p, char *text_start, char *text_end) {
	int text_len = text_end - text_start;

	p->match_start = 0;
	p->match_end = 0;

	if (p->type == 0) {
		PATTERN_CAST(any) {

			char *match = memmem(text_start, text_len, P->text, P->len);
			if (!match) return 0;
			p->match_start = match;
			p->match_end = p->match_start + P->len;
			return 1;
		}
		PATTERN_CAST(start) {

			if (!memcmp(text_start, P->text + 1, P->len)) {
				p->match_start = text_start;
				p->match_end = p->match_start + P->len;
				return 1;
			}
			char *match = memmem(text_start, text_len, P->text, P->len + 1);
			if (!match) return 0;
			p->match_start = match + 1;
			p->match_end = p->match_start + P->len;
			return 1;
		}
		PATTERN_CAST(end) {

			char *match = memmem(text_start, text_len, P->text, P->len + 1);
			if (match) {
				p->match_start = match;
				p->match_end = p->match_start + P->len;
				return 1;
			}
			if (!memcmp(text_end - P->len, P->text, P->len)) {
				p->match_start = text_end - P->len;
				p->match_end = text_end;
				return 1;
			}
		}
		PATTERN_CAST(wrap) {

			char *start = text_start;
			while (text_start < text_end) {
				char *match_start = memmem(start, text_end - start, P->start.text, P->start.len);
				if (!match_start) return 0;
				char *match_limit = text_end;
				if (!option_content_multiline) {
					match_limit = memchr(match_start + P->start.len, '\n', text_end - match_start - P->start.len);
					if (!match_limit) return 0;
				}
				char *match_end = memmem(match_start + P->start.len, match_limit - (match_start + P->start.len), P->end.text, P->end.len);
				if (!match_end) {
					start = match_limit + 1;
					continue;
				};

				p->match_start = match_start;
				p->match_end = match_end + P->end.len;
				return 1;
			}
			return 0;
		}
		PATTERN_CAST(regex) {

			regmatch_t pmatch[1];
			pmatch[0].rm_so = 0;
			pmatch[0].rm_eo = text_len;

			int ret = regexec(&P->regex, text_start, 1, pmatch, REG_STARTEND);
			if (ret == REG_NOMATCH) return 0;

			if (!ret) {
				p->match_start = text_start + pmatch[0].rm_so;
				p->match_end = text_start + pmatch[0].rm_eo;
				return 1;

			} else {
				char buffer[100];
				regerror(ret, &P->regex, buffer, sizeof(buffer));
				printf_error("Regex error: %s", buffer);
				return 0;
			}
		}
	}
	return 0;
}

// === matching

int match_name(string name) {

#define CASE_MATCH(W, _, FUNCTION) \
	case W:                        \
		return FUNCTION(name, option_name_pattern, !option_name_case);

	if (option_name_pattern && !str_equals(option_name_pattern, ".")) {
		switch (option_name_mode) {
			CASE_MATCH('p', "prefix", match_prefix_comma)
			CASE_MATCH('s', "start", match_prefix_comma)
			CASE_MATCH('e', "extension", match_postfix_comma)
			CASE_MATCH('f', "fullname", match_exact_comma)
			CASE_MATCH('-', "any", match_any)
		}
	}
	return 1;
}

char match_any(string text, string value, char case_sensetive) {

#define MATCH_LENS                                      \
	size_t t_len = strlen(text), v_len = strlen(value); \
	if (v_len > t_len) return 0;

	MATCH_LENS
	if (case_sensetive) return strstr(text, value) != 0;

	for (; *text; text++) {
		if (strncasecmp(text, value, v_len) == 0) return 1;
	}
	return 0;
}

char match_exact(string text, string value, char case_sensetive) {
	MATCH_LENS
	if (t_len != v_len) return 0;
	return case_sensetive ? (strncmp(text, value, v_len) == 0) : (strncasecmp(text, value, v_len) == 0);
}
char match_prefix(string text, string value, char case_sensetive) {
	MATCH_LENS

	return case_sensetive ? (strncmp(text, value, v_len) == 0) : (strncasecmp(text, value, v_len) == 0);
}
char match_postfix(string text, string value, char case_sensetive) {
	MATCH_LENS

	string end_start = text + t_len - v_len;
	return case_sensetive ? (strcmp(end_start, value) == 0) : (strcasecmp(end_start, value) == 0);
}

char match_exact_comma(string text, string value, char case_sensetive) {

#define COMMA_EXPANSION(X)                             \
	char *start = value, *end;                         \
	while ((end = strchr(start, ','))) {               \
		size_t len = end - start;                      \
		char buffer[len + 1];                          \
		memcpy(buffer, start, len);                    \
		buffer[len] = '\0';                            \
		if (X(text, buffer, case_sensetive)) return 1; \
		start = end + 1;                               \
	}                                                  \
	return X(text, start, case_sensetive);

	COMMA_EXPANSION(match_exact)
}
char match_prefix_comma(string text, string value, char case_sensetive) {
	COMMA_EXPANSION(match_prefix)
}
char match_postfix_comma(string text, string value, char case_sensetive) {
	COMMA_EXPANSION(match_postfix)
}

// === arguments

int handle_arg_keyword(const char *desc, char *option, string possible, string mappings, string word) {
	char *ref = strchr(possible, word[0]);
	if (!ref) {
		printf_error("Unknown %s '%s'", desc, word);
		return 1;
	}
	*option = mappings[ref - possible];
	return 0;
}

int handle_args(int argc, char *argv[]) {

#define OPTION_CHECK(C, OPTION) \
	case C:                     \
		OPTION += 1;            \
		break;

#define HANDLE_END                 \
	if (str_equals(arg, "--")) {   \
		roots = argv + argi;       \
		roots_count = argc - argi; \
		return 0;                  \
	}

	int argi = 1;

	while (argi < argc) {
		string arg = argv[argi++];
		HANDLE_END

		if (!str_is_option(arg)) {
			if (handle_arg_keyword("file type", &option_file_type,
								   possible_option_file_type,
								   mappings_option_file_type, arg)) return 1;
			break;
		}
		for (char *c = arg + 1; *c; c++) {
			switch (*c) {
				OPTION_CHECK('h', option_help)
				OPTION_CHECK('b', option_bfs)
				OPTION_CHECK('q', option_query)
				OPTION_CHECK('p', option_plain)
				OPTION_CHECK('m', option_monochrome)
				OPTION_CHECK('t', option_table)
				OPTION_CHECK('a', option_unhidden)
				OPTION_CHECK('v', option_verbose)
			default:
				printf_error("Unknown general option '-%c'", *c);
				return 1;
			}
		}
	}

	while (argi < argc) {
		string arg = argv[argi++];
		HANDLE_END

		if (!str_is_option(arg)) {
			if (str_equals(arg, ".")) {
				option_name_mode = 'a';
			} else if (str_endswith(arg, ':')) {
				if (handle_arg_keyword("name pattern type", &option_name_mode,
									   possible_option_name_mode,
									   mappings_option_name_mode, arg)) return 1;
				if (argi == argc) {
					printf_error("Missing value for the name pattern");
					return 1;
				}
				option_name_pattern = argv[argi++];
			} else {
				option_name_pattern = arg;
			}
			break;
		}
		for (char *c = arg + 1; *c; c++) {
			switch (*c) {
				OPTION_CHECK('n', option_name_omit)
				OPTION_CHECK('i', option_name_case)
			default:
				printf_error("Unknown name option '-%c'", *c);
				return 1;
			}
		}
	}

	while (argi < argc) {
		string arg = argv[argi++];
		HANDLE_END

		if (!str_is_option(arg)) {
			pattern *p = content_patterns + content_patterns_len++;
			p->index = content_patterns_len - 1;

			if (str_equals(arg, "--")) {
				break;
			} else if (str_equals(arg, "")) {
				p->type = 0;

			} else if (str_equals(arg, ".")) {
				p->type = T_star;
				dump_files = 1;

			} else if (str_endswith(arg, ':') && !str_equals(arg, ":")) {
				if (handle_arg_keyword("content pattern type", &p->type,
									   possible_option_content_mode,
									   mappings_option_content_mode, arg)) return 1;
				if (argi == argc) {
					printf_error("Missing value for the content pattern");
					return 1;
				}
				switch (p->type) {
				case T_start:
					p->as.start.arg = argv[argi++];
					break;
				case T_end:
					p->as.end.arg = argv[argi++];
					break;
				case T_wrap:
					p->as.wrap.start.arg = argv[argi++];
					if (argi == argc) {
						printf_error("Missing second argument for the content pattern");
						return 1;
					}
					p->as.wrap.end.arg = argv[argi++];
					break;
				case T_regex:
					p->as.regex.arg = argv[argi++];
					break;
				}
			} else {
				p->type = T_any;
				p->as.any.arg = arg;
			}
			if (init_pattern(p)) return 1;
			continue;
		}
		for (char *c = arg + 1; *c; c++) {
			switch (*c) {
				OPTION_CHECK('n', option_content_omit)
				OPTION_CHECK('i', option_content_case)
				OPTION_CHECK('o', option_content_only)
				OPTION_CHECK('m', option_content_multiline)
				OPTION_CHECK('a', option_content_around)
			default:
				printf_error("Unknown content option '-%c'", *c);
				return 1;
			}
		}
	}

	return 0;
}
