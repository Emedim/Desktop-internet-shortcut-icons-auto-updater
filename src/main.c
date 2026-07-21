#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath();
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW(); StringCchCopyW(); StringCchPrintfW();
#include <pathcch.h>    //PathCchRemoveFileSpec

#include "curl/curl.h"

#include "libxml/HTMLparser.h"
#include "libxml/xpath.h"
#include "libxml/uri.h"

#include "cleanup interface.h"
#include "small parser.h"
#define STANDARD_ERROR (-1)
#define MAX_CURRENT_SYSTEM_RESOURCES (9)
#define INICIAL_BUFFER_LENGTH (12)
#define BUFFER_ADDITION (8)


#define FOLDER_PAGES_L L"pages"
#define FOLDER_FAVICONS_L L"favicons"
#define DEFAULT_EXTENTION_OF_FILES_PAGES_L L"html"


typedef struct
{
    char *faviconContentType;
    wchar_t *imageContentType;
} ContentTypesMatchingUnit;

#define CONTENT_TYPES_QUAINITY 14
typedef struct
{
    ContentTypesMatchingUnit array[CONTENT_TYPES_QUAINITY];
    size_t quantity;
} ContentTypesMatchingContainer;

const ContentTypesMatchingContainer contentTypes = 
{
    {
        {
            "image/x-icon",
            L"ico"
        },
        {
            "image/vnd.microsoft.icon",
            L"ico"
        },
        {
            "image/ico",
            L"ico"
        },
        {
            "image/icon",
            L"ico"
        },
        {
            "image/png",
            L"png"
        },
        {
            "image/svg+xml",
            L"svg"
        },
        {
            "image/gif",
            L"gif"
        },
        {
            "image/jpeg",
            L"jpeg"
        },
        {
            "image/webp",
            L"webp"
        },
        {
            "image/bmp",
            L"bmp"
        },
        {
            "image/x-bmp",
            L"bmp"
        },
        {
            "image/tiff",
            L"tiff"
        },
        {
            "image/avif",
            L"avif"
        },
        {
            "image/apng",
            L"apng"
        }
    },
    CONTENT_TYPES_QUAINITY
};


typedef struct 
{
    wchar_t path[MAX_PATH];
    char *folderNameUtf8;
} ResponseFolderInfo;


typedef struct 
{
    char *content;
    size_t length;
} MemoryBuffer;

typedef struct
{
    void *stream;
    bool (*write)(void *, const char *, size_t);
} StreamInfo;

typedef struct
{
    MemoryBuffer logBuffer;
    MemoryBuffer faviconBuffer;
    StreamInfo writeStream;     //сюда пишет curlCallback
    char *url;
    CURL *easy;
    FILE *responseFile;
    wchar_t pageResponseFilePath[MAX_PATH];
    wchar_t faviconResponseFilePath[MAX_PATH];
    wchar_t fileName[MAX_PATH];
    bool downloadingPage;
} IconProcessUnit;

typedef struct
{
    IconProcessUnit **array;
    CURLM *multi;
    size_t occupiedUnits;
    size_t capacity;
} IconsProcessContainer;


size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
static void FatalError(const char *message);
char *WstringToUtf8(const wchar_t *wstr);      // возвращает либо указатель на готовую конвертированную строку, либо NULL
void CurlGetinfoFailMessage(StreamInfo *streamInfo, const char *type);
void LogWstring(const char *format, const wchar_t *wstr, const char *var);
char *GetFaviconUrl(const char *buffer, int size, const char *base_url);
bool DropDirectory(const wchar_t *directory);
int vMakeMessage(char **buffer, const char *format, va_list ap);
int MakeMessage(char **buffer, const char *format, ...);
bool ProcessResponseFolderInfo(ResponseFolderInfo *info, const wchar_t *fileName);
int PrintStream(StreamInfo *info, const char *format, ...);
bool WriteCurlResponseCode(CURL *easy, StreamInfo *stream, long *responseCode);
bool WriteCurlContentType(CURL *easy, StreamInfo *stream, char **contentType);
bool WriteStream(StreamInfo *info, const char *data, size_t dataLength);
bool SIWrap_fwrite(void *streamInfo, const char *data, size_t dataLength);
bool WriteMemoryBuffer(MemoryBuffer *memoryBuffer, const char *data, size_t dataLength);
bool SIWrap_WriteMemoryBuffer(void *memoryBuffer, const char *data, size_t dataLength);
void FlushMemoryBufferToFile(MemoryBuffer *buffer, FILE *file);
wchar_t *GetExtentionFromContentType(const char *contentType);

