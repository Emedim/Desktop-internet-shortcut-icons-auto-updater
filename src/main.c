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
#define MAX_CURRENT_SYSTEM_RESOURCES 3

void FatalError(char *message);
void FindCloseCleanupWrap(void *arg);
void CoTaskMemFreeCleanupWrap(void *arg);

CleanupStack cleanupStack = NULL;   

int main(void)
{
    _setmode(_fileno(stdout), _O_U16TEXT);  //CRT теперь печатает в консоль только unicode

    cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
    if (cleanupStack == NULL) FatalError("initialization error");

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

    if (FAILED(desktopPathResult)) FatalError("Could not find path to desktop");
    
    wprintf(L"Desktop path: %ls\n\n", desktopPath);   //показать путь к рабочему столу
    
    //приведение пути к рабочему столу к виду, пригодному для передачи в FindFirstFileW для поиска файлов на рабочем столе
    wchar_t searchPath[MAX_PATH];
    if
    (
        StringCchCopyW(searchPath, MAX_PATH, desktopPath) != S_OK ||
        StringCchCatW(searchPath, MAX_PATH, L"\\*.url") != S_OK
    )
    FatalError("Error of pathes");

    LARGE_INTEGER filesize;
    WIN32_FIND_DATAW fileData;                                                  // информация о файле
    HANDLE searchingFilesHandle = FindFirstFileW(searchPath, &fileData);        // получить дескриптор поиска и получить первый файл
    if (INVALID_HANDLE_VALUE == searchingFilesHandle) FatalError("Invalid files searching descriptor value");
    PushCleanupStack(cleanupStack, FindCloseCleanupWrap, &searchingFilesHandle);

    FILE *debuging = _wfopen(L"C:\\Users\\emedi\\Documents\\Проекты по программированию\\Complex projects\\Desktop icons auto updater\\debug.txt", L"wb");
    do
    {
        filesize.LowPart = fileData.nFileSizeLow;
        filesize.HighPart = fileData.nFileSizeHigh;
        if (filesize.QuadPart <= 0) continue;

        //получить абсолютный путь до ярлыка
        wchar_t absoluteFilePath[MAX_PATH];
        StringCchCopyW(absoluteFilePath, MAX_PATH, desktopPath);
        StringCchCatW(absoluteFilePath, MAX_PATH, L"\\");
        StringCchCatW(absoluteFilePath, MAX_PATH, fileData.cFileName);

        FILE *file = _wfopen(absoluteFilePath, L"rb");
        char *buffer = malloc(filesize.QuadPart);

        fread(buffer, 1, filesize.QuadPart, file); // Чтение содержимого
        fwrite(buffer, 1, filesize.QuadPart, debuging);
        wprintf(L"File: %ls\n", fileData.cFileName);

        free(buffer);
        fclose(file);
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));
    fclose(debuging);

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES) FatalError("Unknown error of searching files");
    
    CompleteDeallocation(cleanupStack);
    return 0;
}

void FatalError(char *message)
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