#define APP_NAME "Desktop icons auto updater"

#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath();
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW(); StringCchCopyW(); StringCchPrintfW();
#include <pathcch.h>    //PathCchRemoveFileSpec()
#include <shlwapi.h>    //PathFindExtensionW()
#include <wchar.h>      //wmemcmp
#include <errno.h>

//для COM
#include <objbase.h>
#include <objidl.h>
#include <propidl.h>
#include <intshcut.h>


#include "curl/curl.h"
#include "vips.h"

#include "libxml/HTMLparser.h"
#include "libxml/xpath.h"
#include "libxml/uri.h"

#include "cleanup interface.h"
#include "growing list.h"
#include "memory buffer.h"
#include "path info.h"
#define STANDARD_ERROR (-1)
#define MAX_CURRENT_SYSTEM_RESOURCES (18)
#define INICIAL_BUFFER_LENGTH (12)
#define BUFFER_ADDITION (8)
#define URI_MAX_LENGTH (2048)

#define FOLDER_PAGES_L L"pages"
#define FOLDER_FAVICONS_L L"favicons"
#define DEFAULT_EXTENTION_OF_FILES_PAGES_L L"html"
#define TARGET_PAGE_RESPONSE_TYPE "text/html"
#define CHOICE_UPDATE_ICONS '1'
#define CHOICE_RESTORE_DEFAULT_ICONS '2'
#define CHOICE_EXIT '3'


typedef enum
{
    imgTypeUnknown,
    imgTypeIco,
    imgTypePng,
    imgTypeSvg,
    imgTypeGif,
    imgTypeJpeg,
    imgTypeWebp,
    imgTypeBmp,
    imgTypeTiff,
    imgTypeAvif,
    imgTypeApng
} ImageTypes;

typedef struct
{
    const wchar_t *extention;
    const char *extentionUtf8;
    ImageTypes type;
} ImageContentType;

typedef struct
{
    ImageContentType contentType;
    unsigned char *faviconContentType;
} ContentTypesMatchingUnit;

#define CONTENT_TYPES_QUAINITY 14
typedef struct
{
    ContentTypesMatchingUnit array[CONTENT_TYPES_QUAINITY];
    size_t quantity;
} ContentTypesMatchingContainer;

#define ICO_W L"ico"
#define PNG_W L"png"
const ContentTypesMatchingContainer contentTypes = 
{
    {
        { { ICO_W,      "ico",  imgTypeIco  },  "image/vnd.microsoft.icon"  },
        { { ICO_W,      "ico",  imgTypeIco  },  "image/x-icon",             },
        { { ICO_W,      "ico",  imgTypeIco  },  "image/ico",                },
        { { ICO_W,      "ico",  imgTypeIco  },  "image/icon",               },
        { { PNG_W,      "png",  imgTypePng  },  "image/png",                },
        { { L"svg",     "svg",  imgTypeSvg  },  "image/svg+xml",            },
        { { L"gif",     "gif",  imgTypeGif  },  "image/gif",                },
        { { L"jpeg",    "jpeg", imgTypeJpeg },  "image/jpeg",               },
        { { L"webp",    "webp", imgTypeWebp },  "image/webp",               },
        { { L"bmp",     "bmp",  imgTypeBmp  },  "image/bmp",                },
        { { L"bmp",     "bmp",  imgTypeBmp  },  "image/x-bmp",              },
        { { L"tiff",    "tiff", imgTypeTiff },  "image/tiff",               },
        { { L"avif",    "avif", imgTypeAvif },  "image/avif",               },
        { { L"apng",    "apng", imgTypeApng },  "image/apng",               }
    },
    CONTENT_TYPES_QUAINITY
};

typedef enum
{
    DM_PageDefault,
    DM_PageRetry,
    DM_Favicon
} DownloadingMode;

typedef struct
{
    PathInfo *ptr;
    bool isOriginal;
} PathInfoPtr;

typedef struct
{
    PathInfo *path;
    PathInfo *name;
    bool isBound;
} IconFileInfo;

typedef struct
{
    void *stream;
    bool (*write)(void *, const unsigned char *, size_t);
} StreamInfo;

typedef struct
{
    MemoryBuffer logBuffer;
    MemoryBuffer faviconBuffer;
    StreamInfo curlCallbackWriteStream;
    unsigned char *url;
    IconFileInfo *boundIcon;
    CURL *easy;
    FILE *responseFile;

    IPersistFile  *iPersistFile;
    IPropertySetStorage *iPropertySetStorage;
    IPropertyStorage *iPropertyStorage_Intshcut;

    PathInfo *fileName;
    PathInfo *desktopUrlFilePath;
    PathInfo *pageResponseFilePath;
    PathInfo *faviconResponseFilePath;

    DownloadingMode downloadingMode;
} IconProcessUnit;

typedef struct
{
    GrowingList iconProcessUnits;
    CURLM *multi;
} IconsProcessContainer;


typedef struct
{
    MemoryBuffer *buffer;
    bool isОriginal;
} imageBufferInfo;

#define PNG_MIN_LENGTH (33)
#define BPP_DEFAULT (32)
#pragma pack(push, 1)   //выключает выравнивание
typedef struct {
    uint16_t reserved;
    uint16_t type;      // 1 = ICO
    uint16_t num_images;
} ICONDIR;

typedef struct {
    uint8_t  width;
    uint8_t  height;
    uint8_t  palette;
    uint8_t  reserved;
    uint16_t planes;
    uint16_t bpp;
    uint32_t size;
    uint32_t offset;
} ICONDIRENTRY;
#pragma pack(pop)   //восстанавливает выравнивание
const ICONDIR iconHeader = {0, 1, 1};
const unsigned char pngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};


size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
static void FatalError(const unsigned char *message);
void CurlGetinfoFailMessage(StreamInfo *streamInfo, const unsigned char *type);
unsigned char *GetFaviconUrl(const unsigned char *buffer, int size, const unsigned char *base_url);
bool DropDirectory(const wchar_t *directory);
int vMakeMessage(unsigned char **buffer, const unsigned char *format, va_list ap);
int MakeMessage(unsigned char **buffer, const unsigned char *format, ...);
int PrintStream(StreamInfo *info, const unsigned char *format, ...);
bool WriteCurlResponseCode(CURL *easy, StreamInfo *stream, long *responseCode);
bool WriteCurlContentType(CURL *easy, StreamInfo *stream, unsigned char **contentType);
bool WriteStream(StreamInfo *info, const unsigned char *data, size_t dataLength);
bool SIWrap_fwrite(void *streamInfo, const unsigned char *data, size_t dataLength);
bool SIWrap_WriteMemoryBuffer(void *memoryBuffer, const unsigned char *data, size_t dataLength);
const ImageContentType *GetExtentionFromContentType(const unsigned char *contentType);
bool CompareIconFileInfoToIpuName(void *ifi, void *ipu);

void Wrap_FindClose(void *arg);
void Wrap_CoTaskMemFree(void *arg);
void IconsProcessContainerDestructor(void *arg);
void Wrap_FClose(void *arg);
void Wrap_curl_global_cleanup(void *arg);
void Wrap_vips_shutdown(void *arg);
void Wrap_g_object_unref(void *arg);
void Wrap_DestroyGrowingList(void *arg);
void Wrap_CoUninitialize(void *arg);
void Wrap_PropVariantClear(void *arg);
void Wrap_DestroyPathInfo(void *arg);
void DestroyIconFileInfo(void *ifi);