void Wrap_FindClose(const void *arg);
void Wrap_CoTaskMemFree(const void *arg);
void Wrap_Free(const void *arg);
void IconsProcessContainerDestructor(const void *arg);
void Wrap_FClose(const void *arg);
void Wrap_curl_global_cleanup(const void *arg);
void ResponseFolderInfoDestructor(const void *arg);


CleanupStack cleanupStack = NULL;
FILE* log = NULL;
wchar_t appFolder[MAX_PATH];

int main(void)
{
    cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
    if (cleanupStack == NULL)
    {
        FatalError("Initialization error. InitCleanupStack() failed");
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
    if (!(log = _wfopen(logPath, L"wb")))
    {
        FatalError("Could not open .log file");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_FClose, &log);      // (1 глобально)
    
    LogWstring("[DEBUG] Current app folder (CWD with no \\bin): \"%s\"\n", appFolder, "cwd");
    LogWstring("[DEBUG] app.log Path: \"%s\"\n", logPath, "logPath");

    ResponseFolderInfo pages, favicons;
    if (!ProcessResponseFolderInfo(&pages, FOLDER_PAGES_L)) return STANDARD_ERROR;          // (cleanup stack 2 глобально)
    if (!ProcessResponseFolderInfo(&favicons, FOLDER_FAVICONS_L)) return STANDARD_ERROR;    // (cleanup stack 3 глобально)

    wchar_t *desktopPath = NULL;
    HRESULT desktopPathResult = SHGetKnownFolderPath // взять путь до определённой папки
    (
        &FOLDERID_Desktop, // рабочий стол
        0,
        NULL,
        &desktopPath
    );
    PushCleanupStack(cleanupStack, Wrap_CoTaskMemFree, &desktopPath);   //desktopPath освобождать даже в случае неудачи (4 глобально)
    if (FAILED(desktopPathResult)) 
    {
        FatalError("Could not find path to desktop. SHGetKnownFolderPath() failed");
        return STANDARD_ERROR;
    }
    LogWstring("[DEBUG] Desktop path: \"%s\"\n", desktopPath, "desktopPath");
    
    wchar_t searchPath[MAX_PATH];   //Получение токена поиска для FindFirstFileW() из рабочего стола
    if
    (
        StringCchCopyW(searchPath, MAX_PATH, desktopPath) != S_OK ||
        StringCchCatW(searchPath, MAX_PATH, L"\\*.url") != S_OK
    )
    {
        FatalError("Could not convert desktop path to searching .url files token. StringCchCopyW() or StringCchCatW() failed");
        return STANDARD_ERROR;
    }

    WIN32_FIND_DATAW fileData;                                                  // информация о файле
    HANDLE searchingFilesHandle = FindFirstFileW(searchPath, &fileData);        // получить дескриптор поиска и получить первый файл
    if (searchingFilesHandle == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            fprintf(log, "\n[DEBUG] No shortcut (.url) files found on the desktop.\n");
            CompleteDeallocation(cleanupStack);
            return 0;
        }
        else
        {
            FatalError("Invalid files searching descriptor value. FindFirstFileW() failed");
            return STANDARD_ERROR;
        }
    }
    PushCleanupStack(cleanupStack, Wrap_FindClose, &searchingFilesHandle);  // (5 глобально)

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        FatalError("curl_global_init() failed");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Wrap_curl_global_cleanup, NULL);        // (6 глобально)
    IconsProcessContainer iconsProcessContainer = {NULL, curl_multi_init(), 0, INICIAL_BUFFER_LENGTH};
    if (!iconsProcessContainer.multi)
    {
        FatalError("curl_multi_init() failed");
        return STANDARD_ERROR;
    }
    iconsProcessContainer.array = malloc(iconsProcessContainer.capacity * sizeof(IconProcessUnit *));
    if (!iconsProcessContainer.array)
    {
        FatalError("malloc() failed. Var: iconsProcessContainer.array");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, IconsProcessContainerDestructor, &iconsProcessContainer);    // (7 глобально)

    size_t DesktopFilesProcessedCorrectly = 0;
    fprintf(log, "[DEBUG] Started searching and processing .url files in cycle ...\n");
    do
    {
        fprintf(log, "\n|==================================================================================|\n\n");
        LogWstring("[DEBUG] File name: \"%s\"\n", fileData.cFileName, "fileData.cFileName");
        //создать буффер для хранения информации о ярлыках
        if (iconsProcessContainer.occupiedUnits >= iconsProcessContainer.capacity)
        {
            iconsProcessContainer.capacity += BUFFER_ADDITION;
            IconProcessUnit **temp = realloc(iconsProcessContainer.array, iconsProcessContainer.capacity * sizeof(IconProcessUnit *)); //выделили новый массив, количество элементов: старое количество + немного ещё
            if (!temp)  // temp == NULL
            {
                FatalError("realloc() failed");
                return STANDARD_ERROR;
            }
            iconsProcessContainer.array = temp;
        }
        IconProcessUnit *currentUnit = iconsProcessContainer.array[iconsProcessContainer.occupiedUnits++] = malloc(sizeof(IconProcessUnit));
        if (!currentUnit)
        {
            FatalError("malloc() failed. Var: currentUnit");
            return STANDARD_ERROR;
        }
        *currentUnit = (IconProcessUnit){ .downloadingPage = true };

        StringCchCopyW(currentUnit->fileName, MAX_PATH, fileData.cFileName);

        //получить абсолютный путь до ярлыка
        wchar_t CurrentDesktopFileAbsolutePath[MAX_PATH];
        if (FAILED(StringCchPrintfW(CurrentDesktopFileAbsolutePath, MAX_PATH, L"%ls\\%ls", desktopPath, fileData.cFileName)))
        {
            FatalError("Could not build absolute path to one of .url file. StringCchPrintfW() failed");
            return STANDARD_ERROR;
        }
        LogWstring("[DEBUG] absolute path: \"%s\"\n", CurrentDesktopFileAbsolutePath, "CurrentDesktopFileAbsolutePath");

        if
        (
            FAILED(StringCchPrintfW(currentUnit->pageResponseFilePath, MAX_PATH, L"%ls%ls", pages.path, fileData.cFileName)) ||
            PathCchRenameExtension(currentUnit->pageResponseFilePath, MAX_PATH, DEFAULT_EXTENTION_OF_FILES_PAGES_L) != S_OK
        )
        {
            fprintf(log, "[ERROR] Could not build absolute path to debug\\responce\\* file. StringCchPrintfW() or PathCchRenameExtension() failed\n");
            continue;
        }
        char *formatForLogWstring = NULL;
        MakeMessage(&formatForLogWstring, "[DEBUG] \"%s\" responce file path: \"%%s\"\n", pages.folderNameUtf8);
        if (formatForLogWstring)
        {
            LogWstring(formatForLogWstring, currentUnit->pageResponseFilePath, "currentUnit->pageResponseFilePath");
            free(formatForLogWstring);
        }
        else fprintf(log, "[ERROR] Successfuly build responce file path however could not build informative message for app.log. MakeMessage() failed\n");

        //скопировать ini–текст из ярлыков
        LARGE_INTEGER fileSize = (LARGE_INTEGER){ .LowPart = fileData.nFileSizeLow, .HighPart = fileData.nFileSizeHigh };
        char *content = malloc(fileSize.QuadPart);  //создать буффер для копирования
        if(!content)
        {
            fprintf(log, "[ERROR] Could not allocate memory for output buffer. malloc() failed\n");
            continue;
        }
        PushCleanupStack(cleanupStack, Wrap_Free, &content);        // (7 глобально +1 временно ->8)
        FILE *currentDesktopFile = _wfopen(CurrentDesktopFileAbsolutePath, L"rb");  //файл откуда копировать
        if (!currentDesktopFile)
        {
            fprintf(log, "[ERROR] Could not open .url file for reading. _wfopen() failed.\n");
            SingleDeallocation(cleanupStack);   //(8->7)
            continue;
        }
        PushCleanupStack(cleanupStack, Wrap_FClose, &currentDesktopFile);   // (7 глобально +2 временно ->9)
        fread(content, 1, fileSize.QuadPart, currentDesktopFile);

        currentUnit->url = ParseIniText(content, fileSize.QuadPart);   
        if (!currentUnit->url)
        {
            fprintf(log, "[ERROR] Could parse .url content. ParceFileText() failed\n");
            PartialDeallocation(cleanupStack, 2);   //(9->7)
            continue;
        }
        fprintf(log, "[DEBUG] Got url: %s\n", currentUnit->url);

        if (!(currentUnit->responseFile = _wfopen(currentUnit->pageResponseFilePath, L"wb")))
        {
            fprintf(log, "[ERROR] Could not open debug file to process url. _wfopen() failed\n");
            PartialDeallocation(cleanupStack, 2);   //(9->7)
            continue;
        }
        currentUnit->writeStream = (StreamInfo){ currentUnit->responseFile, SIWrap_fwrite };
        
        if (!(currentUnit->easy = curl_easy_init()))
        {
            fprintf(log, "[ERROR] Could not initialize libcurl easy handle to transfer data by url. curl_easy_init() failed\n");
            PartialDeallocation(cleanupStack, 2);   //(9->7)
            continue;
        }

        curl_easy_setopt(currentUnit->easy, CURLOPT_URL, currentUnit->url);                 //url по которому обращаться
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEFUNCTION, WriteCallback);          //колбек когда приходят данные
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEDATA, &currentUnit->writeStream);  //параметр, с которым вызывается колбек
        curl_easy_setopt(currentUnit->easy, CURLOPT_PRIVATE, currentUnit);                  //ассоциация easy с IconProcessUnit
        curl_easy_setopt(currentUnit->easy, CURLOPT_TIMEOUT, 15L);                          //Запрос длиться не более 15 секунд
        curl_easy_setopt(currentUnit->easy, CURLOPT_FOLLOWLOCATION, 1L);                    //Редиректы
        curl_multi_add_handle(iconsProcessContainer.multi, currentUnit->easy);
        
        ++DesktopFilesProcessedCorrectly;
        PartialDeallocation(cleanupStack, 2);   //(9->7)
        fprintf(log, "[DEBUG] Successfuly processed\n");
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));
    if (GetLastError() != ERROR_NO_MORE_FILES)
    {
        FatalError("Error of searching files. FindNextFileW() or FindFirstFileW() failed");
        return STANDARD_ERROR;
    }
    fprintf(log, "\n|==================================================================================|\n\n[DEBUG] End of cycle\n");
    fprintf(log, "[DEBUG] Started transfers cycle ...\n");

    int runningHandles; //склько запросов ещё НЕ завершились
    size_t pagesResponses = 0, successfulPagesResponses = 0, faviconResponses = 0, successfulFaviconResponses = 0;
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
                StreamInfo logFileStream = { log, SIWrap_fwrite };
                CURL *easy = curlMsg->easy_handle;  //завершенный easy
                IconProcessUnit *ipu = NULL;
                if (curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ipu) != CURLE_OK)
                {
                    CurlGetinfoFailMessage(&logFileStream, "CURLINFO_PRIVATE");
                    continue;
                }

                long responseCode;
                char *contentType = NULL;
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
                    char *lastUrl = NULL;
                    if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &lastUrl) != CURLE_OK)
                    {
                        FlushMemoryBufferToFile(&ipu->logBuffer, log);
                        CurlGetinfoFailMessage(&logFileStream, "CURLINFO_EFFECTIVE_URL");
                        continue;
                    }
                    PrintStream(&logBufferStream, "[DEBUG] last used effective URL: %s\n", lastUrl);
                    
                    if (!WriteCurlResponseCode(easy, &logBufferStream, &responseCode))
                    {
                        FlushMemoryBufferToFile(&ipu->logBuffer, log);
                        continue;
                    } 
                    if (!WriteCurlContentType(easy, &logBufferStream, &contentType))
                    {                        
                        FlushMemoryBufferToFile(&ipu->logBuffer, log);
                        continue;
                    }
                    
                    bool ableParseHTML = true;
                    if (responseCode != 200)
                    {
                        PrintStream(&logBufferStream, "[ERROR] Response code is not 200\n");
                        ableParseHTML = false;
                    }
                    char *targetType = "text/html";
                    if (strncmp(contentType, targetType, strlen(targetType)))
                    {
                        PrintStream(&logBufferStream, "[ERROR] Response content type was not recognized as \"%s\"\n", targetType);
                        ableParseHTML = false;
                    }

                    if (ableParseHTML)
                    {
                        fclose(ipu->responseFile);
                        if (!(ipu->responseFile = _wfopen(ipu->pageResponseFilePath, L"rb")))
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, log);
                            PrintStream(&logFileStream, "[ERROR] Could not open responce file. _wfopen() failed");
                            continue;
                        }
                        WIN32_FILE_ATTRIBUTE_DATA responceFileData;
                        if (!GetFileAttributesExW(ipu->pageResponseFilePath, GetFileExInfoStandard, &responceFileData))
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, log);
                            PrintStream(&logFileStream, "[ERROR] Could not get size of responce file. GetFileAttributesExW() failed\n");
                            continue;
                        }
                        ULARGE_INTEGER responceFileSize = { .LowPart = responceFileData.nFileSizeLow, .HighPart = responceFileData.nFileSizeHigh };
                        if (responceFileSize.QuadPart > INT_MAX)
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, log);
                            PrintStream(&logFileStream, "[ERROR] Response size exceeds INT_MAX and can not be processed\n");
                            continue;
                        }
                        char *buffer = malloc(responceFileSize.QuadPart);
                        if (!buffer)
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, log);
                            PrintStream(&logFileStream, "[ERROR] malloc() failed\n");
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Wrap_Free, &buffer);     //(7 глобально +1 временно ->8)

                        fread(buffer, 1, responceFileSize.QuadPart, ipu->responseFile);
                        char *faviconUrl = GetFaviconUrl(buffer, (int)responceFileSize.QuadPart, lastUrl);
                        if(!faviconUrl)
                        {
                            FlushMemoryBufferToFile(&ipu->logBuffer, log);
                            PrintStream(&logFileStream, "[ERROR] Could not get favicon url. GetFaviconUrl() failed.\n");
                            SingleDeallocation(cleanupStack);   //(8->7)
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Wrap_Free, &faviconUrl);     //(7 глобально +2 временно ->9)
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
                        PartialDeallocation(cleanupStack, 2);       //(9->7)
                    }
                    else FlushMemoryBufferToFile(&ipu->logBuffer, log);
                }
                else
                {
                    ++faviconResponses;
                    FlushMemoryBufferToFile(&ipu->logBuffer, log);
                    fprintf(log, "\n        -------- transfering favicon --------\n\n");

                    if (!WriteCurlResponseCode(easy, &logFileStream, &responseCode)) continue;
                    if (!WriteCurlContentType(easy, &logFileStream, &contentType)) continue;

                    wchar_t *extention;
                    if 
                    (
                        !(extention = GetExtentionFromContentType(contentType)) ||
                        FAILED(StringCchPrintfW(ipu->faviconResponseFilePath, MAX_PATH, L"%ls%ls", favicons.path, ipu->fileName)) ||
                        PathCchRenameExtension(ipu->faviconResponseFilePath, MAX_PATH, extention) != S_OK
                    )
                    {
                        fprintf(log, "[ERROR] Could not build path to file for favicon response\n");
                        continue;
                    }
                    LogWstring("[DEBUG] Extention of file for favicon response: %s\n", extention, "extention");
                    LogWstring("[DEBUG] Built path of file for favicon response: %s\n", ipu->faviconResponseFilePath, "ipu->faviconResponseFilePath");

                    if (!(ipu->responseFile = _wfopen(ipu->faviconResponseFilePath, L"wb")))
                    {
                        fprintf(log, "[ERROR] Could not open file for favicon response\n");
                        continue;
                    }
                    FlushMemoryBufferToFile(&ipu->faviconBuffer, ipu->responseFile);

                    ++successfulFaviconResponses;
                }
            }
        }
    }
    while (runningHandles);
    fprintf(log, "\n|==================================================================================|\n\n[DEBUG] transfers cycle finished\n");
    fprintf(log, "[INFO] Found files: %zd\n", iconsProcessContainer.occupiedUnits);
    fprintf(log, "[INFO] Processed files correctly: %zd\n", DesktopFilesProcessedCorrectly);


    if (pages.folderNameUtf8) fprintf(log, "[INFO] \"%s\" responses: %zd\n[INFO] Successful \"%s\" responses: %zd\n", pages.folderNameUtf8, pagesResponses, pages.folderNameUtf8, successfulPagesResponses);
    else fprintf(log, "[ERROR] Unknown responces: %zd\n[ERROR]Successful unknown responses: %zd\n", pagesResponses, successfulPagesResponses);
    if (favicons.folderNameUtf8) fprintf(log, "[INFO] \"%s\" responses: %zd\n[INFO] Successful \"%s\" responses: %zd\n", favicons.folderNameUtf8, faviconResponses, favicons.folderNameUtf8, successfulFaviconResponses);
    else fprintf(log, "[ERROR] Unknown responces: %zd\n[ERROR] Successful unknown responces: %zd\n", faviconResponses, successfulFaviconResponses);
    
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);   //для обновления ярлыков
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

