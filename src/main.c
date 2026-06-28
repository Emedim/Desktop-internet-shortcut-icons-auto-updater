#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath();
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW(); StringCchCopyW();
#include <pathcch.h>    //PathCchRemoveFileSpec

#include <curl/curl.h>

#include "cleanup interface.h"
#define STANDARD_ERROR (-1)
#define MAX_CURRENT_SYSTEM_RESOURCES (7)
#define INICIAL_BUFFER_LENGTH (12)
#define BUFFER_ADDITION (8)

size_t CurlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
static void FatalError(const byte *message);
void ParceFileText(const byte *text, size_t textLength, byte **result);
char *WstringTo_utf8(const wchar_t *wstr);      // возвращает либо указатель на готовую конвертированную строку, либо NULL
void LogWsting(const char *format, const wchar_t *wstr, const char *var);

void Warp_FindClose(const void *arg);
void Warp_CoTaskMemFree(const void *arg);
void Warp_Free(const void *arg);
void Warp_Free_iconsProcessContainer(const void *arg);
void Warp_FClose(const void *arg);
void Warp_curl_global_cleanup(const void *arg);


typedef struct
{
    byte *url;
    CURL *easy;
    FILE *download;
    size_t receivedBytes;
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
    if (!(log = _wfopen(logPath, L"w")))
    {
        FatalError("could not open .log file");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_FClose, &log);

    LogWsting("[DEBUG] Current app folder (CWD with no \\bin): \"%s\"\n", cwd, "cwd");
    LogWsting("[DEBUG] app.log Path: \"%s\"\n", logPath, "logPath");

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

    fprintf(log, "[DEBUG] Started searching and processing .url files in cycle\n");
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
        currentUnit->easy = NULL;
        currentUnit->url = NULL;
        currentUnit->download = NULL;
        currentUnit->receivedBytes = 0;

        LARGE_INTEGER fileSize;
        fileSize.LowPart = fileData.nFileSizeLow;
        fileSize.HighPart = fileData.nFileSizeHigh; // fileSize.QuadPart – размер фала

        //получить абсолютный путь до ярлыка
        wchar_t absoluteFilePath[MAX_PATH];
        if (FAILED(StringCchPrintfW(absoluteFilePath, MAX_PATH, L"%ls\\%ls", desktopPath, fileData.cFileName)))
        {
            FatalError("Could not get absolute path to one of .url file. StringCchPrintfW() failed");
            return STANDARD_ERROR;
        }
        LogWsting("[DEBUG] absolute path: \"%s\"\n", absoluteFilePath, "absoluteFilePath");

        wchar_t absoluteDownloadFilePath[MAX_PATH];
        if
        (
            FAILED(StringCchPrintfW(absoluteDownloadFilePath, MAX_PATH, L"%ls\\debug\\download\\%ls", cwd, fileData.cFileName)) ||
            PathCchRenameExtension(absoluteDownloadFilePath, MAX_PATH, L"txt") != S_OK
        )
        {
            FatalError("Could not get absolute path to one of debug\\download\\* file. StringCchPrintfW() or PathCchRenameExtension() failed");
            return STANDARD_ERROR;
        }
        LogWsting("[DEBUG] debug file to process url path: \"%s\"\n", absoluteDownloadFilePath, "absoluteDownloadFilePath");

        //скопировать ini–текст из ярлыков
        FILE *file = _wfopen(absoluteFilePath, L"rb");
        if (!file)
        {
            fprintf(log, "[ERROR] Could not open .url file for reading. _wfopen() failed.\n");
            continue;
        }
        PushCleanupStack(cleanupStack, Warp_FClose, &file);
        byte *fileContent = malloc(fileSize.QuadPart);
        if(!fileContent)
        {
            fprintf(log, "[ERROR] Could not allocate memory for output buffer. malloc() failed\n");
            SingleDeallocation(cleanupStack);
            continue;
        }
        PushCleanupStack(cleanupStack, Warp_Free, &fileContent);

        fread(fileContent, 1, fileSize.QuadPart, file);
        ParceFileText(fileContent, fileSize.QuadPart, &currentUnit->url);
        if (!currentUnit->url)
        {
            fprintf(log, "[ERROR] Could parse .url content. ParceFileText() failed\n");
            PartialDeallocation(cleanupStack, 2);
            continue;
        }
        fprintf(log, "[DEBUG] Got url: %s\n", currentUnit->url);
        
        if (!(currentUnit->download = _wfopen(absoluteDownloadFilePath, L"wb")))
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
        curl_easy_setopt(currentUnit->easy, CURLOPT_URL, currentUnit->url);         //url по которому обращаться
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEFUNCTION, CurlWriteCallback);  //колбек когда приходят данные
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEDATA, currentUnit);        //параметр, с которым вызывается колбек
        curl_easy_setopt(currentUnit->easy, CURLOPT_PRIVATE, currentUnit);          //ассоциация easy с IconProcessUnit
        curl_easy_setopt(currentUnit->easy, CURLOPT_TIMEOUT, 15L);                  //Запрос длиться не более 15 секунд
        curl_easy_setopt(currentUnit->easy, CURLOPT_FOLLOWLOCATION, 1L);            //Редиректы
        curl_multi_add_handle(iconsProcessContainer.multi, currentUnit->easy);
        

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
    fprintf(log, "[INFO] Processed files: %zd\n", iconsProcessContainer.occupedUnits);

    fprintf(log, "[DEBUG] Started transfers cycle ...\n");
    int runningHandles;
    do
    {
        if(curl_multi_perform(iconsProcessContainer.multi, &runningHandles) != CURLM_OK)
        {
            FatalError("curl_multi_perform() failed");
            return STANDARD_ERROR;
        }
        // curl_multi_wait(iconsProcessContainer.multi, NULL, 0, 1000, NULL);

        // CURLMsg *curlMsg;
        // int curlMsgLeft;

        // while ((curlMsg = curl_multi_info_read(iconsProcessContainer.multi, &curlMsgLeft)))
        // {

        // }
    }
    while (runningHandles);
    fprintf(log, "[DEBUG] transfers cycle finished\n");
    
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);   //для обновления ярлыков
    CompleteDeallocation(cleanupStack);
    return 0;
}