CleanupStack cleanupStack = NULL;
FILE* logFile = NULL;
int main(void)
{
    cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
    if (!cleanupStack)
    {
        FatalError("Could not initialize cleanup stack.");
        return STANDARD_ERROR;
    }

    PathInfo *appFolder = GetDefaultPathInfo();
    if (!appFolder)
    {
        FatalError("Could not initialize (PathInfo *) for app folder. GetDefaultPathInfo() failed.\n");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, appFolder); //(1 глобально)
    if 
    (
        !GetCurrentDirectoryW(MAX_PATH, GetChangeableUtf16Path(appFolder)) ||
        PathCchRemoveFileSpec(GetChangeableUtf16Path(appFolder), MAX_PATH) != S_OK
    )
    {
        FatalError("Could not get CWD or remove \\bin from path. GetCurrentDirectoryW() or PathCchRemoveFileSpec() failed.");
        return STANDARD_ERROR;
    }

    PathInfo *logPath = GetDefaultPathInfo();
    if (!logPath)
    {
        FatalError("Could not initialize (PathInfo *) for path to log file. GetDefaultPathInfo() failed.\n");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, logPath); //(2 глобально)
    if (FAILED(StringCchPrintfW(GetChangeableUtf16Path(logPath), MAX_PATH, L"%ls\\logs\\app.log", GetUtf16Path(appFolder))))
    {
        FatalError("Could not build path to app.log file. StringCchPrintfW() failed.");
        return STANDARD_ERROR;
    }
    if (!(logFile = _wfopen(GetUtf16Path(logPath), L"wb")))
    {
        FatalError("Could not open app.log file.");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_FClose, logFile);      // (3 глобально)
    
    fprintf(logFile, "[DEBUG] Current app folder (CWD with no \\bin): \"%s\".\n", GetUtf8PathMessage(appFolder));
    fprintf(logFile, "[DEBUG] app.log Path: \"%s\".\n", GetUtf8PathMessage(logPath));

    PathInfo *pagesFolderName = GetPathInfo(FOLDER_PAGES_L, NULL);
    PathInfo *pagesPath = GetDefaultPathInfo();
    if (!pagesFolderName || !pagesPath)
    {
        FatalError("Could not initialize (PathInfo *) for folders for response files. GetPathInfo() or GetDefaultPathInfo() failed.\n");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, pagesFolderName);    //(4 глобально)
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, pagesPath);          //(5 глобально)

    if (FAILED(StringCchPrintfW(GetChangeableUtf16Path(pagesPath), MAX_PATH, L"%ls\\response\\%ls\\", GetUtf16Path(appFolder), GetUtf16Path(pagesFolderName))))
    {
        unsigned char *errorMessage = NULL;
        MakeMessage(&errorMessage, "Could not build \"%s\" path for responces.", GetUtf8PathMessage(pagesFolderName));
        FatalError(errorMessage);
        free(errorMessage);
        return STANDARD_ERROR;
    }
    fprintf(logFile, "[DEBUG] Built folder path \"%s\": \"%s\".\n", GetUtf8PathMessage(pagesFolderName), GetUtf8PathMessage(pagesPath));
    if (!DropDirectory(GetUtf16Path(pagesPath)))
        fprintf(logFile, "[ERROR] Deleting files error. Some files may remain.\n");
    else fprintf(logFile, "[DEBUG] Directory cleaned up.\n");
    
    PathInfo *faviconsFolderName = GetPathInfo(FOLDER_FAVICONS_L, NULL);
    PathInfo *faviconsPath = GetDefaultPathInfo();
    if (!faviconsFolderName || !faviconsPath)
    {
        FatalError("Could not initialize (PathInfo *) for folders for response files. GetPathInfo() or GetDefaultPathInfo() failed.\n");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, faviconsFolderName);    //(4 глобально)
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, faviconsPath);          //(5 глобально)

    if (FAILED(StringCchPrintfW(GetChangeableUtf16Path(faviconsPath), MAX_PATH, L"%ls\\response\\%ls\\", GetUtf16Path(appFolder), GetUtf16Path(faviconsFolderName))))
    {
        unsigned char *errorMessage = NULL;
        MakeMessage(&errorMessage, "Could not build \"%s\" path for responces", GetUtf8PathMessage(faviconsFolderName));
        FatalError(errorMessage);
        free(errorMessage);
        return STANDARD_ERROR;
    }
    fprintf(logFile, "[DEBUG] Построен путь к папке \"%s\": \"%s\".\n", GetUtf8PathMessage(faviconsFolderName), GetUtf8PathMessage(faviconsPath));
    if (!DropDirectory(GetUtf16Path(faviconsPath)))
        fprintf(logFile, "[ERROR] Deleting files error. Some files may remain.\n");
    else fprintf(logFile, "[DEBUG] Directory cleaned up.\n");

    WIN32_FIND_DATAW fileData;
    HANDLE searchingFilesHandle;
    wchar_t searchToken[MAX_PATH];
    GrowingList existingIcons;
    PushCleanupStack(cleanupStack, Wrap_DestroyGrowingList, &existingIcons);        //(8 глобально)

    if (InitGrowingList(&existingIcons, DestroyIconFileInfo))
    {
        if (SUCCEEDED(StringCchPrintfW(searchToken, MAX_PATH, L"%ls\\resources\\icons\\*.ico", GetUtf16Path(appFolder))))
        {
            searchingFilesHandle = FindFirstFileW(searchToken, &fileData);
            if (searchingFilesHandle == INVALID_HANDLE_VALUE)
            {
                if (GetLastError() == ERROR_FILE_NOT_FOUND) fprintf(logFile, "[DEBUG] Icon resource directory is empty.\n");
                else fprintf(logFile, "[ERROR] Failed to search for existing icon files. FindFirstFileW() failed.\n");
            }
            else
            {
                fprintf(logFile, "[DEBUG] Searching for existing icons...\n");
                do
                {
                    IconFileInfo *ifi = malloc(sizeof(IconFileInfo));
                    if (!ifi)
                    {
                        fprintf(logFile, "[ERROR] Failed to allocate memory for file object. malloc() failed.\n");
                        continue;
                    }
                    ifi->isBound = false;
                    if
                    (
                        !(ifi->name = GetDefaultPathInfo()) ||
                        !(ifi->path = GetDefaultPathInfo())
                    )
                    {
                        fprintf(logFile, "[ERROR] Could not initialize (IconFileInfo *) for file object. GetDefaultPathInfo() failed.\n");
                        DestroyIconFileInfo((void *)ifi);
                        continue;
                    }
                    if
                    (
                        StringCchCopyW(GetChangeableUtf16Path(ifi->name), MAX_PATH, fileData.cFileName) != S_OK ||
                        FAILED(StringCchPrintfW(GetChangeableUtf16Path(ifi->path), MAX_PATH, L"%ls\\resources\\icons\\%ls", GetUtf16Path(appFolder), GetUtf16Path(ifi->name)))
                    )
                    {
                        fprintf(logFile, "[ERROR] Failed to initialize file object with its name or path. StringCchCopyW() or StringCchPrintfW() failed.\n");
                        DestroyIconFileInfo((void *)ifi);
                        continue;
                    }
                    fprintf(logFile, "\n[DEBUG] Found file: \"%s\".\n", GetUtf8PathMessage(ifi->name));
                    if (!PushGrowingList(&existingIcons, ifi))
                    {
                        fprintf(logFile, "[ERROR] Failed to add file object to the list of existing icons. PushGrowingList() failed.\n");
                        DestroyIconFileInfo((void *)ifi);
                        continue;
                    }
                    fprintf(logFile, "[DEBUG] File processed successfully.\n");
                }
                while(FindNextFileW(searchingFilesHandle, &fileData));
                FindClose(searchingFilesHandle);
                fprintf(logFile, "\n[DEBUG] Finished searching for existing icons.\n");
            }
        }
        else fprintf(logFile, "[ERROR] Could not build searching .ico files token. StringCchPrintfW() failed");
    }
    else fprintf(logFile, "[ERROR] Could not initialize (GrowingList) for existing icon file objects. InitGrowingList() failed.\n");

    PathInfo *folderTempResources = GetDefaultPathInfo();
    if (!folderTempResources)
    {
        FatalError("Could not initialize (PathInfo *) for temp icon file pathes. GetDefaultPathInfo() failed.\n");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, folderTempResources);    // (9 глобально)
    if (SUCCEEDED(StringCchPrintfW(GetChangeableUtf16Path(folderTempResources), MAX_PATH, L"%ls\\resources\\temp\\", GetUtf16Path(appFolder))))
    {
        fprintf(logFile, "[DEBUG] Built directory path \"resources\\temp\": \"%s\".\n", GetUtf8PathMessage(folderTempResources));
        if (DropDirectory(GetUtf16Path(folderTempResources))) fprintf(logFile, "[DEBUG] Directory cleaned up.\n");
        else fprintf(logFile, "[ERROR] Error while cleaning direcotry. Some files may remain.\n");
    }
    else fprintf(logFile, "[ERROR] Could not build path to resources\\temp folder to clean it. Some files may remain.\n");

    wchar_t *desktopPathTempPtr = NULL;
    if (FAILED(SHGetKnownFolderPath(
        &FOLDERID_Desktop, // рабочий стол
        0,
        NULL,
        &desktopPathTempPtr
    )))
    {
        FatalError("Could not find path to desktop. SHGetKnownFolderPath() failed.");
        CoTaskMemFree(desktopPathTempPtr); //COM-кучу освобождать даже в случае неудачи
        return STANDARD_ERROR;
    }
    PathInfo *desktopPath = GetPathInfo(desktopPathTempPtr, Wrap_CoTaskMemFree);
    if (!desktopPath)
    {
        FatalError("Could not initialize (PathInfo *) for desktop path. GetPathInfo() failed.");
        CoTaskMemFree(desktopPathTempPtr);  //Потому что ещё не удалось передать владение указателем на COM-память
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, desktopPath);   // (10 глобально)
    fprintf(logFile, "[DEBUG] Desktop path: \"%s\".\n", GetUtf8PathMessage(desktopPath));
    
    if (FAILED(StringCchPrintfW(searchToken, MAX_PATH, L"%ls\\*.url", GetUtf16Path(desktopPath))))
    {
        FatalError("Could not build search token for .url files on the desktop. StringCchCopyW() or StringCchCatW() failed.");
        return STANDARD_ERROR;
    }
    
    searchingFilesHandle = FindFirstFileW(searchToken, &fileData);        // получить дескриптор поиска и получить первый файл
    if (searchingFilesHandle == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            fprintf(logFile, "\n[DEBUG] No .url (Internet Shortcut) files found on the desktop.\n");
            CompleteDeallocation(cleanupStack);
            return 0;
        }
        else
        {
            FatalError("Invalid file search descriptor. FindFirstFileW() failed.");
            return STANDARD_ERROR;
        }
    }
    PushCleanupStack(cleanupStack, Wrap_FindClose, &searchingFilesHandle);  // (11 глобально)

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        FatalError("Could not initialize the cURL environment. curl_global_init() failed.");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_curl_global_cleanup, NULL);        // (12 глобально)
    IconsProcessContainer iconsProcessContainer;
    iconsProcessContainer.multi = curl_multi_init();
    if (!iconsProcessContainer.multi)
    {
        FatalError("Could not initialize (IconsProcessContainer::multi *). curl_multi_init() failed.");
        return STANDARD_ERROR;
    }
    if (!InitGrowingList(&iconsProcessContainer.iconProcessUnits, NULL))
    {
        FatalError("Could not initialize (iconProcessContainer::iconProcessUnits). InitGrowingList() failed.\n");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, IconsProcessContainerDestructor, &iconsProcessContainer);    // (13 глобально)

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
    {
        FatalError("[ERROR] Could not initialize COM. CoInitializeEx() failed.");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_CoUninitialize, NULL);  // (14 глобально)

    PROPSPEC propspec[2] =
    {
        { .ulKind = PRSPEC_PROPID, .propid = PID_IS_ICONFILE  },
        { .ulKind = PRSPEC_PROPID, .propid = PID_IS_ICONINDEX }
    };

    SetConsoleOutputCP(65001);  //UTF-8
    printf("Choose mode:\n");
    printf("1 – update icons\n");
    printf("2 – restore default icons\n");
    printf("3 – exit\n");
    printf("Your choice >> ");
    int userChoice = getchar();
    if (getchar() != '\n')
    {
        fprintf(logFile, "[ERROR] Too many characters entered. Input not recognized.\n");
        printf("Invalid input\n");
        CompleteDeallocation(cleanupStack);
        system("pause");
        return STANDARD_ERROR;
    }
    switch (userChoice)
    {
    case CHOICE_UPDATE_ICONS:
        fprintf(logFile, "[DEBUG] New icon download and update mode selected.\n");
        printf("Icon update mode (This may take some time)\n");
        break;
    case CHOICE_RESTORE_DEFAULT_ICONS:
        fprintf(logFile, "[DEBUG] Default icon restoration mode selected.\n");
        printf("Restore default icon mode\n");
        break;
    case CHOICE_EXIT:
        fprintf(logFile, "[DEBUG] Exit mode selected.\n");
        printf("Exit ... \n");
        CompleteDeallocation(cleanupStack);
        system("pause");
        return 0;
    default:
        printf("Invalid input\n");
        fprintf(logFile, "[ERROR] Invalid character entered.\n");
        CompleteDeallocation(cleanupStack);
        system("pause");
        return STANDARD_ERROR;
    }
    
    size_t DesktopFilesProcessedCorrectly = 0;
    fprintf(logFile, "[DEBUG] Started searching for and processing .url files...\n");
    do
    {
        fprintf(logFile, "\n|==================================================================================|\n\n");
        IconProcessUnit *currentUnit = malloc(sizeof(IconProcessUnit));
        if (!currentUnit)
        {
            fprintf(logFile, "[ERROR] Failed to allocate memory for the current file processing object. malloc() failed.\n");
            continue;
        }
        *currentUnit = (IconProcessUnit){ .downloadingMode = DM_PageDefault };
        if (!PushGrowingList(&iconsProcessContainer.iconProcessUnits, currentUnit))
        {
            fprintf(logFile, "[ERROR] Failed to add the current icon process unit to (IconsProcessContainer::iconProcessUnits). PushGrowingList() failed.\n");
            continue;
        }

        if (!(currentUnit->fileName = GetDefaultPathInfo()))
        {
            fprintf(logFile, "[ERROR] Could not initialize (PathInfo *) for current file name. GetDefaultPathInfo() failed.\n");
            continue;
        }
        if (FAILED(StringCchCopyW(GetChangeableUtf16Path(currentUnit->fileName), MAX_PATH, fileData.cFileName)))
        {
            fprintf(logFile, "[ERROR] Failed to copy the current .url file name to currentUnit->fileName. StringCchCopyW() failed.\n");
            continue;
        }
        fprintf(logFile, "[DEBUG] File name: \"%s\"\n", GetUtf8PathMessage(currentUnit->fileName));

        if (!(currentUnit->desktopUrlFilePath = GetDefaultPathInfo()))
        {
            fprintf(logFile, "[ERROR] Could not initialize (PathInfo *) for current file path. GetDefaultPathInfo() failed\n");
            continue;
        }
        if (FAILED(StringCchPrintfW(GetChangeableUtf16Path(currentUnit->desktopUrlFilePath), MAX_PATH, L"%ls\\%ls", GetUtf16Path(desktopPath), GetUtf16Path(currentUnit->fileName))))
        {
            fprintf(logFile, "Could not build the absolute path to the current .url file. StringCchPrintfW() failed.\n");
            continue;
        }
        fprintf(logFile, "[DEBUG] absolute path: \"%s\".\n", GetUtf8PathMessage(currentUnit->desktopUrlFilePath));

        if (!(currentUnit->pageResponseFilePath = GetDefaultPathInfo()))
        {
            fprintf(logFile, "[ERROR] Could not initialize (PathInfo *) for \"%s\" response file path. GetDefaultPathInfo() failed.\n", GetUtf8PathMessage(pagesFolderName));
            continue;
        }
        if
        (
            FAILED(StringCchPrintfW(GetChangeableUtf16Path(currentUnit->pageResponseFilePath), MAX_PATH, L"%ls%ls", GetUtf16Path(pagesPath), GetUtf16Path(currentUnit->fileName))) ||
            PathCchRenameExtension(GetChangeableUtf16Path(currentUnit->pageResponseFilePath), MAX_PATH, DEFAULT_EXTENTION_OF_FILES_PAGES_L) != S_OK
        )
        {
            fprintf(logFile, "[ERROR] Failed to build the absolute path for the \"%s\" response file. StringCchPrintfW() or PathCchRenameExtension() failed.\n", GetUtf8PathMessage(pagesFolderName));
            continue;
        }
        fprintf(logFile, "[DEBUG] \"%s\" response file path: \"%s\"\n", 
            GetUtf8PathMessage(pagesFolderName), 
            GetUtf8PathMessage(currentUnit->pageResponseFilePath));

        if (FAILED(CoCreateInstance(
            &CLSID_InternetShortcut,
            NULL,
            CLSCTX_INPROC_SERVER,
            &IID_IPersistFile,
            (void **)&currentUnit->iPersistFile
        )))
        {
            fprintf(logFile, "[ERROR] Failed to create the CLSID_InternetShortcut COM object for currentUnit. CoCreateInstance() failed.\n");
            continue;
        }
        if (FAILED(currentUnit->iPersistFile->lpVtbl->Load(
            currentUnit->iPersistFile, GetUtf16Path(currentUnit->desktopUrlFilePath), STGM_READ)))
        {
            fprintf(logFile, "[ERROR] Failed to load the Internet Shortcut into currentUnit->iPersistFile. IPersistFile::Load() failed.\n");
            continue;
        }
        
        if (FAILED(currentUnit->iPersistFile->lpVtbl->QueryInterface(
            currentUnit->iPersistFile, &IID_IPropertySetStorage, &currentUnit->iPropertySetStorage)))
        {
            fprintf(logFile, "[ERROR] Failed to obtain the IPropertySetStorage interface. IPersistFile::QueryInterface() failed.\n");
            continue;
        }
        if (FAILED(currentUnit->iPropertySetStorage->lpVtbl->Open(
            currentUnit->iPropertySetStorage, &FMTID_Intshcut, STGM_READWRITE, &currentUnit->iPropertyStorage_Intshcut)))
        {
            fprintf(logFile, "[ERROR] Failed to obtain the IPropertyStorage interface from IPropertySetStorage. IPropertySetStorage::Open() failed.\n");
            continue;
        }

        if (userChoice == CHOICE_UPDATE_ICONS)
        {
            IUniformResourceLocatorW *iUniformResourceLocator;
            if (FAILED(currentUnit->iPersistFile->lpVtbl->QueryInterface(
                currentUnit->iPersistFile, &IID_IUniformResourceLocatorW, &iUniformResourceLocator)))
            {
                fprintf(logFile, "[ERROR] Failed to obtain the IUniformResourceLocator interface from IPersistFile. IPersistFile::QueryInterface() failed.\n");
                continue;
            }
            wchar_t *url;
            if (FAILED(iUniformResourceLocator->lpVtbl->GetURL(iUniformResourceLocator, &url)))
            {
                fprintf(logFile, "[ERROR] Failed to retrieve URL from Internet Shortcut. IUniformResourceLocatorW::GetURL() failed.\n");
                iUniformResourceLocator->lpVtbl->Release(iUniformResourceLocator);
                continue;
            }
            currentUnit->url = WstringToUtf8(url);
            CoTaskMemFree(url);
            iUniformResourceLocator->lpVtbl->Release(iUniformResourceLocator);
            if (!currentUnit->url)
            {
                fprintf(logFile, "[ERROR] Failed to convert URL to UTF-8. WstringToUtf8() failed.\n");
                continue;
            }
            fprintf(logFile, "[DEBUG] Extracted URL: %s.\n", currentUnit->url);
            
            if (!(currentUnit->responseFile = _wfopen(GetUtf16Path(currentUnit->pageResponseFilePath), L"wb")))
            {
                fprintf(logFile, "[ERROR] Failed to open the web page response file for writing. _wfopen() failed.\n");
                continue;
            }
            currentUnit->curlCallbackWriteStream = (StreamInfo){ currentUnit->responseFile, SIWrap_fwrite };
            
            if (!(currentUnit->easy = curl_easy_init()))
            {
                fprintf(logFile, "[ERROR] Failed to initialize libcurl easy handle for URL transfer. curl_easy_init() failed.\n");
                continue;
            }

            curl_easy_setopt(currentUnit->easy, CURLOPT_URL, currentUnit->url);                 //url по которому обращаться
            curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEFUNCTION, WriteCallback);          //колбек когда приходят данные
            curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEDATA, &currentUnit->curlCallbackWriteStream);  //параметр, с которым вызывается колбек
            curl_easy_setopt(currentUnit->easy, CURLOPT_PRIVATE, currentUnit);                  //ассоциация easy с IconProcessUnit
            curl_easy_setopt(currentUnit->easy, CURLOPT_TIMEOUT, 15L);                          //Запрос длиться не более 15 секунд
            curl_easy_setopt(currentUnit->easy, CURLOPT_FOLLOWLOCATION, 1L);                    //Редиректы
            curl_multi_add_handle(iconsProcessContainer.multi, currentUnit->easy);
        }

        if (userChoice == CHOICE_RESTORE_DEFAULT_ICONS)
        {
            PROPVARIANT setIconPathValues[2];
            PropVariantInit(&setIconPathValues[0]);
            PushCleanupStack(cleanupStack, Wrap_PropVariantClear, &setIconPathValues[0]);   //(14 глобально +1 временно ->15)
            PropVariantInit(&setIconPathValues[1]);
            PushCleanupStack(cleanupStack, Wrap_PropVariantClear, &setIconPathValues[1]);   //(14 глобально +2 временно ->16)

            if (FAILED(currentUnit->iPropertyStorage_Intshcut->lpVtbl->WriteMultiple(
                currentUnit->iPropertyStorage_Intshcut, 2, propspec, setIconPathValues, PID_FIRST_USABLE)))
            {
                fprintf(logFile, "[ERROR] Failed to reset icon properties to default values. IPropertyStorage::WriteMultiple() failed.\n");
                PartialDeallocation(cleanupStack, 2);   //(16->14)
                continue;
            }
            if (FAILED(currentUnit->iPropertyStorage_Intshcut->lpVtbl->Commit(currentUnit->iPropertyStorage_Intshcut, STGC_DEFAULT)))
            {
                fprintf(logFile, "[ERROR] Failed to commit property storage changes. IPropertyStorage::Commit() failed.\n");
                PartialDeallocation(cleanupStack, 2);   //(16->14)
                continue;
            }
            if (FAILED(currentUnit->iPersistFile->lpVtbl->Save(currentUnit->iPersistFile, GetUtf16Path(currentUnit->desktopUrlFilePath), FALSE)))
            {
                fprintf(logFile, "[ERRROR] Failed to save Internet Shortcut changes. IPersistFile::Save() failed.\n");
                PartialDeallocation(cleanupStack, 2);   //(16->14)
                continue;
            }
            fprintf(logFile, "[DEBUG] Icon path and icon index successfully reset.\n");
            PartialDeallocation(cleanupStack, 2);       //(16->16)
        }

        IconFileInfo *ifi = SearchGrowingList(&existingIcons, CompareIconFileInfoToIpuName, (void *)GetUtf16Path(currentUnit->fileName));
        if (ifi)
        {
            currentUnit->boundIcon = ifi;
            ifi->isBound = true;
            fprintf(logFile, "[DEBUG] Bound icon: \"%s\"\n", GetUtf8PathMessage(ifi->name));
        }
        else fprintf(logFile, "[DEBUG] Matching icon not found.\n");
        
        ++DesktopFilesProcessedCorrectly;
        fprintf(logFile, "[DEBUG] File processed successfully.\n");
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));
    if (GetLastError() != ERROR_NO_MORE_FILES)
    {
        FatalError("Error of searching files. FindNextFileW() or FindFirstFileW() failed.");
        return STANDARD_ERROR;
    }
    fprintf(logFile, "\n|==================================================================================|\n\n[DEBUG] Search and processing cycle completed.\n");

    if (userChoice == CHOICE_UPDATE_ICONS)
    {
        if (VIPS_INIT(APP_NAME))    //инициализация libvips
        {
            FatalError("Could not initialize libvips. VIPS_INIT() failed.");
            return STANDARD_ERROR;
        }
        PushCleanupStack(cleanupStack, Wrap_vips_shutdown, NULL);   // (14 глобально +1 локально ->15)

        fprintf(logFile, "[DEBUG] Started transfer cycle...\n");
        int runningHandles; //склько запросов ещё НЕ завершились
        size_t pagesResponses = 0, successfulPagesResponses = 0, faviconResponses = 0, successfulFaviconResponses = 0;
        size_t replacedIcons = 0, newIcons = 0;
        size_t successfulyUpdatedIcons = 0;
        do
        {
            wchar_t uriBuffer[URI_MAX_LENGTH];

            if(curl_multi_perform(iconsProcessContainer.multi, &runningHandles) != CURLM_OK)
            {
                FatalError("Failed to perform active transfers. curl_multi_perform() failed.");
                return STANDARD_ERROR;
            }
            curl_multi_wait(iconsProcessContainer.multi, NULL, 0, 1000, NULL);

            CURLMsg *curlMsg;
            int curlMsgLeft;
            while ((curlMsg = curl_multi_info_read(iconsProcessContainer.multi, &curlMsgLeft)))
            {
                if (curlMsg->msg == CURLMSG_DONE)
                {
                    StreamInfo logFileStream = { logFile, SIWrap_fwrite };
                    CURL *easy = curlMsg->easy_handle;  //завершенный easy
                    IconProcessUnit *ipu = NULL;
                    if (curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ipu) != CURLE_OK)
                    {
                        CurlGetinfoFailMessage(&logFileStream, "CURLINFO_PRIVATE");
                        continue;
                    }

                    long responseCode;
                    unsigned char *contentType = NULL;
                    switch (ipu->downloadingMode)
                    {
                        case DM_PageDefault:
                        {
                            ++pagesResponses;
                            StreamInfo logBufferStream = { &ipu->logBuffer, SIWrap_WriteMemoryBuffer };
                            
                            PrintStream
                            (
                                &logBufferStream,
                                "\n|==================================================================================|\n\n[DEBUG] Page transfer finished.\n[DEBUG] Exit code:               %s.\n[DEBUG] Initial URL:             %s.\n", 
                                curl_easy_strerror(curlMsg->data.result), 
                                ipu->url
                            );
                            unsigned char *lastUrl = NULL;
                            if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &lastUrl) != CURLE_OK)
                            {
                                FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                CurlGetinfoFailMessage(&logFileStream, "CURLINFO_EFFECTIVE_URL");
                                continue;
                            }
                            PrintStream(&logBufferStream, "[DEBUG] last used effective URL: %s.\n", lastUrl);
                            
                            if (!WriteCurlResponseCode(easy, &logBufferStream, &responseCode))
                            {
                                FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                continue;
                            } 
                            if (!WriteCurlContentType(easy, &logBufferStream, &contentType))
                            {                        
                                FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                continue;
                            }
                            
                            bool ableParseHTML = true;
                            if (responseCode != 200)
                            {
                                PrintStream(&logBufferStream, "[ERROR] Response code is not 200.\n");
                                ableParseHTML = false;
                            }
                            if (strncmp(contentType, TARGET_PAGE_RESPONSE_TYPE, strlen(TARGET_PAGE_RESPONSE_TYPE)))
                            {
                                PrintStream(&logBufferStream, "[ERROR] Response content type was not recognized as \"%s\".\n", TARGET_PAGE_RESPONSE_TYPE);
                                ableParseHTML = false;
                            }

                            if (ableParseHTML)
                            {
                                fclose(ipu->responseFile);
                                if (!(ipu->responseFile = _wfopen(GetUtf16Path(ipu->pageResponseFilePath), L"rb")))
                                {
                                    FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                    PrintStream(&logFileStream, "[ERROR] Could not open response file. _wfopen() failed.\n");
                                    continue;
                                }
                                WIN32_FILE_ATTRIBUTE_DATA responceFileData;
                                if (!GetFileAttributesExW(GetUtf16Path(ipu->pageResponseFilePath), GetFileExInfoStandard, &responceFileData))
                                {
                                    FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                    PrintStream(&logFileStream, "[ERROR] Could not get size of response file. GetFileAttributesExW() failed.\n");
                                    continue;
                                }
                                ULARGE_INTEGER responceFileSize = { .LowPart = responceFileData.nFileSizeLow, .HighPart = responceFileData.nFileSizeHigh };
                                if (responceFileSize.QuadPart > INT_MAX)
                                {
                                    FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                    PrintStream(&logFileStream, "[ERROR] Response size exceeds INT_MAX and can not be processed.\n");
                                    continue;
                                }
                                unsigned char *buffer = malloc(responceFileSize.QuadPart);
                                if (!buffer)
                                {
                                    FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                    PrintStream(&logFileStream, "[ERROR] Failed to allocate memory for the web page response buffer. malloc() failed.\n");
                                    continue;
                                }
                                PushCleanupStack(cleanupStack, Wrap_Free, buffer);     //(14 глобально +1 локально +1 временно ->16)

                                fread(buffer, 1, responceFileSize.QuadPart, ipu->responseFile);
                                unsigned char *faviconUrl = GetFaviconUrl(buffer, (int)responceFileSize.QuadPart, lastUrl);
                                if(!faviconUrl)
                                {
                                    FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                                    PrintStream(&logFileStream, "[ERROR] Could not get favicon URL. GetFaviconUrl() failed.\n");
                                    SingleDeallocation(cleanupStack);   //(16->15)
                                    continue;
                                }
                                PushCleanupStack(cleanupStack, Wrap_Free, faviconUrl);     //(14 глобально +1 локально +2 временно ->17)
                                PrintStream(&logBufferStream, "[DEBUG] Favicon URL: %s.\n", faviconUrl);
                                
                                curl_multi_remove_handle(iconsProcessContainer.multi, easy);
                                curl_easy_setopt(easy, CURLOPT_URL, faviconUrl); // перенастраиваем
                                curl_easy_setopt(easy, CURLOPT_TIMEOUT, 8L);
                                curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
                                curl_multi_add_handle(iconsProcessContainer.multi, easy);

                                fclose(ipu->responseFile);
                                ipu->responseFile = NULL;
                                ipu->curlCallbackWriteStream = (StreamInfo){ &ipu->faviconBuffer, SIWrap_WriteMemoryBuffer};
                                ipu->downloadingMode = DM_Favicon;

                                ++successfulPagesResponses;
                                PrintStream(&logBufferStream, "[DEBUG] Successfuly configured curl easy handle to download favicon.\n");
                                PartialDeallocation(cleanupStack, 2);       //(17->15)
                            }
                            else FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                            break;
                        }
                        case DM_Favicon:
                        {
                            ++faviconResponses;
                            FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                            fprintf(logFile, "\n        -------- Favicon response --------\n\n");

                            if (!WriteCurlResponseCode(easy, &logFileStream, &responseCode)) continue;
                            if (!WriteCurlContentType(easy, &logFileStream, &contentType)) continue;

                            if (!(ipu->faviconResponseFilePath = GetDefaultPathInfo()))
                            {
                                fprintf(logFile, "[ERROR] Could not initialize (PathInfo *) for \"%s\" response file path. GetDefaultPathInfo() failed.\n", GetUtf8PathMessage(faviconsFolderName));
                                continue;
                            }
                            ImageContentType *imageType;
                            if 
                            (
                                !(imageType = GetExtentionFromContentType(contentType)) ||
                                FAILED(StringCchPrintfW(GetChangeableUtf16Path(ipu->faviconResponseFilePath), MAX_PATH, L"%ls%ls", GetUtf16Path(faviconsPath), GetUtf16Path(ipu->fileName))) ||
                                PathCchRenameExtension(GetChangeableUtf16Path(ipu->faviconResponseFilePath), MAX_PATH, imageType->extention) != S_OK
                            )
                            {
                                fprintf(logFile, "[ERROR] Could not build path to file for \"%s\" response. The format may not be supported.\n", GetUtf8PathMessage(faviconsFolderName));
                                continue;
                            }
                            fprintf(logFile, "[DEBUG] Extention of file for \"%s\" response: %s.\n", GetUtf8PathMessage(faviconsFolderName), imageType->extentionUtf8);
                            fprintf(logFile, "[DEBUG] Built path of file for \"%s\" response: \"%s\".\n", GetUtf8PathMessage(faviconsFolderName), GetUtf8PathMessage(ipu->faviconResponseFilePath));

                            if (!(ipu->responseFile = _wfopen(GetUtf16Path(ipu->faviconResponseFilePath), L"wb")))
                            {
                                fprintf(logFile, "[ERROR] Could not open file for \"%s\" response.\n", GetUtf8PathMessage(faviconsFolderName));
                                continue;
                            }
                            FlushMemoryBufferToFile(&ipu->faviconBuffer, ipu->responseFile);

                            fprintf(logFile, "[DEBUG] Successfuly download \"%s\".\n", GetUtf8PathMessage(faviconsFolderName));
                            ++successfulFaviconResponses;

                            
                            //создать иконку для рабочего стола
                            fprintf(logFile, "\n        --------   Making icon    --------\n\n");

                            PathInfoPtr iconFilePathPtr;
                            if (ipu->boundIcon)
                            {
                                ++replacedIcons;
                                iconFilePathPtr = (PathInfoPtr){ ipu->boundIcon->path, false };
                                fprintf(logFile, "[DEBUG] Existing icon will be replaced.\n");
                            }
                            else
                            {
                                ++newIcons;
                                fprintf(logFile, "[DEBUG] A new icon will be created.\n");
                                PathInfo *newIconFilePath = GetDefaultPathInfo();
                                if
                                (
                                    FAILED(StringCchPrintfW(GetChangeableUtf16Path(newIconFilePath), MAX_PATH, L"%ls\\resources\\icons\\%ls", GetUtf16Path(appFolder), GetUtf16Path(ipu->fileName))) ||
                                    PathCchRenameExtension(GetChangeableUtf16Path(newIconFilePath), MAX_PATH, ICO_W) != S_OK
                                )
                                {
                                    fprintf(logFile, "[ERROR] Could not build path to image file. StringCchPrintfW() or PathCchRenameExtension() failed\n");
                                    DestroyPathInfo(newIconFilePath);
                                    continue;
                                }
                                iconFilePathPtr = (PathInfoPtr){ newIconFilePath, true };
                                fprintf("[DEBUG] Built path for new icon: \"%s\"\n", GetUtf8PathMessage(newIconFilePath));
                            }

                            FILE *iconFile = _wfopen(GetUtf16Path(iconFilePathPtr.ptr), L"wb");
                            if (iconFilePathPtr.isOriginal) DestroyPathInfo(iconFilePathPtr.ptr);
                            if (!iconFile)
                            {
                                fprintf(logFile, "[ERROR] Could not open icon file. _wfopen() failed\n");
                                continue;
                            }
                            PushCleanupStack(cleanupStack, Wrap_FClose, iconFile);  //(14 глобально +1 локально +1 временно ->16))

                            if (imageType->type != imgTypeIco)
                            {
                                imageBufferInfo iconBuffer;
                                ICONDIRENTRY iconDirectoryEntry;
                                iconDirectoryEntry.planes = 1;
                                iconDirectoryEntry.palette = 0;
                                iconDirectoryEntry.reserved = 0;
                                iconDirectoryEntry.offset = sizeof(ICONDIR) + sizeof(ICONDIRENTRY);

                                if (imageType->type != imgTypePng)
                                {
                                    VipsImage *image = NULL;
                                    double scale;
                                    if (imageType->type == imgTypeSvg)                                                                          //svg
                                    {
                                        VipsImage *tempSvgImage = NULL;
                                        if (vips_svgload_buffer(ipu->faviconBuffer.content, ipu->faviconBuffer.length, &tempSvgImage, NULL))
                                        {
                                            fprintf(logFile, "[ERROR] Failed to load SVG image. vips_svgload_buffer() failed.\n");
                                            SingleDeallocation(cleanupStack);   //(16->15)
                                            continue;
                                        }
                                        int svgWidth = vips_image_get_width(tempSvgImage);
                                        int svgHeight = vips_image_get_height(tempSvgImage);
                                        Wrap_g_object_unref(tempSvgImage);
                                        if (svgWidth != svgHeight)
                                        {
                                            fprintf(logFile, "[ERROR] SVG image is not square.\n");
                                            SingleDeallocation(cleanupStack);   //(16->15)
                                            continue;
                                        }
                                        scale = 256.0 / svgWidth;
                                        if (vips_svgload_buffer(ipu->faviconBuffer.content, ipu->faviconBuffer.length, &image, NULL))
                                        {
                                            fprintf(logFile, "[ERROR] Failed to load SVG image. vips_svgload_buffer() failed.\n");
                                            SingleDeallocation(cleanupStack);   //(16->15)
                                            continue;
                                        }
                                    }
                                    else        //gif, jpeg, webp, bmp, tiff, avif, apng
                                    {
                                        image = vips_image_new_from_buffer(ipu->faviconBuffer.content, ipu->faviconBuffer.length, "", NULL);
                                        scale = 1;
                                    } 
                                    if (!image)
                                    {
                                        fprintf(logFile, "[ERROR] Could not load image file to build icon. vips_image_new_from_file() failed.\n");
                                        SingleDeallocation(cleanupStack);       //(16->15)
                                        continue;
                                    }
                                    PushCleanupStack(cleanupStack, Wrap_g_object_unref, image); //(14 глобально +1 локально +2 временно ->17))
                                    PathInfo *pngOutputFilePath = GetDefaultPathInfo();
                                    PushCleanupStack(cleanupStack, Wrap_DestroyPathInfo, pngOutputFilePath); //(14 глобально +1 локально +3 временно ->18)
                                    if 
                                    (
                                        FAILED(StringCchPrintfW(GetChangeableUtf16Path(pngOutputFilePath), MAX_PATH, L"%ls%ls", GetUtf16Path(folderTempResources), GetUtf16Path(ipu->fileName))) ||
                                        PathCchRenameExtension(GetChangeableUtf16Path(pngOutputFilePath), MAX_PATH, PNG_W) != S_OK
                                    )
                                    {
                                        fprintf(logFile, "[ERROR] Could not build path to image file. StringCchPrintfW() or PathCchRenameExtension() failed.\n");
                                        PartialDeallocation(cleanupStack, 3);       //(18->15)
                                        continue;
                                    }
                                    if (!GetUtf8Path(pngOutputFilePath))
                                    {
                                        fprintf(logFile, "[ERROR] Failed to convert image path to UTF-8. WstringToUtf8() failed.\n");
                                        PartialDeallocation(cleanupStack, 3);       //(18->15)
                                        continue;
                                    }

                                    if (vips_pngsave(image, GetUtf8Path(pngOutputFilePath), NULL))
                                    {
                                        fprintf(logFile, "[ERROR] Could not save icon as a PNG file. vips_pngsave() failed.\n");
                                        PartialDeallocation(cleanupStack, 3);       //(18->15)
                                        continue;
                                    }

                                    if (!(iconBuffer.buffer = malloc(sizeof(MemoryBuffer))))
                                    {
                                        fprintf(logFile, "[ERROR] Could not allocate memory for (imageBufferInfo *)(iconBuffer.buffer). malloc() failed.\n");
                                        PartialDeallocation(cleanupStack, 3);       //(18->15)
                                        continue;
                                    }
                                    iconBuffer.isОriginal = true;
                                    if (vips_pngsave_buffer(image, (void **)&iconBuffer.buffer->content, &iconBuffer.buffer->length, NULL) != 0)
                                    {
                                        fprintf(logFile, "[ERROR] Could not save icon to buffer. vips_pngsave_buffer() failed.\n");
                                        PartialDeallocation(cleanupStack, 3);       //(18->15)
                                        continue;
                                    }

                                    uint16_t bpp = BPP_DEFAULT;
                                    switch (image->Bands)
                                    {
                                        case 1: bpp = 8;  break;
                                        case 3: bpp = 24; break;
                                        case 4: bpp = 32; break;
                                    }
                                    iconDirectoryEntry.bpp = bpp;
                                    iconDirectoryEntry.width = image->Xsize == 256 ? 0 : (uint8_t)image->Xsize;
                                    iconDirectoryEntry.height = image->Ysize == 256 ? 0 : (uint8_t)image->Ysize;

                                    fprintf(logFile, "[DEBUG] Successfuly saved temporary .png file: \"%s\"\n", GetUtf8PathMessage(pngOutputFilePath));
                                    PartialDeallocation(cleanupStack, 2);       //(18->16) оставляем открытый (FILE *)iconFile
                                }
                                else
                                {
                                    iconBuffer = (imageBufferInfo){ &ipu->faviconBuffer, false };
                                    if (iconBuffer.buffer->length < PNG_MIN_LENGTH)
                                    {
                                        fprintf(logFile, "[ERROR] File is too small.");
                                        continue;
                                    }
                                    if (memcmp(iconBuffer.buffer->content, pngSignature, 8) != 0)
                                    {
                                        fprintf(logFile, "[ERROR] Invalid PNG file signature.");
                                        continue;
                                    }
                                    unsigned char *data = iconBuffer.buffer->content;
                                    uint32_t width  =   ((uint32_t)data[16] << 24)  | ((uint32_t)data[17] << 16)  | ((uint32_t)data[18] << 8) | (uint32_t)data[19]; //первые байты самые "важные" (с 16 до 19)
                                    uint32_t height =   ((uint32_t)data[20] << 24)  | ((uint32_t)data[21] << 16)  | ((uint32_t)data[22] << 8) | (uint32_t)data[23]; //тоже самое (с 20 до 24)

                                    uint8_t bit_depth = data[24];     // 8-й байт IHDR
                                    uint16_t bpp = BPP_DEFAULT;
                                    switch (data[25])   // color type
                                    {
                                        case 0:  bpp = bit_depth;       break; // Grayscale
                                        case 2:  bpp = bit_depth * 3;   break; // RGB
                                        case 3:  bpp = bit_depth;       break; // Indexed (palette)
                                        case 4:  bpp = bit_depth * 2;   break; // Grayscale + Alpha
                                        case 6:  bpp = bit_depth * 4;   break; // RGBA
                                    }

                                    iconDirectoryEntry.width  = (width == 256) ? 0 : (unsigned char)width;
                                    iconDirectoryEntry.height = (height == 256) ? 0 : (unsigned char)height;
                                    iconDirectoryEntry.bpp    = bpp;   
                                }
                                if (iconBuffer.buffer->length > UINT32_MAX)
                                {
                                    fprintf(logFile, "[ERROR] Image is too large. File is not supported.\n");
                                    SingleDeallocation(cleanupStack);       //(16->15)
                                    continue;
                                }
                                iconDirectoryEntry.size = iconBuffer.buffer->length;

                                fwrite(&iconHeader, sizeof(iconHeader), 1, iconFile);
                                fwrite(&iconDirectoryEntry, sizeof(iconDirectoryEntry), 1, iconFile);
                                fwrite(iconBuffer.buffer->content, 1, iconBuffer.buffer->length, iconFile);

                                if (iconBuffer.isОriginal) 
                                {
                                    MemoryBufferDestructor(iconBuffer.buffer);
                                    free(iconBuffer.buffer);
                                }
                            }
                            else fwrite(ipu->faviconBuffer.content, 1, ipu->faviconBuffer.length, iconFile);

                            fprintf(logFile, "[DEBUG] Icon saved successfully.\n");
                            SingleDeallocation(cleanupStack);    // (16->15) закрываем (FILE *)iconFile

                            //сохранить скаченную иконку в COM-объект и на рабочий стол
                            uint32_t uriLength = ARRAYSIZE(uriBuffer);
                            if (FAILED(UrlCreateFromPathW(GetUtf16Path(iconFilePathPtr.ptr), uriBuffer, &uriLength, 0)))
                            {
                                fprintf(logFile, "[ERROR] Failed to create URI for icon configuration in the COM object. UrlCreateFromPathW() failed.\n");
                                continue;
                            }

                            PROPVARIANT setIconPathValues[2];
                            PropVariantInit(&setIconPathValues[0]);
                            PushCleanupStack(cleanupStack, Wrap_PropVariantClear, &setIconPathValues[0]);   //(14 глобально +1 локально +1 временно ->16)
                            setIconPathValues[0].vt = VT_LPWSTR;
                            setIconPathValues[0].pwszVal = CoTaskMemAlloc((uriLength + 1) * sizeof(wchar_t));
                            if (!setIconPathValues[0].pwszVal)
                            {
                                fprintf(logFile, "[ERRPR] Failed to allocate memory from the COM heap for the URI string. CoTaskMemAlloc() failed.\n");
                                SingleDeallocation(cleanupStack);   //(16->15)
                                continue;
                            }
                            memcpy(setIconPathValues[0].pwszVal, uriBuffer, uriLength * sizeof(wchar_t));
                            setIconPathValues[0].pwszVal[uriLength] = L'\0';

                            PropVariantInit(&setIconPathValues[1]);
                            PushCleanupStack(cleanupStack, Wrap_PropVariantClear, &setIconPathValues[1]);   //(14 глобально +1 локально +2 временно ->17)
                            setIconPathValues[1].vt = VT_I4;
                            setIconPathValues[1].lVal = 0;

                            if (FAILED(ipu->iPropertyStorage_Intshcut->lpVtbl->WriteMultiple(
                                ipu->iPropertyStorage_Intshcut, 2, propspec, setIconPathValues, PID_FIRST_USABLE)))
                            {
                                fprintf(logFile, "[ERROR] Failed to write new icon properties to property storage. IPropertyStorage::WriteMultiple() failed.\n");
                                PartialDeallocation(cleanupStack, 2);   //(17->15)
                                continue;
                            }
                            if (FAILED(ipu->iPropertyStorage_Intshcut->lpVtbl->Commit(ipu->iPropertyStorage_Intshcut, STGC_DEFAULT)))
                            {
                                fprintf(logFile, "[ERROR] Failed to commit property storage changes. IPropertyStorage::Commit() failed.\n");
                                PartialDeallocation(cleanupStack, 2);   //(17->15)
                                continue;
                            }
                            if (FAILED(ipu->iPersistFile->lpVtbl->Save(ipu->iPersistFile, GetUtf16Path(ipu->desktopUrlFilePath), FALSE)))
                            {
                                fprintf(logFile, "[ERRROR] Failed to save Internet Shortcut changes. IPersistFile::Save() failed.\n");
                                PartialDeallocation(cleanupStack, 2);   //(17->15)
                                continue;
                            }
                            fprintf(logFile, "[DEBUG] Internet Shortcut icon updated successfully.\n");
                            
                            ++successfulyUpdatedIcons;
                            PartialDeallocation(cleanupStack, 2);       //(17->15)
                            break;
                        }
                    }
                }
            }
        }
        while (runningHandles);
        fprintf(logFile, "\n|==================================================================================|\n\n[DEBUG] Transfers cycle completed.\n");
        
        fprintf(logFile, "[INFO] \"%s\" responses: %zd\n[INFO] Successful \"%s\" responses: %zd\n", GetUtf8PathMessage(pagesFolderName), pagesResponses, GetUtf8PathMessage(pagesFolderName), successfulPagesResponses);
        fprintf(logFile, "[INFO] \"%s\" responses: %zd\n[INFO] Successful \"%s\" responses: %zd\n", GetUtf8PathMessage(faviconsFolderName), faviconResponses, GetUtf8PathMessage(faviconsFolderName), successfulFaviconResponses);
        fprintf(logFile, "[DEBUG] New icon files: %zu\n", newIcons);
        fprintf(logFile, "[DEBUG] Replaced icon files: %zu\n", replacedIcons);
        fprintf(logFile, "[DEBUG] Successfuly updated internet shortcut's icons: %zd", successfulyUpdatedIcons);

        printf("Updated icons: %zd\n", successfulyUpdatedIcons);
        printf("For more information, see the log file at logs/app.log\n");

        SingleDeallocation(cleanupStack);    //(15->14)
    }

    fprintf(logFile, "[DEBUG] Deleting redundant icons ...\n");
    bool deletedAnyFiles = false;
    size_t deletedIcons = 0;
    for (size_t i = 0; i < existingIcons.length; ++i)
    {
        IconFileInfo *ifi = GetGrowingList(&existingIcons, i);
        if (!ifi->isBound)
        {
            if (!deletedAnyFiles) fprintf(logFile, "\n");
            deletedAnyFiles = true;
            if (DeleteFileW(GetUtf16Path(ifi->path)))
            {
                ++deletedIcons;
                fprintf(logFile, "[DEBUG] Deleted: \"%s\".\n", GetUtf8PathMessage(ifi->name));
            } 
            else fprintf(logFile, "[ERROR] Could not delete file: \"%s\".\n", GetUtf8PathMessage(ifi->name));
        }
    }
    if (deletedAnyFiles) fprintf(logFile, "\n");
    else fprintf(logFile, "[DEBUG] No such files.\n");

    fprintf(logFile, "[INFO] Found files: %zd.\n", iconsProcessContainer.iconProcessUnits.length);
    fprintf(logFile, "[INFO] Processed files correctly: %zd.\n", DesktopFilesProcessedCorrectly);
    fprintf(logFile, "[DEBUG] Deleted icon files: %zu.\n", deletedIcons);
    
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);   //для обновления ярлыков
    printf("Done.\n");
    CompleteDeallocation(cleanupStack);
    system("pause");
    return 0;
}



