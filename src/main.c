#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath();
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW(); StringCchCopyW();
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
#define EXTENTION_OF_FILES_PAGES_L L"html"
#define EXTENTION_OF_FILES_FAVICONS_L L"html"

typedef struct 
{
    wchar_t path[MAX_PATH];
    const wchar_t *folderName;
    const wchar_t *extentionOfFiles;
    char *folderNameUtf8;
} ResponceFolderInfo;


typedef struct 
{
    char *content;
    size_t length;
} LogBuffer;


typedef struct
{
    byte *url;
    CURL *easy;
    FILE *responseFile;
    wchar_t pageResponseFilePath[MAX_PATH];
    wchar_t faviconResponseFilePath[MAX_PATH];
    LogBuffer logBuffer;
    bool downloadingPage;
} IconProcessUnit;

typedef struct
{
    IconProcessUnit **array;
    CURLM *multi;
    size_t occupedUnits;
    size_t capacity;
} IconsProcessContainer;


size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
static void FatalError(const byte *message);
char *WstringToUtf8(const wchar_t *wstr);      // возвращает либо указатель на готовую конвертированную строку, либо NULL
void CurlGetinfoFailMessage(char *type, LogBuffer *logBuffer) ;
void LogWstring(const char *format, const wchar_t *wstr, const char *var);
char *GetFaviconUrl(const char *buffer, size_t size, const char *base_url);
bool DropDirectory(const wchar_t *directory, const wchar_t *extention);
int MakeMessage(char **buffer, const char *format, const char *var);
bool ProcessResponseFolderInfo(ResponceFolderInfo *info);
bool ProcessResponseFilePath(wchar_t *value, ResponceFolderInfo *pathInfo, const wchar_t *fileName, const char *variableName);
bool AddLogBuffer(LogBuffer *logBuffer, const char *dataFormat, const char *value);
int MakeMessageLong(char **buffer, const char *format, const long value);