void LogWstring(const char *format, const wchar_t *wstr, const char *var)
{
    char* tempMessage_utf8 = WstringToUtf8(wstr);
    if (tempMessage_utf8)
    {
        fprintf(log, format, tempMessage_utf8);
        free(tempMessage_utf8);
    }
    else fprintf(log, "[ERROR] Could not convert (wchar_t *)%s to utf-8. WstringToUtf8() failed\n", var);
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

int vMakeMessage(char **buffer, const char *format, va_list ap)
{
    if (format == NULL || buffer == NULL) return -1;
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

int MakeMessage(char **buffer, const char *format, ...)
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
    if (FAILED(StringCchPrintfW(info->path, MAX_PATH, L"%ls\\responce\\%ls\\", appFolder, folderName)))
    {
        if (!info->folderNameUtf8) FatalError("Could not make utf8 string from wide-char to build error message. WstringToUtf8() failed");
        char *errorMessage = NULL;
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
        char *pathUtf8 = WstringToUtf8(info->path);
        if (pathUtf8)
        {
            if (info->folderNameUtf8) fprintf(log, "[DEBUG] Build path for responce files \"%s\": \"%s\"\n", info->folderNameUtf8, pathUtf8);
            else fprintf(log, "[DEBUG] Build path: \"%s\"\n", pathUtf8);
            free(pathUtf8);
        }
        else fprintf(log, "[ERROR] Successfuly build path, however could not convert path to utf8 for app.log. WstringToUtf8() failed");
        if (DropDirectory(info->path))
            fprintf(log, "[DEBUG] Directory cleaned up.\n");
        else fprintf(log, "[ERROR] Failed to clean up directory. Some files may remain\n");
    }
    return true;
}


int PrintStream(StreamInfo *info, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char *data;
    int dataLength = vMakeMessage(&data, format, ap);
    va_end(ap);
    if (dataLength < 0) return -1;
    bool res = WriteStream(info, data, dataLength);
    free(data);
    return res ? dataLength : -1;
}

void CurlGetinfoFailMessage(StreamInfo *streamInfo, const char *type) 
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

bool WriteCurlContentType(CURL *easy, StreamInfo *stream, char **contentType)
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

bool WriteStream(StreamInfo *info, const char *data, size_t dataLength)
{
    return info->write(info->stream, data, dataLength);
}

bool SIWrap_fwrite(void *file, const char *data, size_t dataLength)
{
    fwrite(data, 1, dataLength, (FILE *)file);
    return true;
}

bool WriteMemoryBuffer(MemoryBuffer *memoryBuffer, const char *data, size_t dataLength)
{
    char *temp = realloc(memoryBuffer->content, memoryBuffer->length + dataLength);
    if (!temp) return false;
    memoryBuffer->content = temp;
    memcpy(memoryBuffer->content + memoryBuffer->length, data, dataLength);
    memoryBuffer->length += dataLength;
    return true;
}

bool SIWrap_WriteMemoryBuffer(void *memoryBuffer, const char *data, size_t dataLength)
{
    return WriteMemoryBuffer((MemoryBuffer *)memoryBuffer, data, dataLength);
}

void FlushMemoryBufferToFile(MemoryBuffer *buffer, FILE *file)
{
    fwrite(buffer->content, 1, buffer->length, file);
}

wchar_t *GetExtentionFromContentType(const char *contentType)
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
        )  return contentTypes.array[i].imageContentType;
    }
    return NULL;
}