size_t WriteCallback
(
    char *ptr,      //указатель на пришедшие данные
    size_t size,    //size * nmemb = количество пришедших байт
    size_t nmemb,   
    void *userdata  //пользовательские данные. Задаётся через curl_easy_setopt(easy, CURLOPT_WRITEDATA, somePtr). Я здесь получаю IconProcessUnit *
)
{
    WriteStream((StreamInfo *)userdata, ptr, size *= nmemb);
    return size;    //функция должна возвращать количество обработаных байтов
}

bool DropDirectory(const wchar_t *directory)
{
    wchar_t searchToken[MAX_PATH];
    bool returnValue = true;
    if(FAILED(StringCchPrintfW(searchToken, MAX_PATH, L"%ls*", directory))) return false;
    WIN32_FIND_DATAW fileData;
    HANDLE searchHandle = FindFirstFileW(searchToken, &fileData);        // получить дескриптор поиска и получить первый файл
    if (searchHandle == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;    //папка пуста
        return false;
    }
    do
    {
        wchar_t absoluteFilePath[MAX_PATH];
        if (FAILED(StringCchPrintfW(absoluteFilePath, MAX_PATH, L"%ls%ls", directory, fileData.cFileName)))
        {
            returnValue = false;
            continue;
        }
        if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if
            (
                wcscmp(fileData.cFileName, L".") == 0 ||
                wcscmp(fileData.cFileName, L"..") == 0
            ) continue;
            if (SUCCEEDED(StringCchCatW(absoluteFilePath, MAX_PATH, L"\\")))
            {
                returnValue = false;
                continue;
            }
            if (!DropDirectory(absoluteFilePath))
            {
                returnValue = false;
                continue;
            }
            if (!RemoveDirectoryW(absoluteFilePath)) returnValue = false;
        }
        else if(!DeleteFileW(absoluteFilePath)) returnValue = false;
    } 
    while (FindNextFileW(searchHandle, &fileData));
    if (GetLastError() != ERROR_NO_MORE_FILES) returnValue = false;
    FindClose(searchHandle);
    return returnValue;
}

