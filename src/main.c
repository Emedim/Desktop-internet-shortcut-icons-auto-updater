#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath();
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW(); StringCchCopyW();

#include <curl/curl.h>

//для _setmode(_fileno(stdout), _O_U16TEXT);
#include <io.h>
#include <fcntl.h>

#include "cleanup interface.h"
#define STANDARD_ERROR (-1)
#define MAX_CURRENT_SYSTEM_RESOURCES (6)
#define INICIAL_BUFFER_LENGTH (12)
#define BUFFER_ADDITION (8)

size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
static void FatalError(const byte *message);
void ParceFileText(const byte *text, size_t textLength, byte **result);

void Warp_FindClose(const void *arg);
void Warp_CoTaskMemFree(const void *arg);
void Warp_Free(const void *arg);
void Warp_Free_IconProcessContainer(const void *arg);
void Warp_FClose(const void *arg);
void Warp_curl_global_cleanup(const void *arg);


typedef struct tag_IconProcessUnit
{
    byte *url;
    CURL *easy;
    size_t receivedBytes;
} IconProcessUnit;

typedef struct tag_IconProcessContainer
{
    IconProcessUnit *array;
    CURLM *multi;
    size_t occupedUnits;
    size_t capacity;
} IconProcessContainer;


CleanupStack cleanupStack = NULL;

int main(void)
{
    _setmode(_fileno(stdout), _O_U16TEXT);  //CRT теперь печатает в консоль только unicode

    cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
    if (cleanupStack == NULL)
    {
        FatalError("initialization error");
        return STANDARD_ERROR;
    }

    PWSTR desktopPath = NULL;
    HRESULT desktopPathResult;
    desktopPathResult = SHGetKnownFolderPath // взять путь до определённой папки
    (
        &FOLDERID_Desktop, // рабочий стол
        0,
        NULL,
        &desktopPath
    );
    PushCleanupStack(cleanupStack, Warp_CoTaskMemFree, &desktopPath);   //desktopPath освобождать даже в случае неудачи

    if (FAILED(desktopPathResult)) 
    {
        FatalError("Could not find path to desktop");
        return STANDARD_ERROR;
    }
    
    wprintf(L"Путь к рабочему столу: %ls\n\n", desktopPath);   //показать путь к рабочему столу
    
    //приведение пути к рабочему столу к виду, пригодному для передачи в FindFirstFileW для поиска файлов на рабочем столе
    wchar_t searchPath[MAX_PATH];
    if
    (
        StringCchCopyW(searchPath, MAX_PATH, desktopPath) != S_OK ||
        StringCchCatW(searchPath, MAX_PATH, L"\\*.url") != S_OK
    )
    {
        FatalError("Error of pathes");
        return STANDARD_ERROR;
    }

    WIN32_FIND_DATAW fileData;                                                  // информация о файле
    HANDLE searchingFilesHandle = FindFirstFileW(searchPath, &fileData);        // получить дескриптор поиска и получить первый файл
    if (INVALID_HANDLE_VALUE == searchingFilesHandle)
    {
        FatalError("Invalid files searching descriptor value");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_FindClose, &searchingFilesHandle);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        FatalError("curl_global_init failed");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, &Warp_curl_global_cleanup, NULL);
    IconProcessContainer iconProcessContainer = {NULL, 0, INICIAL_BUFFER_LENGTH, curl_multi_init()};
    if (!iconProcessContainer.multi)
    {
        FatalError("error of libcurl initialization");
        return STANDARD_ERROR;
    }
    iconProcessContainer.array = malloc(iconProcessContainer.capacity * sizeof(IconProcessUnit));
    if(!iconProcessContainer.array)
    {
        FatalError("error of allocation heap");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_Free_IconProcessContainer, &iconProcessContainer);

    FILE *debug = fopen("C:\\Users\\emedi\\Documents\\Проекты по программированию\\Complex projects\\Desktop icons auto updater\\debug\\debug.txt", "wb"); //запишу сюда извлёченные данные для проверки
    do
    {
        //создать буффер для хранения информации о ярлыках
        if (iconProcessContainer.occupedUnits >= iconProcessContainer.capacity)
        {
            iconProcessContainer.capacity += BUFFER_ADDITION;
            IconProcessUnit *temp = realloc(iconProcessContainer.array, iconProcessContainer.capacity * sizeof(IconProcessUnit)); //выделили новый массив, количество элементов: старое количество + немного ещё
            if (!temp)  // temp == NULL
            {
                FatalError("error of allocation heap");
                return STANDARD_ERROR;
            }
            iconProcessContainer.array = temp;
        }
        IconProcessUnit *currentUnit = &iconProcessContainer.array[iconProcessContainer.occupedUnits++];
        currentUnit->url = NULL;
        currentUnit->easy = NULL;
        currentUnit->receivedBytes = 0;


        LARGE_INTEGER fileSize;
        fileSize.LowPart = fileData.nFileSizeLow;
        fileSize.HighPart = fileData.nFileSizeHigh; // fileSize.QuadPart – размер фала

        //получить абсолютный путь до ярлыка
        wchar_t absoluteFilePath[MAX_PATH];
        
        if
        (
            StringCchCopyW(absoluteFilePath, MAX_PATH, desktopPath) != S_OK ||
            StringCchCatW(absoluteFilePath, MAX_PATH, L"\\") != S_OK ||
            StringCchCatW(absoluteFilePath, MAX_PATH, fileData.cFileName) != S_OK
        )
        {
            FatalError("error of pathes");
            return STANDARD_ERROR;
        }
        
        wprintf(L"File: %ls\n", fileData.cFileName);    //показать файл

        //скопировать ini–текст из ярлыков
        FILE *file = _wfopen(absoluteFilePath, L"rb");
        if (!file)  //file == NULL
            continue;
        PushCleanupStack(cleanupStack, Warp_FClose, &file);
        byte *fileContent = malloc(fileSize.QuadPart);
        if(!fileContent)  //fileContent == NULL
        {
            SingleDeallocation(cleanupStack);
            continue;
        }
        PushCleanupStack(cleanupStack, Warp_Free, &fileContent);

        fread(fileContent, 1, fileSize.QuadPart, file);
        ParceFileText(fileContent, fileSize.QuadPart, &currentUnit->url);
        if (!currentUnit->url)
        {
            PartialDeallocation(cleanupStack, 2);
            continue;
        }
        
        fwrite(
            currentUnit->url,
            sizeof(char),
            strlen(currentUnit->url),
            debug
        );  //Для проверки закидываем в файл полученные данные.

        currentUnit->easy = curl_easy_init();
        if (!currentUnit->easy)
        {
            PartialDeallocation(cleanupStack, 2);
            continue;
        }
        curl_easy_setopt(currentUnit->easy, CURLOPT_URL, currentUnit->url);
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(currentUnit->easy, CURLOPT_WRITEDATA, currentUnit);
        curl_multi_add_handle(iconProcessContainer.multi, currentUnit->easy);

        PartialDeallocation(cleanupStack, 2);
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));

    fclose(debug);  //закрытие файла дебага

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES)
    {
        FatalError("Unknown error of searching files");
        return STANDARD_ERROR;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);   //для обновления ярлыков
    wprintf(L"\nИтого файлов: %d\n\n", iconProcessContainer.occupedUnits);
    
    CompleteDeallocation(cleanupStack);
    return 0;
}



