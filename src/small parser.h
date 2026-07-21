#ifndef SMALL_PARSER
#define SMALL_PARSER
#include <windows.h>    //byte

typedef struct
{
    const byte *text;
    const size_t textLength;
    size_t seek;
} BufferContext;

byte *ParseIniText(const byte *text, size_t textLength);

#endif