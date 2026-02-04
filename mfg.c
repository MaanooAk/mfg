

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <fts.h>
#include <sys/stat.h>

typedef char check;
typedef char *string;
typedef __mode_t filemode;
typedef __off_t filesize;

#include "help.c"
#include "mfg.h"

#define str_contains(s, c) (strchr(s, c))
#define str_endswith(s, c) ((s)[strlen(s) - 1] == c)
#define str_equals(s1, s2) (strcmp(s1, s2) == 0)
#define str_is_option(s) ((s)[0] == '-' && (s)[1])

#define implies(a, b) (!(a) || (b))

#define printf_error(fmt, ...) fprintf(stderr, "mfg: " fmt "\n", ##__VA_ARGS__);

// === options

check option_help = 0;
check option_bfs = 0;
check option_plain = 0;
check option_unhidden = 0;
char option_file_type = 'a';
string possible_option_file_type = "afdet";
char option_name_mode = '-';
string possible_option_name_mode = "psef";
string option_name_pattern = 0;
check option_name_case = 0;
check option_name_omit = 0;
check option_content_case = 0;
check option_content_omit = 0;
check option_content_only = 0;
check option_content_multiline = 0;
char option_content_mode = '-';
string possible_option_content_mode = "bsewr";
string option_content_pattern = 0;
string option_content_pattern_second = 0;

check skip_loading = 0;

// === main

int main(int argc, char *argv[]) {

	int args_error = handle_args(argc, argv);
	if (args_error) return 2;

	if (option_help) {
		printf("%s", help);
		return 0;
	}

	skip_loading = !option_content_pattern && !str_contains("et", option_file_type);

	traverse_paths();

	return 0;
}

// === handlers

int traverse_paths() {

	char *paths[] = {".", 0};
	FTS *tree = fts_open(paths, FTS_NOCHDIR | (skip_loading ? FTS_NOSTAT : 0), 0);
	if (!tree) return 1;

	while (1) {
		FTSENT *node = fts_read(tree);
		if (!node) break;

		string path = node->fts_path;
		string name = node->fts_name;

		char skip = (option_unhidden && name[0] == '.' && name[1]);

		switch (node->fts_info) {
		case FTS_F:
			if (!skip) {
				filemode mode = skip_loading ? 0 : node->fts_statp->st_mode;
				filesize size = skip_loading ? 0 : node->fts_statp->st_size;
				handle_file(path, name, mode, size);
			}
			break;
		case FTS_D:
			if (skip || skip_directory(name)) {
				fts_set(tree, node, FTS_SKIP);
			} else {
				handle_directory(path, name);
			}
			break;
		case FTS_ERR:
		case FTS_DNR:
			printf_error("Error reading %s", path);
		default:
		}
	}

	fts_close(tree);
	return 0;
}

char skip_directory(const char *name) {
	return str_equals(name, ".git") ||
		   str_equals(name, "node_modules") ||
		   str_equals(name, "build") ||
		   str_equals(name, "dist");
}

void handle_directory(string path, string name) {

	if (option_name_omit) return;
	if (!str_contains("ad", option_file_type)) return;
	if (!match_name(name)) return;

	if (option_content_pattern) return;

	printf("%s\n", path);
}

void handle_file(string path, string name, filemode mode, filesize size) {

	if (!str_contains("afet", option_file_type)) return;
	if (!implies(option_file_type == 'e', mode & S_IXUSR)) return;
	if (!match_name(name)) return;

	if (!option_content_pattern) {
		printf("%s\n", path);
	} else {
		handle_content(path, name, mode, size);
	}
}