int vMakeMessage(unsigned char **buffer, const unsigned char *format, va_list ap)
{
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(NULL, 0, format, ap_copy);
    va_end(ap_copy);
    if (len >= 0)
    {
        if (*buffer = malloc((size_t)len + 1))
        {
            vsnprintf(*buffer, (size_t)len + 1, format, ap);
            return len;
        }
    }
    else *buffer = NULL;
    return -1;
}

int MakeMessage(unsigned char **buffer, const unsigned char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int len = vMakeMessage(buffer, format, ap);
    va_end(ap);
    return len;
}

int PrintStream(StreamInfo *info, const unsigned char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    unsigned char *data;
    int dataLength = vMakeMessage(&data, format, ap);
    va_end(ap);
    if (dataLength < 0) return -1;
    bool res = WriteStream(info, data, dataLength);
    free(data);
    return res ? dataLength : -1;
}

void CurlGetinfoFailMessage(StreamInfo *streamInfo, const unsigned char *type) 
{
    PrintStream(streamInfo, "[ERROR] Could not get info from curl easy handle. curl_easy_getinfo(%s) failed.\n", type);
}

bool WriteCurlResponseCode(CURL *easy, StreamInfo *stream, long *responseCode)
{
    if (curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, responseCode) == CURLE_OK)
    {
        PrintStream(stream, "[DEBUG] Response code:           %ld.\n", *responseCode);
        return true;
    }
    else 
    {
        CurlGetinfoFailMessage(stream, "CURLINFO_RESPONSE_CODE");
        return false;
    }
}