void Warp_FindClose(const void *arg);
void Warp_CoTaskMemFree(const void *arg);
void Warp_Free(const void *arg);
void IconsProcessContainerDestructor(const void *arg);
void Warp_FClose(const void *arg);
void Warp_curl_global_cleanup(const void *arg);
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
        FatalError("StringCchPrintfW() failed");
        return STANDARD_ERROR;
    }
    if (!(log = _wfopen(logPath, L"wb")))
    {
        FatalError("Could not open .log file");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_FClose, &log);      // (1 глобально)

    LogWstring("[DEBUG] Current app folder (CWD with no \\bin): \"%s\"\n", appFolder, "cwd");
    LogWstring("[DEBUG] app.log Path: \"%s\"\n", logPath, "logPath");

    ResponceFolderInfo pages, favicons;
    pages.folderName = FOLDER_PAGES_L;
    pages.extentionOfFiles = EXTENTION_OF_FILES_PAGES_L;
    if (!ProcessResponseFolderInfo(&pages)) return STANDARD_ERROR;   // (cleanup stack 2 глобально)

    favicons.folderName = FOLDER_FAVICONS_L;
    favicons.extentionOfFiles = EXTENTION_OF_FILES_FAVICONS_L;
    if (!ProcessResponseFolderInfo(&favicons)) return STANDARD_ERROR;      // (cleanup stack 3 глобально)

    wchar_t *desktopPath = NULL;
    HRESULT desktopPathResult = SHGetKnownFolderPath // взять путь до определённой папки
    (
        &FOLDERID_Desktop, // рабочий стол
        0,
        NULL,
        &desktopPath
    );
    PushCleanupStack(cleanupStack, Warp_CoTaskMemFree, &desktopPath);   //desktopPath освобождать даже в случае неудачи (4 глобально)
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
    PushCleanupStack(cleanupStack, Warp_FindClose, &searchingFilesHandle);  // (5 глобально)

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        FatalError("curl_global_init() failed");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_curl_global_cleanup, NULL);        // (6 глобально)
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
        if (iconsProcessContainer.occupedUnits >= iconsProcessContainer.capacity)
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
        IconProcessUnit *currentUnit = iconsProcessContainer.array[iconsProcessContainer.occupedUnits++] = malloc(sizeof(IconProcessUnit));
        if (!currentUnit)
        {
            FatalError("malloc() failed. Var: currentUnit");
            return STANDARD_ERROR;
        }
        *currentUnit = (IconProcessUnit){ .downloadingPage = true };

        //получить абсолютный путь до ярлыка
        wchar_t CurrentDesktopFileAbsolutePath[MAX_PATH];
        if (FAILED(StringCchPrintfW(CurrentDesktopFileAbsolutePath, MAX_PATH, L"%ls\\%ls", desktopPath, fileData.cFileName)))
        {
            FatalError("Could not build absolute path to one of .url file. StringCchPrintfW() failed");
            return STANDARD_ERROR;
        }
        LogWstring("[DEBUG] absolute path: \"%s\"\n", CurrentDesktopFileAbsolutePath, "CurrentDesktopFileAbsolutePath");

        if (!ProcessResponseFilePath(currentUnit->pageResponseFilePath, &pages, fileData.cFileName, "currentUnit->pageResponceFilePath")) continue;
        if (!ProcessResponseFilePath(currentUnit->faviconResponseFilePath, &favicons, fileData.cFileName, "currentUnit->faviconResponceFilePath")) continue;

        //скопировать ini–текст из ярлыков
        LARGE_INTEGER fileSize = (LARGE_INTEGER){.LowPart = fileData.nFileSizeLow, .HighPart = fileData.nFileSizeHigh};
        byte *content = malloc(fileSize.QuadPart);  //создать буффер для копирования
        if(!content)
        {
            fprintf(log, "[ERROR] Could not allocate memory for output buffer. malloc() failed\n");
            continue;
        }
        PushCleanupStack(cleanupStack, Warp_Free, &content);        // (7 глобально +1 временно ->8)
        FILE *currentDesktopFile = _wfopen(CurrentDesktopFileAbsolutePath, L"rb");  //файл откуда копировать
        if (!currentDesktopFile)
        {
            fprintf(log, "[ERROR] Could not open .url file for reading. _wfopen() failed.\n");
            SingleDeallocation(cleanupStack);   //(8->7)
            continue;
        }
        PushCleanupStack(cleanupStack, Warp_FClose, &currentDesktopFile);   // (7 глобально +2 временно ->9)
        fread(content, 1, fileSize.QuadPart, currentDesktopFile);

        currentUnit->url = ParceIniText(content, fileSize.QuadPart);   
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
        
        if (!(currentUnit->easy = curl_easy_init()))
        {
            fprintf(log, "[ERROR] Could not initialize libcurl easy handle to transfer data by url. curl_easy_init() failed\n");
            PartialDeallocation(cleanupStack, 2);   //(9->7)
            continue;
        }
        curl_easy_setopt(currentUnit->easy, CURLOPT_URL, currentUnit->url);                 //url по которому обращаться
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEFUNCTION, WriteCallback);          //колбек когда приходят данные
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEDATA, &currentUnit->responseFile); //параметр, с которым вызывается колбек
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
    size_t successfulHtmlResponses = 0, successfulFaviconResponses = 0;
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
                CURL *easy = curlMsg->easy_handle;  //завершенный easy
                IconProcessUnit *ipu = NULL;
                if (curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ipu) != CURLE_OK)
                {
                    CurlGetinfoFailMessage("CURLINFO_PRIVATE", &ipu->logBuffer);
                    continue;
                }
                if (ipu->downloadingPage)
                {
                    ipu->downloadingPage = false;
                    
                    AddLogBuffer(&ipu->logBuffer, "[DEBUG] Page transfer finished\n[DEBUG] Exit code:               %s\n", curl_easy_strerror(curlMsg->data.result));
                    AddLogBuffer(&ipu->logBuffer, "[DEBUG] Initial URL:             %s\n", ipu->url);
                    char *lastUrl = NULL;
                    if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &lastUrl) == CURLE_OK)
                    {
                        AddLogBuffer(&ipu->logBuffer, "[DEBUG] last used effective URL: %s\n", lastUrl);
                    }
                    else CurlGetinfoFailMessage("CURLINFO_EFFECTIVE_URL", &ipu->logBuffer);
                    
                    long responseCode;
                    if (curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode) == CURLE_OK)
                    {
                        char *temp = NULL;
                        MakeMessageLong(&temp, "%ld", responseCode);
                        if (!temp)
                        {
                            fprintf(log, "хуй\n");
                            continue;
                        }
                        AddLogBuffer(&ipu->logBuffer, "[DEBUG] Response code:           %s\n", temp);
                        free(temp);
                    }
                    else CurlGetinfoFailMessage("CURLINFO_RESPONSE_CODE", &ipu->logBuffer);

                    char *contentType = NULL;
                    if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &contentType) == CURLE_OK)
                    {
                        if (contentType)
                        {
                            AddLogBuffer(&ipu->logBuffer, "[DEBUG] Content type:            %s\n", contentType);
                        } 
                        else
                        {
                            AddLogBuffer(&ipu->logBuffer, "[ERROR] The server did not send a valid Content-Type header or the protocol used does not support this\n", NULL);
                            continue;
                        }
                    }
                    else CurlGetinfoFailMessage("CURLINFO_CONTENT_TYPE", &ipu->logBuffer);
                    AddLogBuffer(&ipu->logBuffer, "\n", NULL);
                    
                    bool ableParseHTML = true;
                    if (responseCode != 200)
                    {
                        AddLogBuffer(&ipu->logBuffer, "[ERROR] Response code is not 200\n", NULL);
                        ableParseHTML = false;
                    }
                    char *targetType = "text/html";
                    if (strncmp(contentType, targetType, strlen(targetType)))
                    {
                        AddLogBuffer(&ipu->logBuffer, "[ERROR] Response content type was not recognized as \"%s\"\n", targetType);
                        ableParseHTML = false;
                    }

                    if (ableParseHTML)
                    {
                        ++successfulHtmlResponses;

                        fclose(ipu->responseFile);
                        if (!(ipu->responseFile = _wfopen(ipu->pageResponseFilePath, L"rb")))
                        {
                            AddLogBuffer(&ipu->logBuffer, "[ERROR] Could not open responce file. _wfopen() failed", NULL);
                            continue;
                        }
                        WIN32_FILE_ATTRIBUTE_DATA responceFileData;
                        if (!GetFileAttributesExW(ipu->pageResponseFilePath, GetFileExInfoStandard, &responceFileData))
                        {
                            AddLogBuffer(&ipu->logBuffer, "[ERROR] Could not get size of responce file. GetFileAttributesExW() failed", NULL);
                            continue;
                        }
                        ULARGE_INTEGER responceFileSize = { .LowPart = responceFileData.nFileSizeLow, .HighPart = responceFileData.nFileSizeHigh };
                        char *buffer = malloc(responceFileSize.QuadPart);
                        if (!buffer)
                        {
                            AddLogBuffer(&ipu->logBuffer, "[ERROR] malloc() failed\n", NULL);
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Warp_Free, &buffer);     //(7 глобально +1 временно ->8)

                        fread(buffer, 1, responceFileSize.QuadPart, ipu->responseFile);
                        char *faviconUrl = GetFaviconUrl(buffer, responceFileSize.QuadPart, lastUrl);
                        if(!faviconUrl)
                        {
                            AddLogBuffer(&ipu->logBuffer, "[ERROR] Could not get favicon url. GetFaviconUrl() failed.\n", NULL);
                            SingleDeallocation(cleanupStack);   //(8->7)
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Warp_Free, &faviconUrl);     //(7 глобально +2 временно ->9)
                        AddLogBuffer(&ipu->logBuffer, "[DEBUG] Favicon url: %s\n", faviconUrl);
                        curl_multi_remove_handle(iconsProcessContainer.multi, easy);
                        fclose(ipu->responseFile);

                        curl_easy_setopt(easy, CURLOPT_URL, faviconUrl); // перенастраиваем
                        curl_easy_setopt(easy, CURLOPT_TIMEOUT, 8L);
                        if (!(ipu->responseFile = _wfopen(ipu->faviconResponseFilePath, L"wb")))
                        {
                            PartialDeallocation(cleanupStack, 2);   //(9->7)
                            AddLogBuffer(&ipu->logBuffer, "[ERROR] Could not open file for favicon responce\n", NULL);
                            continue;
                        }

                        AddLogBuffer(&ipu->logBuffer, "[DEBUG] Successfuly configured curl easy handle to download favicon\n", NULL);
                        curl_multi_add_handle(iconsProcessContainer.multi, easy);
                        PartialDeallocation(cleanupStack, 2);       //(9->7)
                    }
                }
                else
                {
                    ++successfulFaviconResponses;
                    fprintf(log, "\n|==================================================================================|\n\n");
                    fprintf(log, ipu->logBuffer.content);
                }
            }
        }
    }
    while (runningHandles);
    fprintf(log, "\n|==================================================================================|\n\n[DEBUG] transfers cycle finished\n");
    fprintf(log, "[INFO] Found files: %zd\n", iconsProcessContainer.occupedUnits);
    fprintf(log, "[INFO] Processed files correctly: %zd\n", DesktopFilesProcessedCorrectly);
    fprintf(log, "[INFO] Successful \"%s\" responses: %zd\n", pages.folderNameUtf8, successfulHtmlResponses);
    fprintf(log, "[INFO] Successful \"%s\" responses: %zd\n", favicons.folderNameUtf8, successfulHtmlResponses);
    
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
    fwrite(ptr, 1, size *= nmemb, *(FILE **)userdata);
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

