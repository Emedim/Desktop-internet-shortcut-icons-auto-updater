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
#define MAX_CURRENT_SYSTEM_RESOURCES (7)
#define INICIAL_BUFFER_LENGTH (12)
#define BUFFER_ADDITION (8)

size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
static void FatalError(const byte *message);
char *WstringTo_utf8(const wchar_t *wstr);      // возвращает либо указатель на готовую конвертированную строку, либо NULL
void CurlGetinfoFailMessage(char *type);
void LogWsting(const char *format, const wchar_t *wstr, const char *var);
char *GetFaviconUrl(const char *buffer, size_t size, const char *base_url);
bool BuildResponcePath(wchar_t *resultPath, const wchar_t *cwd, const wchar_t *endFolder);
bool DropDirectory(const wchar_t *directory, const wchar_t *extention);

void Warp_FindClose(const void *arg);
void Warp_CoTaskMemFree(const void *arg);
void Warp_Free(const void *arg);
void Warp_Free_iconsProcessContainer(const void *arg);
void Warp_FClose(const void *arg);
void Warp_curl_global_cleanup(const void *arg);


typedef struct
{
    byte *url;
    wchar_t *htmlResponceFilePath;
    wchar_t *faviconResponceFilePath;
    CURL *easy;
    FILE *responceFile;
    bool transferingHTML;
} IconProcessUnit;

typedef struct
{
    IconProcessUnit **array;
    CURLM *multi;
    size_t occupedUnits;
    size_t capacity;
} IconsProcessContainer;


CleanupStack cleanupStack = NULL;
FILE* log = NULL;

