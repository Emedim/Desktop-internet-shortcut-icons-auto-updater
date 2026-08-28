#include <windows.h>
#include <stdbool.h>
#include "path info.h"

typedef struct tag_PathInfo
{
    wchar_t *utf16;
    void (*destroyMainStr)(wchar_t *);
    unsigned char *utf8;
} PathInfo;

PathInfo *GetPathInfo(wchar_t *mainPath, void (*PathDestructor)(wchar_t *))
{
    if (!mainPath) return NULL;
    PathInfo *pi = malloc(sizeof(PathInfo));
    if (pi) *pi = (PathInfo){ mainPath, PathDestructor, NULL };
    else if (PathDestructor) PathDestructor(mainPath);
    return pi;
}

PathInfo *GetDefaultPathInfo()
{
    return GetPathInfo(malloc(sizeof(wchar_t) * MAX_PATH), Wrap_Free);
}

wchar_t *GetChangeableUtf16Path(PathInfo *pi)
{
    return pi->utf16;
}

const wchar_t *GetUtf16Path(PathInfo *pi)
{
    return pi->utf16;
}

const char *GetUtf8Path(PathInfo *pi)
{
    if (!pi->utf8) pi->utf8 = WstringToUtf8(pi->utf16);
    return pi->utf8;
}

const char *GetUtf8PathMessage(PathInfo *pi)
{
    return AvoidNull(GetUtf8Path(pi), STANDARD_ERROR_MESSAGE_OF_CONVERTING_WCHAR_TO_UTF8);
}

void *DestroyPathInfo(PathInfo *pi)
{
    if (pi->destroyMainStr) pi->destroyMainStr(pi->utf16);
    free(pi->utf8);
    free(pi);
}



char *WstringToUtf8(const wchar_t *wstr)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (!size) return NULL; 
    char *utf8 = malloc(size);
    if (!utf8) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, size, NULL, NULL))
    {
        free(utf8);
        return NULL;
    }
    return utf8;
}

const char *AvoidNull(const char *msg, const char *errorMessage)
{
    return msg ? msg : errorMessage;
}

void Wrap_Free(void *arg)
{
    free(arg);
}