#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW; StringCchCopyW

//для _setmode(_fileno(stdout), _O_U16TEXT);
#include <io.h>
#include <fcntl.h>

void FindCloseCleanupWrap(void *arg)
{
    HANDLE *realArg = arg;
    FindClose(*realArg);
}

void CoTaskMemFreeCleanupWrap(void *arg)
{
    PWSTR *realArg = arg;
    CoTaskMemFree(*realArg);
}

typedef struct
{
    void (*func)(void *);    //указатель на функцию, принемающую аргумент – указатель на void func(void *arg);
    void *arg;
} DeallocatingUnit;

typedef struct
{
    DeallocatingUnit deallocatingUnits[4];
    size_t size;
} *CleanupStack;

CleanupStack CleanupStackInit()
{
    CleanupStack tempPtr = malloc(sizeof(*tempPtr));
    if (tempPtr) tempPtr->size = 0;     //если malloc вернул не NULL
    return tempPtr;
}

void CleanupPush(CleanupStack cs, void (*func)(void *), void *arg)
{
    cs->deallocatingUnits[cs->size++] = (DeallocatingUnit){ func, arg };
}
void CleanupExecute(CleanupStack cs)
{
    while (cs->size > 0)
    {
        --cs->size;
        cs->deallocatingUnits[cs->size].func(cs->deallocatingUnits[cs->size].arg);
    }
    free(cs);
}

int main(void)
{
    _setmode(_fileno(stdout), _O_U16TEXT);  //CRT теперь печатает в консоль только unicode

    CleanupStack cleanupStack = CleanupStackInit();
    if (cleanupStack == NULL)
    {
        MessageBoxA
        (
            NULL,
            "initialization error",
            NULL,
            MB_OK
        );
        return -1;
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
    CleanupPush(cleanupStack, CoTaskMemFreeCleanupWrap, &desktopPath);

    if (FAILED(desktopPathResult))
    {
        CleanupExecute(cleanupStack);
        MessageBoxA
        (
            NULL,
            "Could not find path to desktop",
            NULL,
            MB_OK
        );
        return -1;
    }
    wprintf(L"Desktop path: %ls\n\n", desktopPath);   //показать путь к рабочему 

    //приведение пути к рабочему столу к виду, пригодному для передачи в FindFirstFileW для поиска файлов на рабочем столе
    wchar_t searchPath[MAX_PATH];
    if
    (
        StringCchCopyW(searchPath, MAX_PATH, desktopPath) != S_OK ||
        StringCchCatW(searchPath, MAX_PATH, L"\\*.url") != S_OK
    )
    {
        CleanupExecute(cleanupStack);
        MessageBoxA
        (
            NULL,
            "Error of pathes",
            NULL,
            MB_OK
        );
        return -1;
    }

    LARGE_INTEGER filesize;
    WIN32_FIND_DATAW fileData;                                                  // информация о файле
    HANDLE searchingFilesHandle = FindFirstFileW(searchPath, &fileData);        // получить дескриптор поиска и получить первый файл
    if (INVALID_HANDLE_VALUE == searchingFilesHandle)                           // если дескриптор неверный
    {
        CleanupExecute(cleanupStack);
        MessageBoxA
        (
            NULL,
            "Invalid files searching descriptor value",
            NULL,
            MB_OK
        );
        return -1;
    }
    CleanupPush(cleanupStack, FindCloseCleanupWrap, &searchingFilesHandle);

    FILE *file = NULL;
    do
    {
        filesize.LowPart = fileData.nFileSizeLow;
        filesize.HighPart = fileData.nFileSizeHigh;

        file = _wfopen(fileData.cFileName, "r");
        char *buffer = malloc(filesize.QuadPart + 1);   //+1 для NULL-терминатора
        if(buffer == NULL || file == NULL) continue;

        fread(buffer, 1, filesize.QuadPart, file); // Чтение содержимого
        buffer[filesize.QuadPart] = '\0';
        wprintf(L"File: %ls – \n", fileData.cFileName);

        free(buffer);
        fclose(file);
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES) 
    {
        CleanupExecute(cleanupStack);
        MessageBoxA
        (
            NULL,
            "Unknown error of searching files",
            NULL,
            MB_OK
        );
        return -1;
    }
    
    CleanupExecute(cleanupStack);
    return 0;
}