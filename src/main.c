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

#include "curl/curl.h"

#include "libxml/HTMLparser.h"
#include "libxml/xpath.h"
#include "libxml/uri.h"

#include "vips.h"

#include "cleanup interface.h"
#include "small parser.h"
#include "growing list.h"
#include "memory buffer.h"
#define STANDARD_ERROR (-1)
#define MAX_CURRENT_SYSTEM_RESOURCES (12)
#define INICIAL_BUFFER_LENGTH (12)
#define BUFFER_ADDITION (8)

#define FOLDER_PAGES_L L"pages"
#define FOLDER_FAVICONS_L L"favicons"
#define DEFAULT_EXTENTION_OF_FILES_PAGES_L L"html"
#define TARGET_PAGE_RESPONSE_TYPE "text/html"


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
    wchar_t *extention;
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

#define PNG_W L"png"
#define ICO_W L"ico"
const ContentTypesMatchingContainer contentTypes = 
{
    {
        { { ICO_W,      imgTypeIco  },  "image/vnd.microsoft.icon"  },
        { { ICO_W,      imgTypeIco  },  "image/x-icon",             },
        { { ICO_W,      imgTypeIco  },  "image/ico",                },
        { { ICO_W,      imgTypeIco  },  "image/icon",               },
        { { PNG_W,      imgTypePng  },  "image/png",                },
        { { L"svg",     imgTypeSvg  },  "image/svg+xml",            },
        { { L"gif",     imgTypeGif  },  "image/gif",                },
        { { L"jpeg",    imgTypeJpeg },  "image/jpeg",               },
        { { L"webp",    imgTypeWebp },  "image/webp",               },
        { { L"bmp",     imgTypeBmp  },  "image/bmp",                },
        { { L"bmp",     imgTypeBmp  },  "image/x-bmp",              },
        { { L"tiff",    imgTypeTiff },  "image/tiff",               },
        { { L"avif",    imgTypeAvif },  "image/avif",               },
        { { L"apng",    imgTypeApng },  "image/apng",               }
    },
    CONTENT_TYPES_QUAINITY
};

typedef struct
{
    wchar_t path[MAX_PATH];
    wchar_t name[MAX_PATH];
    bool isBound;
} IconFileInfo;

typedef struct 
{
    wchar_t path[MAX_PATH];
    unsigned char *folderNameUtf8;
} ResponseFolderInfo;

typedef struct
{
    void *stream;
    bool (*write)(void *, const unsigned char *, size_t);
} StreamInfo;