bool WriteCurlContentType(CURL *easy, StreamInfo *stream, unsigned char **contentType)
{
    if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, contentType) == CURLE_OK)
    {
        if (*contentType)
        {
            PrintStream(stream, "[DEBUG] Content type:            %s.\n", *contentType);
            return true;
        }
        else PrintStream(stream, "[ERROR] The server did not send a valid Content-Type header or the protocol used does not support this.\n");
    }
    else CurlGetinfoFailMessage(stream, "CURLINFO_CONTENT_TYPE");
    return false;
}

bool WriteStream(StreamInfo *info, const unsigned char *data, size_t dataLength)
{
    return info->write(info->stream, data, dataLength);
}

bool SIWrap_fwrite(void *file, const unsigned char *data, size_t dataLength)
{
    fwrite(data, 1, dataLength, (FILE *)file);
    return true;
}

bool SIWrap_WriteMemoryBuffer(void *memoryBuffer, const unsigned char *data, size_t dataLength)
{
    return WriteMemoryBuffer((MemoryBuffer *)memoryBuffer, data, dataLength);
}

const ImageContentType *GetExtentionFromContentType(const unsigned char *contentType)
{
    for (size_t i = 0; i < contentTypes.quantity; ++i)
    {
        if 
        (
            strncmp
            (
                contentType, 
                contentTypes.array[i].faviconContentType, 
                strlen(contentTypes.array[i].faviconContentType)
            ) == 0
        )  return &contentTypes.array[i].contentType;
    }
    return NULL;
}