int match_name(string name) {

#define CASE_MATCH(W, _, FUNCTION) \
	case W:                        \
		return FUNCTION(name, option_name_pattern, option_name_case);

	if (option_name_pattern && !str_equals(option_name_pattern, ".")) {
		switch (option_name_mode) {
			CASE_MATCH('p', "prefix", match_prefix_comma)
			CASE_MATCH('s', "start", match_prefix_comma)
			CASE_MATCH('e', "extension", match_postfix_comma)
			CASE_MATCH('f', "fullname", match_exact)
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

char match_prefix_comma(string text, string value, char case_sensetive) {
	char *start = value, *end;
	while ((end = strchr(start, ','))) {
		size_t len = end - start;
		char buffer[len + 1];
		memcpy(buffer, start, len);
		buffer[len] = '\0';
		if (match_prefix(text, buffer, case_sensetive)) return 1;
		start = end + 1;
	}
	return match_prefix(text, start, case_sensetive);
}
char match_postfix_comma(string text, string value, char case_sensetive) {
	char *start = value, *end;
	while ((end = strchr(start, ','))) {
		size_t len = end - start;
		char buffer[len + 1];
		memcpy(buffer, start, len);
		buffer[len] = '\0';
		if (match_postfix(text, buffer, case_sensetive)) return 1;
		start = end + 1;
	}
	return match_postfix(text, start, case_sensetive);
}

// === content

void handle_content(string path, string name, filemode mode, filesize size) {
	printf("%s (%ldKb)\n", path, size / 1024);

	// TODO
}

// === arguments

int handle_arg_keyword(const char *desc, char *option, string possible, string word) {
	char value = word[0];
	if (!str_contains(possible, value)) {
		printf_error("Unknown %s '%s'", desc, word);
		return 1;
	}
	*option = value;
	return 0;
}

int handle_args(int argc, char *argv[]) {

#define OPTION_CHECK(C, OPTION) \
	case C:                     \
		OPTION = 1;             \
		break;

	int argi = 1;

	while (argi < argc) {
		string arg = argv[argi++];

		if (!str_is_option(arg)) {
			if (handle_arg_keyword("file type", &option_file_type, possible_option_file_type, arg)) return 1;
			break;
		}
		for (char *c = arg + 1; *c; c++) {
			switch (*c) {
				OPTION_CHECK('h', option_help)
				OPTION_CHECK('b', option_bfs)
				OPTION_CHECK('p', option_plain)
				OPTION_CHECK('a', option_unhidden)
			default:
				printf_error("Unknown general option '-%c'", *c);
				return 1;
			}
		}
	}

	while (argi < argc) {
		string arg = argv[argi++];

		if (!str_is_option(arg)) {
			if (str_equals(arg, ".")) {
				option_name_mode = 'a';
			} else if (str_endswith(arg, ':')) {
				if (handle_arg_keyword("name pattern type", &option_name_mode, possible_option_name_mode, arg)) return 1;
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
				OPTION_CHECK('c', option_name_case)
				OPTION_CHECK('n', option_name_omit)
			default:
				printf_error("Unknown name option '-%c'", *c);
				return 1;
			}
		}
	}

	while (argi < argc) {
		string arg = argv[argi++];

		if (!str_is_option(arg)) {
			if (str_equals(arg, ".")) {
				option_content_mode = 'a';
				option_content_pattern = "";
			} else if (str_endswith(arg, ':')) {
				if (handle_arg_keyword("content pattern type", &option_content_mode, possible_option_content_mode, arg)) return 1;
				if (argi == argc) {
					printf_error("Missing value for the content pattern");
					return 1;
				}
				option_content_pattern = argv[argi++];
				if (option_content_mode == 'w') {
					if (argi == argc) {
						printf_error("Missing second value for the content pattern");
						return 1;
					}
					option_content_pattern_second = argv[argi++];
				}
			} else {
				option_content_pattern = arg;
			}
			break;
		}
		for (char *c = arg + 1; *c; c++) {
			switch (*c) {
				OPTION_CHECK('c', option_content_case)
				OPTION_CHECK('n', option_content_omit)
				OPTION_CHECK('o', option_content_only)
				OPTION_CHECK('m', option_content_multiline)
			default:
				printf_error("Unknown content option '-%c'", *c);
				return 1;
			}
		}
	}

	if (argi < argc) {
		printf_error("Unhandled trailing argument '%s'", argv[argi]);
		return 1;
	}

	return 0;
}
