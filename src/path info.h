#ifndef PATH_INFO
#define PATH_INFO

#define STANDARD_ERROR_MESSAGE_OF_CONVERTING_WCHAR_TO_UTF8 "<error: could not convert to utf-8 string>"

typedef struct tag_PathInfo PathInfo;

PathInfo *GetPathInfo(wchar_t *mainPath, void (*PathDestructor)(wchar_t *));
PathInfo *GetDefaultPathInfo();
wchar_t *GetChangeableUtf16Path(PathInfo *pi);
const wchar_t *GetUtf16Path(PathInfo *pi);
const unsigned char *GetUtf8Path(PathInfo *pi);
const unsigned char *GetUtf8PathMessage(PathInfo *pi);
void *DestroyPathInfo(PathInfo *pi);

const unsigned char *AvoidNull(const char *msg, const char *errorMessage);
unsigned char *WstringToUtf8(const wchar_t *wstr);
void Wrap_Free(void *arg);

#endif