bool CompareIconFileInfoToIpuName(void *ifiVoidPtr, void *desiredNameVoidPtr)
{
    IconFileInfo *ifi = ifiVoidPtr;
    if (ifi->isBound) return false;
    const wchar_t *desiredName = desiredNameVoidPtr;
    
    const wchar_t *urlNameExtentionPtr = PathFindExtensionW(desiredName);
    const wchar_t *icoNameExtentionPtr = PathFindExtensionW(GetUtf16Path(ifi->name));
    size_t lenUrl = urlNameExtentionPtr - desiredName;
    size_t lenIco = icoNameExtentionPtr - GetUtf16Path(ifi->name);
    if
    (
        lenUrl == lenIco &&
        wmemcmp(desiredName, GetUtf16Path(ifi->name), lenUrl) == 0
    ) return true;
    return false;
}

static void FatalError(const unsigned char *message)
{
    unsigned char nullMessage[] = "Could not build error message.";
    if(logFile)
    {
        fprintf(logFile, "[FATAL ERROR] %s\n", message ? message : nullMessage);
        MessageBoxA(NULL, "The program terminated due to a fatal error. See the log file for details.", NULL, MB_OK);
    } 
    else MessageBoxA(NULL, message ? message : nullMessage, NULL, MB_OK);
    if (cleanupStack) CompleteDeallocation(cleanupStack);
}