static void FatalError(const char *message)
{
    char nullMessage[] = "Could not build error message";
    if(log)
    {
        fprintf(log, "[FATAL ERROR] %s\n", message ? message : nullMessage);
        MessageBoxA(NULL, "The program terminated due to a fatal error. See the log file for details.", NULL, MB_OK);
    } 
    else MessageBoxA(NULL, message ? message : nullMessage, NULL, MB_OK);
    if (cleanupStack) CompleteDeallocation(cleanupStack);
}


void Wrap_FindClose(const void *arg)
{
    FindClose(*(HANDLE *)arg);
}

void Wrap_CoTaskMemFree(const void *arg)
{
    CoTaskMemFree(*(wchar_t **)arg);
}

void Wrap_Free(const void *arg)
{
    free(*(void **)arg);
}

void IconsProcessContainerDestructor(const void *arg)
{
    IconsProcessContainer *container = (IconsProcessContainer *)arg;
    while (container->occupiedUnits > 0)
    {
        IconProcessUnit *unit = container->array[--container->occupiedUnits];
        free(unit->url);
        free(unit->logBuffer.content);
        free(unit->faviconBuffer.content);
        if (unit->responseFile) fclose(unit->responseFile);
        if(unit->easy)
        {
            curl_multi_remove_handle(container->multi, unit->easy);
            curl_easy_cleanup(unit->easy);
        }
        free(unit);
    }
    curl_multi_cleanup(container->multi);
    free(container->array);
}

void Wrap_FClose(const void *arg)
{
    fclose(*(FILE **)arg);
}

void Wrap_curl_global_cleanup(const void *arg)
{
    curl_global_cleanup();
}

void ResponseFolderInfoDestructor(const void *arg)
{
    free((*(ResponseFolderInfo *)arg).folderNameUtf8);
}



char *GetFaviconUrl(const char *buffer, int size, const char *base_url)
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

    char *finalUrl = NULL;
    if (href)
    {
        // если ссылка абсолютная
        if (strstr((char *)href, "http://") || strstr((char *)href, "https://"))
            finalUrl = strdup((char *)href);
        else
        {
            // относительная -> делаем абсолютную
            xmlChar *abs = xmlBuildURI(href, (xmlChar *)base_url);
            if (abs)
            {
                finalUrl = strdup((char *)abs);
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