void CurlGetinfoFailMessage(char *type, LogBuffer *logBuffer) 
{
    AddLogBuffer(logBuffer, "[ERROR] Could not get info from curl easy handle. curl_easy_getinfo(%s) failed\n", type);
}

bool DropDirectory(const wchar_t *directory, const wchar_t *extention)
{
    wchar_t searchToken[MAX_PATH];
    bool returnValue = true;
    if(FAILED(StringCchPrintfW(searchToken, MAX_PATH, L"%ls*.%ls", directory, extention))) return false;
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
        if(!DeleteFileW(absoluteFilePath)) returnValue = false;
    } 
    while (FindNextFileW(searchHandle, &fileData));
    if (GetLastError() != ERROR_NO_MORE_FILES) returnValue = false;
    FindClose(searchHandle);
    return returnValue;
}

int MakeMessage(char **buffer, const char *format, const char *value)
{
    int len = snprintf(NULL, 0, format, value);
    if (len < 0)
    {
        *buffer = NULL;
        return -1;
    } 
    *buffer = malloc((size_t)len + 1);
    if (!*buffer) return -1; 
    snprintf(*buffer, (size_t)len + 1, format, value);
    return len;
}

bool ProcessResponseFolderInfo(ResponceFolderInfo *info)
{
    PushCleanupStack(cleanupStack, ResponseFolderInfoDestructor, info);
    info->folderNameUtf8 = WstringToUtf8(info->folderName);
    if (FAILED(StringCchPrintfW(info->path, MAX_PATH, L"%ls\\responce\\%ls\\", appFolder, info->folderName)))
    {
        if (!info->folderNameUtf8) FatalError("Could not make utf8 string from wide-char to build error message. WstringToUtf8() failed");
        char *errorMessage = NULL;
        MakeMessage(&errorMessage, "Could not build \"%s\" path for responces", info->folderNameUtf8);
        if (!errorMessage) FatalError("Could not build error message. MakeMessage() failed");
        FatalError(errorMessage);
        free(errorMessage);
        return false;
    }
    else
    {
        char *pathUtf8 = WstringToUtf8(info->path);
        if (pathUtf8)
        {
            fprintf(log, "[DEBUG] Build path for responce files \"%s\": \"%s\"\n", info->folderNameUtf8, pathUtf8);
            free(pathUtf8);
        }
        else fprintf(log, "[ERROR] Successfuly build \"%s\" path, however could not convert path to utf8 for app.log. WstringToUtf8() failed");
        if (DropDirectory(info->path, info->extentionOfFiles))
            fprintf(log, "[DEBUG] Directory cleaned up.\n");
        else fprintf(log, "[ERROR] Failed to clean up directory. Some files may remain\n");
    }
    return true;
}

