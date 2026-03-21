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
static void ParceFileText(const byte *text, size_t textLength, byte *result);
bool CompareSymbols(byte symbol1, byte symbol2);

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
    size_t *occupedUnitsPtr;
} IconProcessContainer;



CleanupStack cleanupStack = NULL;
const byte internetShortcut[] = "internetshortcut";

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
    PushCleanupStack(cleanupStack, Warp_CoTaskMemFree, &desktopPath);

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

    LARGE_INTEGER fileSize;
    WIN32_FIND_DATAW fileData;                                                  // информация о файле
    HANDLE searchingFilesHandle = FindFirstFileW(searchPath, &fileData);        // получить дескриптор поиска и получить первый файл
    if (INVALID_HANDLE_VALUE == searchingFilesHandle)
    {
        FatalError("Invalid files searching descriptor value");
        return STANDARD_ERROR;
    }
    PushCleanupStack(cleanupStack, Warp_FindClose, &searchingFilesHandle);
    
    size_t processingFilesContainerLength = INICIAL_BUFFER_LENGTH;
    size_t occupedProcessingFilesContainerUnits = 0;
    IconProcessUnit *processingFilesContainer = malloc(processingFilesContainerLength * sizeof(IconProcessUnit));
    if(!processingFilesContainer) // не NULL
    {
        FatalError("error of allocation heap");
        return STANDARD_ERROR;
    }
    IconProcessContainer iconProcessContainer = {processingFilesContainer, &occupedProcessingFilesContainerUnits};
    PushCleanupStack(cleanupStack, Warp_Free_IconProcessContainer, &iconProcessContainer);

    do
    {
        fileSize.LowPart = fileData.nFileSizeLow;
        fileSize.HighPart = fileData.nFileSizeHigh; // fileSize.QuadPart – размер фала

        //получить абсолютный путь до ярлыка
        wchar_t absoluteFilePath[MAX_PATH];
        StringCchCopyW(absoluteFilePath, MAX_PATH, desktopPath);
        StringCchCatW(absoluteFilePath, MAX_PATH, L"\\");
        StringCchCatW(absoluteFilePath, MAX_PATH, fileData.cFileName);

        wprintf(L"File: %ls\n", fileData.cFileName);

        //создать буффер для хранения информации о ярлыках
        ++occupedProcessingFilesContainerUnits;
        if (occupedProcessingFilesContainerUnits > processingFilesContainerLength)
        {
            IconProcessUnit *temp = realloc(processingFilesContainer, (processingFilesContainerLength + BUFFER_ADDITION) * sizeof(IconProcessUnit)); //выделили новый массив, количество элементов: старое количество + немного ещё
            if (!temp)  // temp == NULL
            {
                FatalError("error of allocation heap");
                return STANDARD_ERROR;
            }
            processingFilesContainerLength += BUFFER_ADDITION;
            processingFilesContainer = temp;
        }
        
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
        ParceFileText(fileContent, fileSize.QuadPart, processingFilesContainer[occupedProcessingFilesContainerUnits - 1].url);

        PartialDeallocation(cleanupStack, 2);
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES)
    {
        FatalError("Unknown error of searching files");
        return STANDARD_ERROR;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);   //для обновления ярлыков
    wprintf(L"\nИтого файлов: %d\n", occupedProcessingFilesContainerUnits);
    
    CompleteDeallocation(cleanupStack);
    return 0;
}


static void ParceFileText(const byte *text, size_t textLength, byte *result)
{
    *result = NULL;
    size_t seek = 0;
    bool inTargetSection = false;
    while (textLength - seek)   //разница =! 0
    {
        if (text[seek] == '[')
        {
            if (inTargetSection) break; //началась новая секция
            ++seek;
            while(true)
            {
                
                ++seek;
            }
        }
    }
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

byte ToLower(byte symbol)
{
    if (symbol >= 65 && symbol <= 90) symbol += 32;
    return symbol;
}

bool CompareSymbols(byte symbol1, byte symbol2)
{
    return ToLower(symbol1) == ToLower(symbol2);
}



static void Warp_FindClose(const void *arg)
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
    IconProcessContainer realTypeArg = *(IconProcessContainer*)arg;
    while (*realTypeArg.occupedUnitsPtr > 0)
    {
        --*realTypeArg.occupedUnitsPtr;
        if (realTypeArg.array->url) free(realTypeArg.array->url);
    }
    free(realTypeArg.array);
}

void Warp_FClose(const void *arg)
{
    FILE **realTypeArg = arg;
    fclose(*realTypeArg);
}