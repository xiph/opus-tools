#include "unicode_support.h"

#include <windows.h>
#include <io.h>

char *utf16_to_utf8(const wchar_t *input)
{
	char *Buffer;
	int BuffSize = 0, Result = 0;

	BuffSize = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
	Buffer = (char*) malloc(sizeof(char) * BuffSize);
	if(Buffer)
	{
		Result = WideCharToMultiByte(CP_UTF8, 0, input, -1, Buffer, BuffSize, NULL, NULL);
	}

	return ((Result > 0) && (Result <= BuffSize)) ? Buffer : NULL;
}

char *utf16_to_ansi(const wchar_t *input)
{
	char *Buffer;
	int BuffSize = 0, Result = 0;

	BuffSize = WideCharToMultiByte(CP_ACP, 0, input, -1, NULL, 0, NULL, NULL);
	Buffer = (char*) malloc(sizeof(char) * BuffSize);
	if(Buffer)
	{
		Result = WideCharToMultiByte(CP_ACP, 0, input, -1, Buffer, BuffSize, NULL, NULL);
	}

	return ((Result > 0) && (Result <= BuffSize)) ? Buffer : NULL;
}

wchar_t *utf8_to_utf16(const char *input)
{
	wchar_t *Buffer;
	int BuffSize = 0, Result = 0;

	BuffSize = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
	Buffer = (wchar_t*) malloc(sizeof(wchar_t) * BuffSize);
	if(Buffer)
	{
		Result = MultiByteToWideChar(CP_UTF8, 0, input, -1, Buffer, BuffSize);
	}

	return ((Result > 0) && (Result <= BuffSize)) ? Buffer : NULL;
}

void init_commandline_arguments_utf8(int *argc, char ***argv)
{
	int i, nArgs;
	LPWSTR *szArglist;

	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

	if(NULL == szArglist)
	{
		fprintf(stderr, "\nFATAL: CommandLineToArgvW failed\n\n");
		exit(-1);
	}

	*argv = (char**) malloc(sizeof(char*) * nArgs);
	*argc = nArgs;

	if(NULL == *argv)
	{
		fprintf(stderr, "\nFATAL: Malloc failed\n\n");
		exit(-1);
	}
	
	for(i = 0; i < nArgs; i++)
	{
		(*argv)[i] = utf16_to_utf8(szArglist[i]);
		if(NULL == (*argv)[i])
		{
			fprintf(stderr, "\nFATAL: utf16_to_utf8 failed\n\n");
			exit(-1);
		}
	}

	LocalFree(szArglist);
}

void free_commandline_arguments_utf8(int *argc, char ***argv)
{
	int i = 0;
	
	if(*argv != NULL)
	{
		for(i = 0; i < *argc; i++)
		{
			if((*argv)[i] != NULL)
			{
				free((*argv)[i]);
				(*argv)[i] = NULL;
			}
		}
		free(*argv);
		*argv = NULL;
	}
}

FILE *fopen_utf8(const char *filename_utf8, const char *mode_utf8)
{
	FILE *ret = NULL;
	wchar_t *filename_utf16 = utf8_to_utf16(filename_utf8);
	wchar_t *mode_utf16 = utf8_to_utf16(mode_utf8);
	
	if(filename_utf16 && mode_utf16)
	{
		ret = _wfopen(filename_utf16, mode_utf16);
	}

	if(filename_utf16) free(filename_utf16);
	if(mode_utf16) free(mode_utf16);

	return ret;
}

int stat_utf8(const char *path_utf8, struct _stat *buf)
{
	int ret = -1;
	
	wchar_t *path_utf16 = utf8_to_utf16(path_utf8);
	if(path_utf16)
	{
		ret = _wstat(path_utf16, buf);
		free(path_utf16);
	}
	
	return ret;
}

int unlink_utf8(const char *path_utf8)
{
	int ret = -1;
	
	wchar_t *path_utf16 = utf8_to_utf16(path_utf8);
	if(path_utf16)
	{
		ret = _wunlink(path_utf16);
		free(path_utf16);
	}
	
	return ret;
}
