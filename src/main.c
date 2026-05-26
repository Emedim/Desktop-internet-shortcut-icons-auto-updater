#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath();

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW(); StringCchCopyW();

//для _setmode(_fileno(stdout), _O_U16TEXT);
#include <io.h>
#include <fcntl.h>

#include "cleanup interface.h"
#define STANDARD_ERROR (-1)
#define MAX_CURRENT_SYSTEM_RESOURCES (5)
#define INICIAL_BUFFER_LENGTH (12)
#define BUFFER_ADDITION (8)

static void FatalError(const byte *message);
void ParceFileText(const byte *text, size_t textLength, byte **result);

void Warp_FindClose(const void *arg);
void Warp_CoTaskMemFree(const void *arg);
void Warp_Free(const void *arg);
void Warp_Free_IconProcessContainer(const void *arg);
void Warp_FClose(const void *arg);


typedef struct tag_IconProcessUnit
{
    byte *url;
} IconProcessUnit;

typedef struct tag_IconProcessContainer
{
    IconProcessUnit *array;
    size_t occupedUnits;
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
    
    size_t processingFilesContainerLength = INICIAL_BUFFER_LENGTH;
    IconProcessContainer iconProcessContainer = {0};
    iconProcessContainer.array = malloc(processingFilesContainerLength * sizeof(IconProcessUnit));
    if(!iconProcessContainer.array) // не NULL
    {
        FatalError("error of allocation heap");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_Free_IconProcessContainer, &iconProcessContainer);

    FILE *debug = fopen("C:\\Users\\emedi\\Documents\\Проекты по программированию\\Complex projects\\Desktop icons auto updater\\debug\\debug.txt", "wb"); //запишу сюда извлёченные данные для проверки
    do
    {
        //создать буффер для хранения информации о ярлыках
        ++iconProcessContainer.occupedUnits;
        if (iconProcessContainer.occupedUnits > processingFilesContainerLength)
        {
            processingFilesContainerLength += BUFFER_ADDITION;
            IconProcessUnit *temp = realloc(iconProcessContainer.array, processingFilesContainerLength * sizeof(IconProcessUnit)); //выделили новый массив, количество элементов: старое количество + немного ещё
            if (!temp)  // temp == NULL
            {
                --iconProcessContainer.occupedUnits;
                FatalError("error of allocation heap");
                return STANDARD_ERROR;
            }
            iconProcessContainer.array = temp;
        }
        iconProcessContainer.array[iconProcessContainer.occupedUnits - 1].url = NULL;

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
        
        wprintf(L"File: %ls\n", fileData.cFileName);

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
        ParceFileText(fileContent, fileSize.QuadPart, &(iconProcessContainer.array[iconProcessContainer.occupedUnits - 1].url));
        if (!iconProcessContainer.array[iconProcessContainer.occupedUnits - 1].url)
        {
            PartialDeallocation(cleanupStack, 2);
            continue;
        }
        
        fwrite(
            iconProcessContainer.array[iconProcessContainer.occupedUnits - 1].url,
            sizeof(char),
            strlen(iconProcessContainer.array[iconProcessContainer.occupedUnits - 1].url),
            debug
        );  //Для проверки закидываем в файл полученные данные.

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
    HANDLE *realTypeArg = arg;
    FindClose(*realTypeArg);
}

void Warp_CoTaskMemFree(const void *arg)
{
    PWSTR *realTypeArg = arg;
    CoTaskMemFree(*realTypeArg);
}

void Warp_Free(const void *arg)
{
    void **realTypeArg = arg;
    free(*realTypeArg);
}

void Warp_Free_IconProcessContainer(const void *arg)
{
    IconProcessContainer *realTypeArg = (IconProcessContainer *)arg;
    while (realTypeArg->occupedUnits > 0)
    {
        --realTypeArg->occupedUnits;
        if (realTypeArg->array[realTypeArg->occupedUnits].url) 
            free(realTypeArg->array[realTypeArg->occupedUnits].url);
    }
    free(realTypeArg->array);
}

void Warp_FClose(const void *arg)
{
    FILE **realTypeArg = arg;
    fclose(*realTypeArg);
}