size_t CurlWriteCallback
(
    char *ptr,      //указатель на пришедшие данные
    size_t size,    //size * nmemb = количество пришедших байт
    size_t nmemb,   
    void *userdata  //пользовательские данные. Задаётся через curl_easy_setopt(easy, CURLOPT_WRITEDATA, somePtr). Я здесь получаю IconProcessUnit *
)
{
    IconProcessUnit *unit = (IconProcessUnit *)userdata;
    size_t receivedBytes = size * nmemb;
    unit->receivedBytes += receivedBytes;
    fwrite(ptr, 1, receivedBytes, unit->download);
    return receivedBytes;    //функция должна возвращать количество обработаных байтов
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
    else fprintf(log, "[ERROR] Could not convert (wchar_t *)%s to utf-8. Error of WstringTo_utf8()\n", var);
}

static void FatalError(const byte *message)
{
    if(log)
    {
        fprintf(log, "[FATAL ERROR] %ls\n", message);
        MessageBoxA(NULL, "The program terminated due to a fatal error. See the log file for details.", NULL, MB_OK);
    } 
    else MessageBoxA(NULL, message, NULL, MB_OK);
    if (cleanupStack) CompleteDeallocation(cleanupStack);   //если cleanupStack не NULL
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
        if (temp->url)
        {
            free(temp->url);
            if (temp->download)
            {
                fclose(temp->download);
                if(temp->easy)
                {
                    curl_multi_remove_handle(realTypeArg->multi, temp->easy);
                    curl_easy_cleanup(temp->easy);
                }
            }
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