size_t WriteCallback
(
    char *ptr,      //указатель на пришедшие данные
    size_t size,    //size * nmemb = количество пришедших байт
    size_t nmemb,   
    void *userdata  //пользовательские данные. Задаётся через curl_easy_setopt(easy, CURLOPT_WRITEDATA, somePtr). Я здесь получаю IconProcessUnit *
){
    IconProcessUnit *unit = (IconProcessUnit *)userdata;
    unit->receivedBytes += size * nmemb;
    printf("%zu\n", unit->receivedBytes);
    return unit->receivedBytes;    //функция должна возвращать количество обработаных байтов
}

static void FatalError(const byte *message)
{
    if (cleanupStack) CompleteDeallocation(cleanupStack);   //если cleanupStack не NULL
    MessageBoxA
    (
        NULL,
        message,
        NULL,
        MB_OK
    );
}



void Warp_FindClose(const void *arg)
{
    FindClose(*(HANDLE *)arg);
}

void Warp_CoTaskMemFree(const void *arg)
{
    CoTaskMemFree(*(PWSTR *)arg);
}

void Warp_Free(const void *arg)
{
    free(*(void **)arg);
}

void Warp_Free_IconProcessContainer(const void *arg)
{
    IconProcessContainer *realTypeArg = (IconProcessContainer *)arg;
    while (realTypeArg->occupedUnits > 0)
    {
        --realTypeArg->occupedUnits;
        if (realTypeArg->array[realTypeArg->occupedUnits].url)
        {
            free(realTypeArg->array[realTypeArg->occupedUnits].url);
            if(realTypeArg->array[realTypeArg->occupedUnits].easy)
            {
                curl_multi_remove_handle(realTypeArg->multi, realTypeArg->array[realTypeArg->occupedUnits].easy);
                curl_easy_cleanup(realTypeArg->array[realTypeArg->occupedUnits].easy);
            }
        }
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