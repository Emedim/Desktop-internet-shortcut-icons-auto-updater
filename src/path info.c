#include <windows.h>
#include "path info.h"

typedef struct tag_PathInfo
{
    wchar_t *utf16;
    void (*destroyMainStr)(wchar_t *);
    unsigned char *utf8;
} PathInfo;

PathInfo *GetPathInfo(void (*PathDestructor)(wchar_t *))
{
    PathInfo *pi = malloc(sizeof(PathInfo));
    if (pi) *pi = (PathInfo){ .destroyMainStr = PathDestructor};
    return pi;
}

wchar_t *GetChangeableUtf16Path(PathInfo *pi)
{
    free(pi->utf8);
    pi->utf8 = NULL;
    return pi->utf16;
}

const unsigned char *GetUtf8Path(PathInfo *pi)
{
    if (!pi->utf8) pi->utf8 = WstringToUtf8(pi->utf16);
    return pi->utf8;
}

void *DestroyPathInfo(PathInfo *pi)
{
    if (pi->destroyMainStr) pi->destroyMainStr(pi->utf16);
    free(pi->utf8);
    free(pi);
}



unsigned char *WstringToUtf8(const wchar_t *wstr)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (!size) return NULL; 
    unsigned char *utf8 = malloc(size);
    if (!utf8) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, size, NULL, NULL))
    {
        free(utf8);
        return NULL;
    }
    return utf8;
}

const unsigned char *AvoidNull(const char *msg, const char *errorMessage)
{
    return msg ? msg : errorMessage;
}