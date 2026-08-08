#ifndef SMALL_PARSER
#define SMALL_PARSER

#include <stdio.h>
#include "growing list.h"
#include "memory buffer.h"

typedef struct
{
    GrowingList sections;
    FILE* stream;
} IniFileInfo;

typedef struct tag_IniSection
{
    MemoryBuffer name;
    GrowingList iniPairs;
} IniSection;

typedef struct tag_IniPair
{
    MemoryBuffer key, value;
} IniPair;

IniFileInfo *InitIniInfo(const wchar_t *filePath);
void DestroyIniFileInfo(IniFileInfo *iniFI);

unsigned char *GetIniUrl(const IniFileInfo *iniFI);

#endif