void Wrap_FindClose(void *arg)
{
    FindClose(*(HANDLE *)arg);
}

void Wrap_CoTaskMemFree(void *arg)
{
    CoTaskMemFree((wchar_t *)arg);
}

void IconsProcessContainerDestructor(void *arg)
{
    IconsProcessContainer *container = (IconsProcessContainer *)arg;
    for (size_t i; i < container->iconProcessUnits.length; ++i)
    {
        IconProcessUnit *unit = GetGrowingList(&container->iconProcessUnits, i);
        free(unit->url);
        if (unit->iPropertyStorage_Intshcut) unit->iPropertyStorage_Intshcut->lpVtbl->Release(unit->iPropertyStorage_Intshcut);
        if (unit->iPropertySetStorage) unit->iPropertySetStorage->lpVtbl->Release(unit->iPropertySetStorage);
        if (unit->iPersistFile) unit->iPersistFile->lpVtbl->Release(unit->iPersistFile);
        if (unit->fileName) DestroyPathInfo(unit->fileName);
        if (unit->desktopUrlFilePath) DestroyPathInfo(unit->desktopUrlFilePath);
        if (unit->pageResponseFilePath) DestroyPathInfo(unit->pageResponseFilePath);
        if (unit->faviconResponseFilePath) DestroyPathInfo(unit->faviconResponseFilePath);
        MemoryBufferDestructor(&unit->logBuffer);
        MemoryBufferDestructor(&unit->faviconBuffer);
        if (unit->responseFile) fclose(unit->responseFile);
        if(unit->easy)
        {
            curl_multi_remove_handle(container->multi, unit->easy);
            curl_easy_cleanup(unit->easy);
        }
        free(unit);
    }
    DestroyGrowingList(&container->iconProcessUnits);
    curl_multi_cleanup(container->multi);
}