typedef struct
{
    MemoryBuffer logBuffer;
    MemoryBuffer faviconBuffer;
    StreamInfo writeStream;     //сюда пишет curlCallback
    unsigned char *url;
    IconFileInfo *boundIcon;
    IniFileInfo *ini;
    CURL *easy;
    FILE *responseFile;
    wchar_t pageResponseFilePath[MAX_PATH];
    wchar_t faviconResponseFilePath[MAX_PATH];
    wchar_t fileName[MAX_PATH];
    wchar_t desktopUrlFilePath[MAX_PATH];
    bool downloadingPage;
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
unsigned char *WstringToUtf8(const wchar_t *wstr);      // возвращает либо указатель на готовую конвертированную строку, либо NULL
void CurlGetinfoFailMessage(StreamInfo *streamInfo, const unsigned char *type);
void LogWstring(const unsigned char *format, const wchar_t *wstr, const unsigned char *var);
unsigned char *GetFaviconUrl(const unsigned char *buffer, int size, const unsigned char *base_url);
bool DropDirectory(const wchar_t *directory);
int vMakeMessage(unsigned char **buffer, const unsigned char *format, va_list ap);
int MakeMessage(unsigned char **buffer, const unsigned char *format, ...);
bool ProcessResponseFolderInfo(ResponseFolderInfo *info, const wchar_t *fileName);
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
void Wrap_Free(void *arg);
void IconsProcessContainerDestructor(void *arg);
void Wrap_FClose(void *arg);
void Wrap_curl_global_cleanup(void *arg);
void ResponseFolderInfoDestructor(void *arg);
void Wrap_vips_shutdown(void *arg);
void Wrap_g_object_unref(void *arg);
void Wrap_DestroyGrowingList(void *arg);


CleanupStack cleanupStack = NULL;
FILE* logFile = NULL;
wchar_t appFolder[MAX_PATH];

int main(void)
{
    cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
    if (!cleanupStack)
    {
        fprintf(logFile, "[ERROR] malloc() failed: %s", strerror(errno));
        FatalError("Could not initialize cleanup stack");
        return STANDARD_ERROR;
    }

    if 
    (
        !GetCurrentDirectoryW(MAX_PATH, appFolder) ||
        PathCchRemoveFileSpec(appFolder, MAX_PATH) != S_OK
    )
    {
        FatalError("Could not get CWD or remove \\bin from path. GetCurrentDirectoryW() or PathCchRemoveFileSpec() failed");
        return STANDARD_ERROR;
    }

    wchar_t logPath[MAX_PATH];
    if (FAILED(StringCchPrintfW(logPath, MAX_PATH, L"%ls\\logs\\app.log", appFolder)))
    {
        FatalError("Could not build path to app.log file. StringCchPrintfW() failed");
        return STANDARD_ERROR;
    }
    if (!(logFile = _wfopen(logPath, L"wb")))
    {
        FatalError("Could not open .log file");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_FClose, logFile);      // (1 глобально)
    
    LogWstring("[DEBUG] Current app folder (CWD with no \\bin): \"%s\"\n", appFolder, "cwd");
    LogWstring("[DEBUG] app.log Path: \"%s\"\n", logPath, "logPath");

    ResponseFolderInfo pages, favicons;
    if (!ProcessResponseFolderInfo(&pages, FOLDER_PAGES_L)) return STANDARD_ERROR;          // (cleanup stack 2 глобально)
    if (!ProcessResponseFolderInfo(&favicons, FOLDER_FAVICONS_L)) return STANDARD_ERROR;    // (cleanup stack 3 глобально)

    WIN32_FIND_DATAW fileData;
    HANDLE searchingFilesHandle;
    wchar_t searchToken[MAX_PATH];
    GrowingList existingIcons;
    PushCleanupStack(cleanupStack, Wrap_DestroyGrowingList, &existingIcons);        //(4 глобально)

    if (InitGrowingList(&existingIcons, Wrap_Free))
    {
        if (SUCCEEDED(StringCchPrintfW(searchToken, MAX_PATH, L"%ls\\resources\\icons\\*.ico", appFolder)))
        {
            searchingFilesHandle = FindFirstFileW(searchToken, &fileData);
            if (searchingFilesHandle == INVALID_HANDLE_VALUE)
            {
                if (GetLastError() == ERROR_FILE_NOT_FOUND) fprintf(logFile, "[DEBUG] Папка с иконками пуста\n");
                else fprintf(logFile, "[ERROR] Не удалось выполнить поиск файлов с существующими иконками\n");
            }
            else
            {
                fprintf(logFile, "[DEBUG] цикл поиска существующих иконок ...\n");
                do
                {
                    LogWstring("\n[DEBUG] Найден файл: \"%s\"\n", fileData.cFileName, "fileData.cFileName");
                    IconFileInfo *ifi = malloc(sizeof(IconFileInfo));
                    if (!ifi)
                    {
                        fprintf(logFile, "[ERROR] Не удалось выделить память для объекта, представляющего этот файл\n");
                        continue;
                    }
                    ifi->isBound = false;
                    if
                    (
                        StringCchCopyW(ifi->name, MAX_PATH, fileData.cFileName) != S_OK ||
                        FAILED(StringCchPrintfW(ifi->path, MAX_PATH, L"%ls\\resources\\icons\\%ls", appFolder, fileData.cFileName))
                    )
                    {
                        fprintf(logFile, "[ERROR] Не удалось проинициализировать имя или путь к этому файлу\n");
                        free(ifi);
                        continue;
                    }
                    if (!PushGrowingList(&existingIcons, ifi))
                    {
                        fprintf(logFile, "[ERROR] Не удалось добавить в список объект, представляющий этот файл\n");
                        free(ifi);
                        continue;
                    }
                    fprintf(logFile, "[DEBUG] Файл успешно обработан\n");
                }
                while(FindNextFileW(searchingFilesHandle, &fileData));
                FindClose(searchingFilesHandle);
                fprintf(logFile, "\n[DEBUG] цикл поиска существующих иконок завершен\n");
            }
        }
        else fprintf(logFile, "[ERROR] Could not build searching .ico files token. StringCchPrintfW() failed");
    }
    else fprintf(logFile, "[ERROR] Не удалось инициализировать список объектов, представляющих файлы существующих иконок\n");

    wchar_t folderTempResources[MAX_PATH];
    if (SUCCEEDED(StringCchPrintfW(folderTempResources, MAX_PATH, L"%ls\\resources\\temp\\", appFolder)))
    {
        LogWstring("[DEBUG] Built path to \"resources\\temp\" directory: \"%s\"\n", folderTempResources, "folderTempResources");
        if (DropDirectory(folderTempResources)) fprintf(logFile, "[DEBUG] Directory cleaned up\n");
        else fprintf(logFile, "[ERROR] Some error while cleaning direcotry. Some files may remain\n");
    }
    else fprintf(logFile, "[ERROR] Could not build path to resources\\temp folder to clean it. Some files may remain\n");

    wchar_t *desktopPath = NULL;
    HRESULT desktopPathResult = SHGetKnownFolderPath // взять путь до определённой папки
    (
        &FOLDERID_Desktop, // рабочий стол
        0,
        NULL,
        &desktopPath
    );
    PushCleanupStack(cleanupStack, Wrap_CoTaskMemFree, &desktopPath);   //desktopPath освобождать даже в случае неудачи (5 глобально)
    if (FAILED(desktopPathResult)) 
    {
        FatalError("Could not find path to desktop. SHGetKnownFolderPath() failed");
        return STANDARD_ERROR;
    }
    LogWstring("[DEBUG] Desktop path: \"%s\"\n", desktopPath, "desktopPath");
    
    if (FAILED(StringCchPrintfW(searchToken, MAX_PATH, L"%ls\\*.url", desktopPath)))
    {
        FatalError("Could not convert desktop path to searching .url files token. StringCchCopyW() or StringCchCatW() failed");
        return STANDARD_ERROR;
    }
    
    searchingFilesHandle = FindFirstFileW(searchToken, &fileData);        // получить дескриптор поиска и получить первый файл
    if (searchingFilesHandle == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            fprintf(logFile, "\n[DEBUG] No shortcut (.url) files found on the desktop.\n");
            CompleteDeallocation(cleanupStack);
            return 0;
        }
        else
        {
            FatalError("Invalid files searching descriptor value. FindFirstFileW() failed");
            return STANDARD_ERROR;
        }
    }
    PushCleanupStack(cleanupStack, Wrap_FindClose, &searchingFilesHandle);  // (6 глобально)

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        FatalError("curl_global_init() failed");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_curl_global_cleanup, NULL);        // (7 глобально)
    IconsProcessContainer iconsProcessContainer;
    iconsProcessContainer.multi = curl_multi_init();
    if (!iconsProcessContainer.multi)
    {
        FatalError("curl_multi_init() failed");
        return STANDARD_ERROR;
    }
    if (!InitGrowingList(&iconsProcessContainer.iconProcessUnits, NULL))
    {
        FatalError("не удалось инициализировать растущий список для iconProcessContainer");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, IconsProcessContainerDestructor, &iconsProcessContainer);    // (8 глобально)
    
    size_t DesktopFilesProcessedCorrectly = 0;
    fprintf(logFile, "[DEBUG] Started searching and processing .url files in cycle ...\n");
    do
    {
        fprintf(logFile, "\n|==================================================================================|\n\n");
        LogWstring("[DEBUG] File name: \"%s\"\n", fileData.cFileName, "fileData.cFileName");
        IconProcessUnit *currentUnit = malloc(sizeof(IconProcessUnit));
        if (!currentUnit)
        {
            fprintf(logFile, "[ERROR] malloc() failed\n");
            continue;
        }
        *currentUnit = (IconProcessUnit){ .downloadingPage = true };
        if (!PushGrowingList(&iconsProcessContainer.iconProcessUnits, currentUnit))
        {
            fprintf(logFile, "[ERROR] не удалось пихнуть currentUnit в iconsProcessContainer.iconProcessUnits");
            continue;
        }

        if (FAILED(StringCchCopyW(currentUnit->fileName, MAX_PATH, fileData.cFileName)))
        {
            fprintf(logFile, "[ERROR] Не удалось скопирнуть имя обрабатываемого url в currentUnit->fileName\n");
            continue;
        }

        //получить абсолютный путь до ярлыка
        if (FAILED(StringCchPrintfW(currentUnit->desktopUrlFilePath, MAX_PATH, L"%ls\\%ls", desktopPath, fileData.cFileName)))
        {
            fprintf(logFile, "Could not build absolute path to one of .url file. StringCchPrintfW() failed");
            continue;
        }
        LogWstring("[DEBUG] absolute path: \"%s\"\n", currentUnit->desktopUrlFilePath, "currentUnit->desktopUrlFilePath");

        if
        (
            FAILED(StringCchPrintfW(currentUnit->pageResponseFilePath, MAX_PATH, L"%ls%ls", pages.path, fileData.cFileName)) ||
            PathCchRenameExtension(currentUnit->pageResponseFilePath, MAX_PATH, DEFAULT_EXTENTION_OF_FILES_PAGES_L) != S_OK
        )
        {
            fprintf(logFile, "[ERROR] Could not build absolute path to debug\\responce\\* file. StringCchPrintfW() or PathCchRenameExtension() failed\n");
            continue;
        }
        unsigned char *formatForLogWstring = NULL;
        MakeMessage(&formatForLogWstring, "[DEBUG] \"%s\" responce file path: \"%%s\"\n", pages.folderNameUtf8);
        if (formatForLogWstring)
        {
            LogWstring(formatForLogWstring, currentUnit->pageResponseFilePath, "currentUnit->pageResponseFilePath");
            free(formatForLogWstring);
        }
        else fprintf(logFile, "[ERROR] Successfuly build responce file path however could not build informative message for app.log. MakeMessage() failed\n");

        currentUnit->ini = InitIniInfo(currentUnit->desktopUrlFilePath);
        if (!currentUnit->ini)
        {
            fprintf(logFile, "[ERROR] не удалось прочитать ini-текст из файла на рабочем столе\n");
            continue;
        }
        currentUnit->url = GetIniUrl(currentUnit->ini);

        if (!currentUnit->url)
        {
            fprintf(logFile, "[ERROR] не удалось найти url в ini-тексте\n");
            continue;
        }
        fprintf(logFile, "[DEBUG] Got url: %s\n", currentUnit->url);

        if (!(currentUnit->responseFile = _wfopen(currentUnit->pageResponseFilePath, L"wb")))
        {
            fprintf(logFile, "[ERROR] Could not open debug file to process url. _wfopen() failed\n");
            continue;
        }
        currentUnit->writeStream = (StreamInfo){ currentUnit->responseFile, SIWrap_fwrite };
        
        if (!(currentUnit->easy = curl_easy_init()))
        {
            fprintf(logFile, "[ERROR] Could not initialize libcurl easy handle to transfer data by url. curl_easy_init() failed\n");
            continue;
        }

        curl_easy_setopt(currentUnit->easy, CURLOPT_URL, currentUnit->url);                 //url по которому обращаться
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEFUNCTION, WriteCallback);          //колбек когда приходят данные
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEDATA, &currentUnit->writeStream);  //параметр, с которым вызывается колбек
        curl_easy_setopt(currentUnit->easy, CURLOPT_PRIVATE, currentUnit);                  //ассоциация easy с IconProcessUnit
        curl_easy_setopt(currentUnit->easy, CURLOPT_TIMEOUT, 15L);                          //Запрос длиться не более 15 секунд
        curl_easy_setopt(currentUnit->easy, CURLOPT_FOLLOWLOCATION, 1L);                    //Редиректы
        curl_multi_add_handle(iconsProcessContainer.multi, currentUnit->easy);

        IconFileInfo *ifi = SearchGrowingList(&existingIcons, CompareIconFileInfoToIpuName, (void *)fileData.cFileName);
        if (ifi)
        {
            currentUnit->boundIcon = ifi;
            ifi->isBound = true;
            LogWstring("[DEBUG] Привязан .ico: \"%s\"\n", ifi->name, "ifi->name");
        }
        else fprintf(logFile, "[DEBUG] Соответсвующая иконка не найдена\n");
        
        ++DesktopFilesProcessedCorrectly;
        fprintf(logFile, "[DEBUG] Successfuly processed\n");
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));
    if (GetLastError() != ERROR_NO_MORE_FILES)
    {
        FatalError("Error of searching files. FindNextFileW() or FindFirstFileW() failed");
        return STANDARD_ERROR;
    }
    fprintf(logFile, "\n|==================================================================================|\n\n[DEBUG] End of cycle\n");

    if (VIPS_INIT(APP_NAME))    //инициализация libvips
    {
        FatalError("Could not initialize libvips. VIPS_INIT() failed");
        return -1;
    }
    PushCleanupStack(cleanupStack, Wrap_vips_shutdown, NULL);   // (9 глобально)

    fprintf(logFile, "[DEBUG] Started transfers cycle ...\n");
    int runningHandles; //склько запросов ещё НЕ завершились
    size_t pagesResponses = 0, successfulPagesResponses = 0, faviconResponses = 0, successfulFaviconResponses = 0;
    size_t replacedIcons = 0, deletedIcons = 0, newIcons = 0;
    do
    {
        if(curl_multi_perform(iconsProcessContainer.multi, &runningHandles) != CURLM_OK)
        {
            FatalError("curl_multi_perform() failed");
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
                if (ipu->downloadingPage)
                {
                    ++pagesResponses;
                    ipu->downloadingPage = false;
                    StreamInfo logBufferStream = { &ipu->logBuffer, SIWrap_WriteMemoryBuffer};
                    
                    PrintStream
                    (
                        &logBufferStream,
                        "\n|==================================================================================|\n\n[DEBUG] Page transfer finished\n[DEBUG] Exit code:               %s\n[DEBUG] Initial URL:             %s\n", 
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
                    PrintStream(&logBufferStream, "[DEBUG] last used effective URL: %s\n", lastUrl);
                    
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
                        PrintStream(&logBufferStream, "[ERROR] Response code is not 200\n");
                        ableParseHTML = false;
                    }
                    if (strncmp(contentType, TARGET_PAGE_RESPONSE_TYPE, strlen(TARGET_PAGE_RESPONSE_TYPE)))
                    {
                        PrintStream(&logBufferStream, "[ERROR] Response content type was not recognized as \"%s\"\n", TARGET_PAGE_RESPONSE_TYPE);
                        ableParseHTML = false;
                    }

                    if (ableParseHTML)
                    {
                        fclose(ipu->responseFile);
                        if (!(ipu->responseFile = _wfopen(ipu->pageResponseFilePath, L"rb")))
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                            PrintStream(&logFileStream, "[ERROR] Could not open responce file. _wfopen() failed");
                            continue;
                        }
                        WIN32_FILE_ATTRIBUTE_DATA responceFileData;
                        if (!GetFileAttributesExW(ipu->pageResponseFilePath, GetFileExInfoStandard, &responceFileData))
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                            PrintStream(&logFileStream, "[ERROR] Could not get size of responce file. GetFileAttributesExW() failed\n");
                            continue;
                        }
                        ULARGE_INTEGER responceFileSize = { .LowPart = responceFileData.nFileSizeLow, .HighPart = responceFileData.nFileSizeHigh };
                        if (responceFileSize.QuadPart > INT_MAX)
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                            PrintStream(&logFileStream, "[ERROR] Response size exceeds INT_MAX and can not be processed\n");
                            continue;
                        }
                        unsigned char *buffer = malloc(responceFileSize.QuadPart);
                        if (!buffer)
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                            PrintStream(&logFileStream, "[ERROR] malloc() failed\n");
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Wrap_Free, buffer);     //(9 глобально +1 временно ->10)

                        fread(buffer, 1, responceFileSize.QuadPart, ipu->responseFile);
                        unsigned char *faviconUrl = GetFaviconUrl(buffer, (int)responceFileSize.QuadPart, lastUrl);
                        if(!faviconUrl)
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                            PrintStream(&logFileStream, "[ERROR] Could not get favicon url. GetFaviconUrl() failed.\n");
                            SingleDeallocation(cleanupStack);   //(10->9)
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Wrap_Free, faviconUrl);     //(9 глобально +2 временно ->11)
                        PrintStream(&logBufferStream, "[DEBUG] Favicon url: %s\n", faviconUrl);
                        
                        curl_multi_remove_handle(iconsProcessContainer.multi, easy);
                        curl_easy_setopt(easy, CURLOPT_URL, faviconUrl); // перенастраиваем
                        curl_easy_setopt(easy, CURLOPT_TIMEOUT, 8L);
                        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
                        curl_multi_add_handle(iconsProcessContainer.multi, easy);

                        fclose(ipu->responseFile);
                        ipu->responseFile = NULL;
                        ipu->writeStream = (StreamInfo){ &ipu->faviconBuffer, SIWrap_WriteMemoryBuffer};

                        ++successfulPagesResponses;
                        PrintStream(&logBufferStream, "[DEBUG] Successfuly configured curl easy handle to download favicon\n");
                        PartialDeallocation(cleanupStack, 2);       //(11->9)
                    }
                    else FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                }
                else    //обрабатываем скаченный favicon
                {
                    ++faviconResponses;
                    FlushMemoryBufferToFile(&ipu->logBuffer, logFile);
                    fprintf(logFile, "\n        -------- Favicon response --------\n\n");

                    if (!WriteCurlResponseCode(easy, &logFileStream, &responseCode)) continue;
                    if (!WriteCurlContentType(easy, &logFileStream, &contentType)) continue;

                    ImageContentType *imageType;
                    if 
                    (
                        !(imageType = GetExtentionFromContentType(contentType)) ||
                        FAILED(StringCchPrintfW(ipu->faviconResponseFilePath, MAX_PATH, L"%ls%ls", favicons.path, ipu->fileName)) ||
                        PathCchRenameExtension(ipu->faviconResponseFilePath, MAX_PATH, imageType->extention) != S_OK
                    )
                    {
                        fprintf(logFile, "[ERROR] Could not build path to file for favicon response. The format may not be supported\n");
                        continue;
                    }
                    LogWstring("[DEBUG] Extention of file for favicon response: %s\n", imageType->extention, "imageType->extention");
                    LogWstring("[DEBUG] Built path of file for favicon response: \"%s\"\n", ipu->faviconResponseFilePath, "ipu->faviconResponseFilePath");

                    if (!(ipu->responseFile = _wfopen(ipu->faviconResponseFilePath, L"wb")))
                    {
                        fprintf(logFile, "[ERROR] Could not open file for favicon response\n");
                        continue;
                    }
                    FlushMemoryBufferToFile(&ipu->faviconBuffer, ipu->responseFile);

                    fprintf(logFile, "[DEBUG] Successfuly download favicon\n");
                    ++successfulFaviconResponses;

                    
                    //создать иконку для рабочего стола
                    fprintf(logFile, "\n        --------   Making icon    --------\n\n");

                    wchar_t *iconFilePathPtr;
                    if (ipu->boundIcon)
                    {
                        ++replacedIcons;
                        iconFilePathPtr = ipu->boundIcon->path;
                        fprintf(logFile, "[DEBUG] Уже существующая иконка будет заменена\n");
                    }
                    else
                    {
                        ++newIcons;
                        fprintf(logFile, "[DEBUG] Будет собранна новая иконка\n");
                        wchar_t newIconFilePath[MAX_PATH];
                        if
                        (
                            FAILED(StringCchPrintfW(newIconFilePath, MAX_PATH, L"%ls\\resources\\icons\\%ls", appFolder, ipu->fileName)) ||
                            PathCchRenameExtension(newIconFilePath, MAX_PATH, ICO_W) != S_OK
                        )
                        {
                            fprintf(logFile, "[ERROR] Could not build path to image file\n");
                            continue;
                        }
                        iconFilePathPtr = newIconFilePath;
                        LogWstring("[DEBUG] Built path for new icon: \"%s\"\n", newIconFilePath, "newIconFilePath");
                    }

                    FILE *iconFile = _wfopen(iconFilePathPtr, L"wb");
                    if (!iconFile)
                    {
                        fprintf(logFile, "[ERROR] Could not open icon file. _wfopen() failed\n");
                        continue;
                    }
                    PushCleanupStack(cleanupStack, Wrap_FClose, iconFile);  //(9 глобально +1 локально ->10))

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
                                    fprintf(logFile, "[ERROR] Не удалось загрузить svg-картинку. vips_svgload_buffer() failed\n");
                                    SingleDeallocation(cleanupStack);   //(10->9)
                                    continue;
                                }
                                int svgWidth = vips_image_get_width(tempSvgImage);
                                int svgHeight = vips_image_get_height(tempSvgImage);
                                Wrap_g_object_unref(tempSvgImage);
                                if (svgWidth != svgHeight)
                                {
                                    fprintf(logFile, "[ERROR] svg в не квадратном формате\n");
                                    SingleDeallocation(cleanupStack);   //(10->9)
                                    continue;
                                }
                                scale = 256.0 / svgWidth;
                                if (vips_svgload_buffer(ipu->faviconBuffer.content, ipu->faviconBuffer.length, &image, NULL))
                                {
                                    fprintf(logFile, "[ERROR] Не удалось загрузить svg-картинку. vips_svgload_buffer() failed\n");
                                    SingleDeallocation(cleanupStack);   //(10->9)
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
                                fprintf(logFile, "[ERROR] Could not load image file to build icon. vips_image_new_from_file() failed\n");
                                SingleDeallocation(cleanupStack);       //(10->9)
                                continue;
                            }
                            PushCleanupStack(cleanupStack, Wrap_g_object_unref, image); //(9 глобально +1 локально +1 временно ->11)
                            wchar_t pngOutputFilePath[MAX_PATH];
                            if 
                            (
                                FAILED(StringCchPrintfW(pngOutputFilePath, MAX_PATH, L"%ls\\resources\\temp\\%ls", appFolder, ipu->fileName)) ||
                                PathCchRenameExtension(pngOutputFilePath, MAX_PATH, PNG_W) != S_OK
                            )
                            {
                                fprintf(logFile, "[ERROR] Could not build path to image file\n");
                                PartialDeallocation(cleanupStack, 2);       //(11->9)
                                continue;
                            }
                            unsigned char *iconOutputFilePathUtf8 = WstringToUtf8(pngOutputFilePath);
                            if (!iconOutputFilePathUtf8)
                            {
                                fprintf(logFile, "[ERROR] Could not convert image path to UTF-8. WstringToUtf8() failed\n");
                                PartialDeallocation(cleanupStack, 2);       //(11->9)
                                continue;
                            }
                            PushCleanupStack(cleanupStack, Wrap_Free, iconOutputFilePathUtf8); //(9 глобально +1 локально +2 временно ->12)

                            if (vips_pngsave(image, iconOutputFilePathUtf8, NULL))
                            {
                                fprintf(logFile, "[ERROR] Could not save icon to .png file. vips_pngsave() failed\n");
                                PartialDeallocation(cleanupStack, 3);       //(12->9)
                                continue;
                            }

                            if (!(iconBuffer.buffer = malloc(sizeof(MemoryBuffer))))
                            {
                                fprintf(logFile, "[ERROR] не удалось выделить память для (imageBufferInfo *)(iconBuffer.buffer). malloc() failed");
                                PartialDeallocation(cleanupStack, 3);       //(12->9)
                                continue;
                            }
                            iconBuffer.isОriginal = true;
                            if (vips_pngsave_buffer(image, (void **)&iconBuffer.buffer->content, &iconBuffer.buffer->length, NULL) != 0)
                            {
                                fprintf(logFile, "[ERROR] Could not save icon to buffer. vips_pngsave_buffer() failed\n");
                                PartialDeallocation(cleanupStack, 3);       //(12->9)
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

                            fprintf(logFile, "[DEBUG] Successfuly saved temporary .png file: \"%s\"\n", iconOutputFilePathUtf8);
                            PartialDeallocation(cleanupStack, 2);       //(12->10) оставляем локальные ресурсы, если не было ошибки
                        }
                        else
                        {
                            iconBuffer = (imageBufferInfo){ &ipu->faviconBuffer, false };
                            if (iconBuffer.buffer->length < PNG_MIN_LENGTH)
                            {
                                fprintf(logFile, "[ERROR] Слишком маленький файл");
                                continue;
                            }
                            if (memcmp(iconBuffer.buffer->content, pngSignature, 8) != 0)
                            {
                                fprintf(logFile, "[ERROR] Сигнатура не совпала с png форматом");
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
                            fprintf(logFile, "[ERROR] слишком большой размер картинки. Файл не поддерживается\n");
                            SingleDeallocation(cleanupStack);       //10->9
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

                    fprintf(logFile, "[DEBUG] Иконка успешно сохранена\n");
                    
                    SingleDeallocation(cleanupStack);    // (10->9) сброс локальный ресурсов: файл с иконкой
                }
            }
        }
    }
    while (runningHandles);
    fprintf(logFile, "\n|==================================================================================|\n\n[DEBUG] transfers cycle finished\n");

    fprintf(logFile, "[DEBUG] Deleting redundant icons ...\n");
    bool deletedAnyFiles = false;
    for (size_t i = 0; i < existingIcons.length; ++i)
    {
        IconFileInfo *ifi = GetGrowingList(&existingIcons, i);
        if (!ifi->isBound)
        {
            if (!deletedAnyFiles) fprintf(logFile, "\n");
            deletedAnyFiles = true;
            if (DeleteFileW(ifi->path))
            {
                ++deletedIcons;
                LogWstring("[DEBUG] Deleted: \"%s\"\n", ifi->name, "ifi->path");
            } 
            else LogWstring("[ERROR] Could not delete file: \"%s\"\n", ifi->name, "ifi->path");
        }
    }
    if (deletedAnyFiles) fprintf(logFile, "\n");
    else fprintf(logFile, "[DEBUG] No such files\n");

    fprintf(logFile, "[INFO] Found files: %zd\n", iconsProcessContainer.iconProcessUnits.length);
    fprintf(logFile, "[INFO] Processed files correctly: %zd\n", DesktopFilesProcessedCorrectly);

    if (pages.folderNameUtf8) fprintf(logFile, "[INFO] \"%s\" responses: %zd\n[INFO] Successful \"%s\" responses: %zd\n", pages.folderNameUtf8, pagesResponses, pages.folderNameUtf8, successfulPagesResponses);
    else fprintf(logFile, "[ERROR] Unknown responces: %zd\n[ERROR]Successful unknown responses: %zd\n", pagesResponses, successfulPagesResponses);
    if (favicons.folderNameUtf8) fprintf(logFile, "[INFO] \"%s\" responses: %zd\n[INFO] Successful \"%s\" responses: %zd\n", favicons.folderNameUtf8, faviconResponses, favicons.folderNameUtf8, successfulFaviconResponses);
    else fprintf(logFile, "[ERROR] Unknown responces: %zd\n[ERROR] Successful unknown responces: %zd\n", faviconResponses, successfulFaviconResponses);

    fprintf(logFile, "[DEBUG] New icon files: %zu\n", newIcons);
    fprintf(logFile, "[DEBUG] Replaced icon files: %zu\n", replacedIcons);
    fprintf(logFile, "[DEBUG] Deleted icon files: %zu\n", deletedIcons);
    
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);   //для обновления ярлыков
    fflush(logFile);
    CompleteDeallocation(cleanupStack);
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