int main(void)
{
    cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
    if (cleanupStack == NULL)
    {
        FatalError("initialization error. InitCleanupStack() failed");
        return STANDARD_ERROR;
    }

    wchar_t cwd[MAX_PATH];
    if
    (
        !GetCurrentDirectoryW(MAX_PATH, cwd) ||
        PathCchRemoveFileSpec(cwd, MAX_PATH) != S_OK
    )
    {
        FatalError("Could not get CWD or remove \\bin from path. GetCurrentDirectoryW() or PathCchRemoveFileSpec() failed");
        return STANDARD_ERROR;
    }

    wchar_t logPath[MAX_PATH];
    if (FAILED(StringCchPrintfW(logPath, MAX_PATH, L"%ls\\logs\\app.log", cwd)))
    {
        FatalError("StringCchPrintfW() failed");
        return STANDARD_ERROR;
    }
    if (!(log = _wfopen(logPath, L"wb")))
    {
        FatalError("could not open .log file");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_FClose, &log);

    LogWsting("[DEBUG] Current app folder (CWD with no \\bin): \"%s\"\n", cwd, "cwd");
    LogWsting("[DEBUG] app.log Path: \"%s\"\n", logPath, "logPath");

    wchar_t responcePathHtml[MAX_PATH], responcePathFavicon[MAX_PATH];
    if (BuildResponcePath(responcePathHtml, cwd, L"html") || BuildResponcePath(responcePathFavicon, cwd, L"icons")) return STANDARD_ERROR;

    if(DropDirectory(responcePathHtml, L"html")) LogWsting("[DEBUG] Directory cleaned: \"%s\"\n", responcePathHtml, "responcePathHtml");
    else LogWsting("[ERROR] Error of cleaning directory: \"%s\" . Redundant files may remain. DropDirectory() failed\n", responcePathHtml, "responcePathHtml");

    if(DropDirectory(responcePathFavicon, L"html")) LogWsting("[DEBUG] Directory cleaned: \"%s\"\n", responcePathFavicon, "responcePathFavicon");
    else LogWsting("[ERROR] Error of cleaning directory: \"%s\" . Redundant files may remain. DropDirectory() failed\n", responcePathFavicon, "responcePathFavicon");

    wchar_t *desktopPath = NULL;
    HRESULT desktopPathResult = SHGetKnownFolderPath // взять путь до определённой папки
    (
        &FOLDERID_Desktop, // рабочий стол
        0,
        NULL,
        &desktopPath
    );
    PushCleanupStack(cleanupStack, Warp_CoTaskMemFree, &desktopPath);   //desktopPath освобождать даже в случае неудачи
    if (FAILED(desktopPathResult)) 
    {
        FatalError("Could not find path to desktop. SHGetKnownFolderPath() failed");
        return STANDARD_ERROR;
    }
    LogWsting("[DEBUG] Desktop path: \"%s\"\n", desktopPath, "desktopPath");
    
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
        FatalError("Invalid files searching descriptor value. FindFirstFileW() failed");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_FindClose, &searchingFilesHandle);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        FatalError("curl_global_init() failed");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, &Warp_curl_global_cleanup, NULL);
    IconsProcessContainer iconsProcessContainer = {NULL, curl_multi_init(), 0, INICIAL_BUFFER_LENGTH};
    if (!iconsProcessContainer.multi)
    {
        FatalError("curl_multi_init() failed");
        return STANDARD_ERROR;
    }
    iconsProcessContainer.array = malloc(iconsProcessContainer.capacity * sizeof(IconProcessUnit *));
    if(!iconsProcessContainer.array)
    {
        FatalError("malloc() failed. Var: iconsProcessContainer.array");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_Free_iconsProcessContainer, &iconsProcessContainer);

    size_t DesktopFilesProcessedCorrectly = 0;
    fprintf(log, "[DEBUG] Started searching and processing .url files in cycle ...\n");
    do
    {
        fprintf(log, "\n|==================================================================================|\n\n");
        LogWsting("[DEBUG] file name: \"%s\"\n", fileData.cFileName, "fileData.cFileName");
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
        *currentUnit = (IconProcessUnit){ .transferingHTML = true };

        currentUnit->htmlResponceFilePath = malloc(MAX_PATH * sizeof(wchar_t));
        if
        (
            FAILED(StringCchPrintfW(currentUnit->htmlResponceFilePath, MAX_PATH, L"%ls%ls", responcePathHtml, fileData.cFileName)) ||
            PathCchRenameExtension(currentUnit->htmlResponceFilePath, MAX_PATH, L"html") != S_OK
        )
        {
            fprintf(log, "[ERROR] Could not get absolute path to one of debug\\responce\\* file. StringCchPrintfW() or PathCchRenameExtension() failed");
            continue;
        }
        LogWsting("[DEBUG] HTML responce file path: \"%s\"\n", currentUnit->htmlResponceFilePath, "currentUnit->htmlResponceFilePath");

        currentUnit->faviconResponceFilePath = malloc(MAX_PATH * sizeof(wchar_t));
        if
        (
            FAILED(StringCchPrintfW(currentUnit->faviconResponceFilePath, MAX_PATH, L"%ls%ls", responcePathFavicon, fileData.cFileName)) ||
            PathCchRenameExtension(currentUnit->faviconResponceFilePath, MAX_PATH, L"html") != S_OK
        )
        {
            fprintf(log, "[ERROR] Could not get absolute path to one of debug\\icons\\* file. StringCchPrintfW() or PathCchRenameExtension() failed");
            continue;
        }
        LogWsting("[DEBUG] Favicon responce file path: \"%s\"\n", currentUnit->faviconResponceFilePath, "currentUnit->faviconResponceFilePath");

        //получить абсолютный путь до ярлыка
        wchar_t CerrentDesktopFileAbsolutePath[MAX_PATH];
        if (FAILED(StringCchPrintfW(CerrentDesktopFileAbsolutePath, MAX_PATH, L"%ls\\%ls", desktopPath, fileData.cFileName)))
        {
            FatalError("Could not get absolute path to one of .url file. StringCchPrintfW() failed");
            return STANDARD_ERROR;
        }
        LogWsting("[DEBUG] absolute path: \"%s\"\n", CerrentDesktopFileAbsolutePath, "CerrentDesktopFileAbsolutePath");

        //скопировать ini–текст из ярлыков
        LARGE_INTEGER fileSize = (LARGE_INTEGER){.LowPart = fileData.nFileSizeLow, .HighPart = fileData.nFileSizeHigh};
        byte *content = malloc(fileSize.QuadPart);  //создать буффер для копирования
        if(!content)
        {
            fprintf(log, "[ERROR] Could not allocate memory for output buffer. malloc() failed\n");
            continue;
        }
        PushCleanupStack(cleanupStack, Warp_Free, &content);
        FILE *currentDesktopFile = _wfopen(CerrentDesktopFileAbsolutePath, L"rb");  //файл откуда копировать
        if (!currentDesktopFile)
        {
            fprintf(log, "[ERROR] Could not open .url file for reading. _wfopen() failed.\n");
            SingleDeallocation(cleanupStack);
            continue;
        }
        PushCleanupStack(cleanupStack, Warp_FClose, &currentDesktopFile);
        fread(content, 1, fileSize.QuadPart, currentDesktopFile);

        currentUnit->url = ParceIniText(content, fileSize.QuadPart);   
        if (!currentUnit->url)
        {
            fprintf(log, "[ERROR] Could parse .url content. ParceFileText() failed\n");
            PartialDeallocation(cleanupStack, 2);
            continue;
        }
        fprintf(log, "[DEBUG] Got url: %s\n", currentUnit->url);

        if (!(currentUnit->responceFile = _wfopen(currentUnit->htmlResponceFilePath, L"wb")))
        {
            fprintf(log, "[ERROR] Could not open debug file to process url. _wfopen() failed\n");
            PartialDeallocation(cleanupStack, 2);
            continue;
        }
        
        if (!(currentUnit->easy = curl_easy_init()))
        {
            fprintf(log, "[ERROR] Could not initialize libcurl easy handle to transfer data by url. curl_easy_init() failed\n");
            PartialDeallocation(cleanupStack, 2);
            continue;
        }
        curl_easy_setopt(currentUnit->easy, CURLOPT_URL, currentUnit->url);                 //url по которому обращаться
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEFUNCTION, WriteCallback);          //колбек когда приходят данные
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEDATA, &currentUnit->responceFile); //параметр, с которым вызывается колбек
        curl_easy_setopt(currentUnit->easy, CURLOPT_PRIVATE, currentUnit);                  //ассоциация easy с IconProcessUnit
        curl_easy_setopt(currentUnit->easy, CURLOPT_TIMEOUT, 15L);                          //Запрос длиться не более 15 секунд
        curl_easy_setopt(currentUnit->easy, CURLOPT_FOLLOWLOCATION, 1L);                    //Редиректы
        curl_multi_add_handle(iconsProcessContainer.multi, currentUnit->easy);
        
        ++DesktopFilesProcessedCorrectly;
        PartialDeallocation(cleanupStack, 2);
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
        while (curlMsg = curl_multi_info_read(iconsProcessContainer.multi, &curlMsgLeft))
        {
            if (curlMsg->msg == CURLMSG_DONE)
            {
                CURL *easy = curlMsg->easy_handle;  //завершенный easy
                IconProcessUnit *ipu = NULL;
                if (curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ipu) != CURLE_OK)
                {
                    CurlGetinfoFailMessage("CURLINFO_PRIVATE");
                    continue;
                }
                if (ipu->transferingHTML)
                {
                    ipu->transferingHTML = false;
                    fprintf(log, "\n|==================================================================================|\n\n[DEBUG] Transfer finished\n[DEBUG] Exit code:               %s\n", curl_easy_strerror(curlMsg->data.result));

                    fprintf(log, "[DEBUG] Initial URL:             %s\n", ipu->url);
                    char *destinationURL = NULL;
                    if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &destinationURL) == CURLE_OK)
                        fprintf(log, "[DEBUG] last used effective URL: %s\n", destinationURL);
                    else CurlGetinfoFailMessage("CURLINFO_EFFECTIVE_URL");

                    long responseCode;
                    if (curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode) == CURLE_OK)
                        fprintf(log, "[DEBUG] Response code:           %ld\n", responseCode);
                    else CurlGetinfoFailMessage("CURLINFO_RESPONSE_CODE");

                    char *contentType = NULL;
                    if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &contentType) == CURLE_OK)
                    {
                        if (contentType) fprintf(log, "[DEBUG] Content type:            %s\n", contentType);
                        else
                        {
                            fprintf(log, "[ERROR] The server did not send a valid Content-Type header or the protocol used does not support this\n");
                            continue;
                        }
                    }
                    else CurlGetinfoFailMessage("CURLINFO_CONTENT_TYPE");
                    fprintf(log, "\n");
                    
                    bool ableParseHTML = true;
                    if (responseCode != 200)
                    {
                        fprintf(log, "[ERROR] Response code is not 200\n");
                        ableParseHTML = false;
                    }
                    BufferContext bfctx = {contentType, strlen(contentType), 0};
                    char *targetType = "text/html";
                    if (strncmp(contentType, targetType, strlen(targetType)))
                    {
                        fprintf(log, "[ERROR] Response content type was not recognized as \"%s\"\n", targetType);
                        ableParseHTML = false;
                    }

                    if (ableParseHTML)
                    {
                        ++successfulHtmlResponses;

                        fclose(ipu->responceFile);
                        if (!(ipu->responceFile = _wfopen(ipu->htmlResponceFilePath, L"rb")))
                        {
                            fprintf(log, "[ERROR] could not open responce file. _wfopen() failed");
                            continue;
                        }
                        WIN32_FILE_ATTRIBUTE_DATA responceFileData;
                        if (!GetFileAttributesExW(ipu->htmlResponceFilePath, GetFileExInfoStandard, &responceFileData))
                        {
                            fprintf(log, "[ERROR] could not get size of responce file. GetFileAttributesExW() failed");
                            continue;
                        }
                        ULARGE_INTEGER responceFileSize = { .LowPart = responceFileData.nFileSizeLow, .HighPart = responceFileData.nFileSizeHigh };
                        char *buffer = malloc(responceFileSize.QuadPart);
                        if (!buffer)
                        {
                            fprintf(log, "[ERROR] malloc() failed\n");
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Warp_Free, &buffer);

                        fread(buffer, 1, responceFileSize.QuadPart, ipu->responceFile);
                        char *faviconUrl = GetFaviconUrl(buffer, responceFileSize.QuadPart, destinationURL);
                        if(!faviconUrl)
                        {
                            fprintf(log, "[ERROR] Could not get url to favicon. GetFaviconUrl() failed.\n");
                            SingleDeallocation(cleanupStack);
                            continue;
                        }
                        PushCleanupStack(cleanupStack, Warp_Free, &faviconUrl);
                        fprintf(log, "[DEBUG] Favicon url: %s\n", faviconUrl);
                        curl_multi_remove_handle(iconsProcessContainer.multi, easy);
                        fclose(ipu->responceFile);

                        curl_easy_setopt(easy, CURLOPT_URL, faviconUrl); // перенастраиваем
                        curl_easy_setopt(easy, CURLOPT_TIMEOUT, 8L);
                        if (!(ipu->responceFile = _wfopen(ipu->faviconResponceFilePath, L"wb")))
                        {
                            PartialDeallocation(cleanupStack, 2);
                            fprintf(log, "[ERROR] Could not open file for favicon responce\n");
                            continue;
                        }

                        fprintf(log, "[DEBUG] Successfuly configured curl easy handle to download favicon\n");
                        curl_multi_add_handle(iconsProcessContainer.multi, easy);
                        PartialDeallocation(cleanupStack, 2);
                    }
                }
                else
                {
                    ++successfulFaviconResponses;
                    //favicon
                }
            }
        }
    }
    while (runningHandles);
    fprintf(log, "\n|==================================================================================|\n\n[DEBUG] transfers cycle finished\n");
    fprintf(log, "[INFO] Found files: %zd\n", iconsProcessContainer.occupedUnits);
    fprintf(log, "[INFO] Processed files correctly: %zd\n", DesktopFilesProcessedCorrectly);
    fprintf(log, "[INFO] Successful HTML responses: %zd\n", successfulHtmlResponses);
    fprintf(log, "[INFO] Successful favicon responses: %zd\n", successfulHtmlResponses);
    
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