bool ProcessResponseFilePath(wchar_t *value, ResponceFolderInfo *pathInfo, const wchar_t *fileName, const char *variableName)
{
    if
    (
        FAILED(StringCchPrintfW(value, MAX_PATH, L"%ls%ls", pathInfo->path, fileName)) ||
        PathCchRenameExtension(value, MAX_PATH, pathInfo->extentionOfFiles) != S_OK
    )
    {
        fprintf(log, "[ERROR] Could not build absolute path to one of debug\\responce\\* file. StringCchPrintfW() or PathCchRenameExtension() failed\n");
        return false;
    }
    char *formatForLogWstring = NULL;
    MakeMessage(&formatForLogWstring, "[DEBUG] \"%s\" responce file path: \"%%s\"\n", pathInfo->folderNameUtf8);
    if (!formatForLogWstring)
    {
        fprintf(log, "[ERROR] Successfuly build responce file path however could not build informative message for app.log. MakeMessage() failed\n");
        return true;
    }
    LogWstring(formatForLogWstring, value, variableName);
    free(formatForLogWstring);
    return true;
}

bool AddLogBuffer(LogBuffer *logBuffer, const char *dataFormat, const char *value)
{
    char *data = NULL;
    int dataLength = MakeMessage(&data, dataFormat, value);
    if (dataLength < 0) return false;
    char *temp = realloc(logBuffer->content, logBuffer->length + dataLength + 1);
    if (!temp) 
    {
        free(data);
        return false;
    }
    logBuffer->content = temp;
    memcpy(logBuffer->content + logBuffer->length, data, dataLength + 1);
    logBuffer->length += dataLength;
    free(data);
    return true;
}

