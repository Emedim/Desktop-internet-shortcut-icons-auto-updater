#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW; StringCchCopyW

//для _setmode(_fileno(stdout), _O_U16TEXT);
#include <io.h>
#include <fcntl.h>

#include "cleanup interface.h"
#define MAX_CURRENT_SYSTEM_RESOURCES 6

void FindCloseCleanupWrap(void *arg);
void CoTaskMemFreeCleanupWrap(void *arg);

int main(void)
{
    _setmode(_fileno(stdout), _O_U16TEXT);  //CRT теперь печатает в консоль только unicode

    CleanupStack cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
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
    PushCleanupStack(cleanupStack, CoTaskMemFreeCleanupWrap, &desktopPath);

    if (FAILED(desktopPathResult))
    {
        CompleteDeallocation(cleanupStack);
        MessageBoxA
        (
            NULL,
            "Could not find path to desktop",
            NULL,
            MB_OK
        );
        return -1;
    }
    wprintf(L"Desktop path: %ls\n\n", desktopPath);   //показать путь к рабочему столу
    
    //приведение пути к рабочему столу к виду, пригодному для передачи в FindFirstFileW для поиска файлов на рабочем столе
    wchar_t searchPath[MAX_PATH];
    if
    (
        StringCchCopyW(searchPath, MAX_PATH, desktopPath) != S_OK ||
        StringCchCatW(searchPath, MAX_PATH, L"\\*.url") != S_OK
    )
    {
        CompleteDeallocation(cleanupStack);
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
        CompleteDeallocation(cleanupStack);
        MessageBoxA
        (
            NULL,
            "Invalid files searching descriptor value",
            NULL,
            MB_OK
        );
        return -1;
    }
    PushCleanupStack(cleanupStack, FindCloseCleanupWrap, &searchingFilesHandle);

    FILE *debuging = _wfopen(L"C:\\Users\\emedi\\Documents\\Проекты по программированию\\Complex projects\\Desktop icons auto updater\\debug.txt", L"w");
    do
    {
        filesize.LowPart = fileData.nFileSizeLow;
        filesize.HighPart = fileData.nFileSizeHigh;

        //получить абсолютный путь до ярлыка
        wchar_t absoluteFilePath[MAX_PATH];
        StringCchCopyW(absoluteFilePath, MAX_PATH, desktopPath);
        StringCchCatW(absoluteFilePath, MAX_PATH, L"\\");
        StringCchCatW(absoluteFilePath, MAX_PATH, fileData.cFileName);

        FILE *file = _wfopen(absoluteFilePath, L"r");
        char *buffer = malloc(filesize.QuadPart + 1);   //+1 для NULL-терминатора

        fread(buffer, 1, filesize.QuadPart, file); // Чтение содержимого
        buffer[filesize.QuadPart] = '\0';
        fwrite(buffer, 1, filesize.QuadPart, debuging);
        wprintf(L"File: %ls\n", fileData.cFileName);

        free(buffer);
        fclose(file);
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));
    fclose(debuging);

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES) 
    {
        CompleteDeallocation(cleanupStack);
        MessageBoxA
        (
            NULL,
            "Unknown error of searching files",
            NULL,
            MB_OK
        );
        return -1;
    }
    
    CompleteDeallocation(cleanupStack);
    return 0;
}

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