char *WstringTo_utf8(const wchar_t *wstr)
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

void LogWsting(const char *format, const wchar_t *wstr, const char *var)
{
    char* tempMessage_utf8 = WstringTo_utf8(wstr);
    if (tempMessage_utf8)
    {
        fprintf(log, format, tempMessage_utf8);
        free(tempMessage_utf8);
    }
    else fprintf(log, "[ERROR] Could not convert (wchar_t *)%s to utf-8. WstringTo_utf8() failed\n", var);
}

void CurlGetinfoFailMessage(char *type) 
{ 
    fprintf(log, "[ERROR] Could not get info from curl easy handle. curl_easy_getinfo(%s) failed\n", type);
}

bool BuildResponcePath(wchar_t *resultPath, const wchar_t *cwd, const wchar_t *endFolder)
{
    char *endFolderUTF8 = WstringTo_utf8(endFolder);
    bool returnValue = false;
    if (FAILED(StringCchPrintfW(resultPath, MAX_PATH, L"%ls\\responce\\%ls\\", cwd, endFolder)))
    {
        char *errorMessage = NULL;
        char *format = "Could not build path to \"%s\" responce folder. BuildResponcePath() failed";
        int length = snprintf(NULL, 0, format, endFolderUTF8);
        if (endFolderUTF8 && length >= 0)
        {
            errorMessage = malloc(length + 1); // +1 для '\0'
            if (errorMessage) snprintf(errorMessage, length + 1, format, endFolderUTF8);
        }
        FatalError(errorMessage);
        free(errorMessage);
        returnValue = true;
    }
    else
    {
        char *resultPathUTF8 = WstringTo_utf8(resultPath);
        if (endFolderUTF8 && resultPathUTF8) fprintf(log, "[DEBUG] \"%s\" responce folder: \"%s\"\n", endFolderUTF8, resultPathUTF8);
        else fprintf(log, "[DEBUG] BuildResponcePath() sccessfuly returned\n[ERROR] Could not print built path");
        free(resultPathUTF8);
    }
    free(endFolderUTF8);
    return returnValue;
}

