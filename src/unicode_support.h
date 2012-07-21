#ifndef UNICODE_SUPPORT_H_INCLUDED
#define UNICODE_SUPPORT_H_INCLUDED

#include <stdio.h>
#include <sys/stat.h>

char *utf16_to_utf8(const wchar_t *input);
char *utf16_to_ansi(const wchar_t *input);
wchar_t *utf8_to_utf16(const char *input);
void init_commandline_arguments_utf8(int *argc, char ***argv);
void free_commandline_arguments_utf8(int *argc, char ***argv);
FILE *fopen_utf8(const char *filename_utf8, const char *mode_utf8);
int stat_utf8(const char *path_utf8, struct _stat *buf);
int unlink_utf8(const char *path_utf8);

#endif