void Wrap_FClose(void *arg)
{
    fclose((FILE *)arg);
}

void Wrap_curl_global_cleanup(void *arg)
{
    curl_global_cleanup();
}

void Wrap_vips_shutdown(void *arg)
{
    vips_shutdown();
}

void Wrap_g_object_unref(void *arg)
{
    g_object_unref((VipsImage *)arg);
}

void Wrap_DestroyGrowingList(void *arg)
{
    DestroyGrowingList((GrowingList *)arg);
}

void Wrap_CoUninitialize(void *arg)
{
    CoUninitialize();
}

void Wrap_PropVariantClear(void *arg)
{
    PropVariantClear((PROPVARIANT *)arg);
}

void Wrap_DestroyPathInfo(void *arg)
{
    DestroyPathInfo((PathInfo *)arg);
}

void DestroyIconFileInfo(void *arg)
{
    IconFileInfo *ifi = arg;
    if (ifi->name) DestroyPathInfo(ifi->name);
    if (ifi->path) DestroyPathInfo(ifi->path);
    free(ifi);
}


unsigned char *GetFaviconUrl(const unsigned char *buffer, int size, const unsigned char *base_url)
{
    xmlDocPtr doc = htmlReadMemory(
        buffer,
        size,
        NULL,
        NULL,
        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING
    );
    if (!doc) return NULL;

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx)
    {
        xmlFreeDoc(doc);
        return NULL;
    }
    // shortcut icon в приоритете
    xmlXPathObjectPtr result = xmlXPathEvalExpression
    (
        (xmlChar*)"//link[contains(translate(@rel,'ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz'),'shortcut') and contains(translate(@rel,'ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz'),'icon')]/@href",
        ctx
    );

    xmlChar *href = NULL;
    if (result && result->nodesetval && result->nodesetval->nodeNr > 0)
        href = xmlNodeGetContent(result->nodesetval->nodeTab[0]);
    // fallback: просто icon (если shortcut icon нет)
    if (!href)
    {
        xmlXPathFreeObject(result);
        result = xmlXPathEvalExpression
        (
            (xmlChar*)"//link[contains(translate(@rel,'ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz'),'icon')]/@href",
            ctx
        );
        if (result && result->nodesetval && result->nodesetval->nodeNr > 0)
            href = xmlNodeGetContent(result->nodesetval->nodeTab[0]);
    }

    unsigned char *finalUrl = NULL;
    if (href)
    {
        // если ссылка абсолютная
        if (strstr((unsigned char *)href, "http://") || strstr((unsigned char *)href, "https://"))
            finalUrl = strdup((unsigned char *)href);
        else
        {
            // относительная -> делаем абсолютную
            xmlChar *abs = xmlBuildURI(href, (xmlChar *)base_url);
            if (abs)
            {
                finalUrl = strdup((unsigned char *)abs);
                xmlFree(abs);
            }
        }
        xmlFree(href);
    }

    xmlXPathFreeObject(result);
    xmlXPathFreeContext(ctx);
    xmlFreeDoc(doc);
    return finalUrl;
}