int MakeMessageLong(char **buffer, const char *format, const long value)
{
    int len = snprintf(NULL, 0, format, value);
    if (len < 0)
    {
        *buffer = NULL;
        return -1;
    } 
    *buffer = malloc((size_t)len + 1);
    if (!*buffer) return -1; 
    snprintf(*buffer, (size_t)len + 1, format, value);
    return len;
}

static void FatalError(const byte *message)
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



void Warp_FindClose(const void *arg)
{
    FindClose(*(HANDLE *)arg);
}

void Warp_CoTaskMemFree(const void *arg)
{
    CoTaskMemFree(*(wchar_t **)arg);
}

void Warp_Free(const void *arg)
{
    free(*(void **)arg);
}

void IconsProcessContainerDestructor(const void *arg)
{
    IconsProcessContainer *container = (IconsProcessContainer *)arg;
    while (container->occupedUnits > 0)
    {
        IconProcessUnit *unit = container->array[--container->occupedUnits];
        free(unit->url);
        free(unit->logBuffer.content);
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

void Warp_FClose(const void *arg)
{
    fclose(*(FILE **)arg);
}

void Warp_curl_global_cleanup(const void *arg)
{
    curl_global_cleanup();
}

void ResponseFolderInfoDestructor(const void *arg)
{
    free((*(ResponceFolderInfo *)arg).folderNameUtf8);
}



char *GetFaviconUrl(const char *buffer, size_t size, const char *base_url)
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

    char *final_url = NULL;
    if (href)
    {
        // если ссылка абсолютная
        if (strstr((char *)href, "http://") || strstr((char *)href, "https://"))
            final_url = strdup((char *)href);
        else
        {
            // относительная → делаем абсолютную
            xmlChar *abs = xmlBuildURI(href, (xmlChar *)base_url);
            if (abs)
            {
                final_url = strdup((char *)abs);
                xmlFree(abs);
            }
        }
        xmlFree(href);
    }

    xmlXPathFreeObject(result);
    xmlXPathFreeContext(ctx);
    xmlFreeDoc(doc);
    return final_url;
}