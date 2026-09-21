#pragma once
#include "core/as_string.h"


struct _iobuf; typedef _iobuf FILE;

namespace zs1 {
using as1::STRING;


extern STRING g_WindowsUserPath;


void AssignCString(char** destination, const char* source);

bool SplitLine(char* text, char** left, char** right, char delimiter);

char* ReadLine(FILE* file);

void BackupFile(const char* fileName);

unsigned int Adler32(const char* text);


void Assert(int severity, const char* expression, const char* fileName, int line);


}