void LogWstring(const unsigned char *format, const wchar_t *wstr, const unsigned char *var)
{
    unsigned char* tempMessage_utf8 = WstringToUtf8(wstr);
    if (tempMessage_utf8)
    {
        fprintf(logFile, format, tempMessage_utf8);
        free(tempMessage_utf8);
    }
    else fprintf(logFile, "[ERROR] Could not convert (wchar_t *)%s to utf-8. WstringToUtf8() failed\n", var);
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

bool ProcessResponseFolderInfo(ResponseFolderInfo *info, const wchar_t *folderName)
{
    PushCleanupStack(cleanupStack, ResponseFolderInfoDestructor, info);
    info->folderNameUtf8 = WstringToUtf8(folderName);
    if (FAILED(StringCchPrintfW(info->path, MAX_PATH, L"%ls\\response\\%ls\\", appFolder, folderName)))
    {
        if (!info->folderNameUtf8) FatalError("Could not make utf8 string from wide-char to build error message. WstringToUtf8() failed");
        unsigned char *errorMessage = NULL;
        if (MakeMessage(&errorMessage, "Could not build \"%s\" path for responces", info->folderNameUtf8) >= 0)
        {
            FatalError(errorMessage);
            free(errorMessage);
        }
        else FatalError("Could not build error message. MakeMessage() failed");
        return false;
    }
    else
    {
        unsigned char *pathUtf8 = WstringToUtf8(info->path);
        if (pathUtf8)
        {
            if (info->folderNameUtf8) fprintf(logFile, "[DEBUG] Build path for responce files \"%s\": \"%s\"\n", info->folderNameUtf8, pathUtf8);
            else fprintf(logFile, "[DEBUG] Build path: \"%s\"\n", pathUtf8);
            free(pathUtf8);
        }
        else fprintf(logFile, "[ERROR] Successfuly build path, however could not convert path to utf8 for app.log. WstringToUtf8() failed");
        if (DropDirectory(info->path))
            fprintf(logFile, "[DEBUG] Directory cleaned up.\n");
        else fprintf(logFile, "[ERROR] Failed to clean up directory. Some files may remain\n");
    }
    return true;
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
    PrintStream(streamInfo, "[ERROR] Could not get info from curl easy handle. curl_easy_getinfo(%s) failed\n", type);
}

bool WriteCurlResponseCode(CURL *easy, StreamInfo *stream, long *responseCode)
{
    if (curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, responseCode) == CURLE_OK)
    {
        PrintStream(stream, "[DEBUG] Response code:           %ld\n", *responseCode);
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
            PrintStream(stream, "[DEBUG] Content type:            %s\n", *contentType);
            return true;
        }
        else PrintStream(stream, "[ERROR] The server did not send a valid Content-Type header or the protocol used does not support this\n");
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
    const wchar_t *icoNameExtentionPtr = PathFindExtensionW(ifi->name);
    size_t lenUrl = urlNameExtentionPtr - desiredName;
    size_t lenIco = icoNameExtentionPtr - ifi->name;
    if
    (
        lenUrl == lenIco &&
        wmemcmp(desiredName, ifi->name, lenUrl) == 0
    ) return true;
}

static void FatalError(const unsigned char *message)
{
    unsigned char nullMessage[] = "Could not build error message";
    if(logFile)
    {
        fprintf(logFile, "[FATAL ERROR] %s\n", message ? message : nullMessage);
        MessageBoxA(NULL, "The program terminated due to a fatal error. See the log file for details", NULL, MB_OK);
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
    CoTaskMemFree(*(wchar_t **)arg);
}

void Wrap_Free(void *arg)
{
    free(arg);
}

void IconsProcessContainerDestructor(void *arg)
{
    IconsProcessContainer *container = (IconsProcessContainer *)arg;
    for (size_t i; i < container->iconProcessUnits.length; ++i)
    {
        IconProcessUnit *unit = GetGrowingList(&container->iconProcessUnits, i);
        if (unit->ini) DestroyIniFileInfo(unit->ini);
        free(unit->url);
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

void ResponseFolderInfoDestructor(void *arg)
{
    free((*(ResponseFolderInfo *)arg).folderNameUtf8);
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