bool DropDirectory(const wchar_t *directory, const wchar_t *extention)
{
    wchar_t searchToken[MAX_PATH];
    bool returnValue = true;
    if(FAILED(StringCchPrintfW(searchToken, MAX_PATH, L"%ls*.%ls", directory, extention))) return false;
    WIN32_FIND_DATAW fileData;
    HANDLE searchHandle = FindFirstFileW(searchToken, &fileData);        // получить дескриптор поиска и получить первый файл
    if (searchHandle == INVALID_HANDLE_VALUE) return false;
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

static void FatalError(const byte *message)
{
    char nullMessage[] = "Could not build message about error";
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

void Warp_Free_iconsProcessContainer(const void *arg)
{
    IconsProcessContainer *realTypeArg = (IconsProcessContainer *)arg;
    while (realTypeArg->occupedUnits > 0)
    {
        IconProcessUnit *temp = realTypeArg->array[--realTypeArg->occupedUnits];
        free(temp->url);
        free(temp->htmlResponceFilePath);
        free(temp->faviconResponceFilePath);
        if (temp->responceFile) fclose(temp->responceFile);
        if(temp->easy)
        {
            curl_multi_remove_handle(realTypeArg->multi, temp->easy);
            curl_easy_cleanup(temp->easy);
        }
        free(temp);
    }
    curl_multi_cleanup(realTypeArg->multi);
    free(realTypeArg->array);
}

void Warp_FClose(const void *arg)
{
    fclose(*(FILE **)arg);
}

void Warp_curl_global_cleanup(const void *arg)
{
    curl_global_cleanup();
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