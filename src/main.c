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
#define MAX_CURRENT_SYSTEM_RESOURCES 3
#define DEBUG_ICO_PATH L"C:/Users/emedi/Documents/Проекты по программированию/Complex projects/Desktop icons auto updater/resources/test_ico/ico.ico"

void FatalError(char *message);
void FindCloseCleanupWarp(void *arg);
void CoTaskMemFreeCleanupWarp(void *arg);

CleanupStack cleanupStack = NULL;

int main(void)
{
    _setmode(_fileno(stdout), _O_U16TEXT);  //CRT теперь печатает в консоль только unicode

    cleanupStack = InitCleanupStack(MAX_CURRENT_SYSTEM_RESOURCES);
    if (cleanupStack == NULL)
    {
        FatalError("initialization error");
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
    PushCleanupStack(cleanupStack, CoTaskMemFreeCleanupWarp, &desktopPath);

    if (FAILED(desktopPathResult)) 
    {
        FatalError("Could not find path to desktop");
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
        FatalError("Error of pathes");
        return -1;
    }

    LARGE_INTEGER filesize;
    WIN32_FIND_DATAW fileData;                                                  // информация о файле
    HANDLE searchingFilesHandle = FindFirstFileW(searchPath, &fileData);        // получить дескриптор поиска и получить первый файл
    if (INVALID_HANDLE_VALUE == searchingFilesHandle)
    {
        FatalError("Invalid files searching descriptor value");
        return -1;
    }
    PushCleanupStack(cleanupStack, FindCloseCleanupWarp, &searchingFilesHandle);
    
    do
    {
        filesize.LowPart = fileData.nFileSizeLow;
        filesize.HighPart = fileData.nFileSizeHigh;

        //получить абсолютный путь до ярлыка
        wchar_t absoluteFilePath[MAX_PATH];
        StringCchCopyW(absoluteFilePath, MAX_PATH, desktopPath);
        StringCchCatW(absoluteFilePath, MAX_PATH, L"\\");
        StringCchCatW(absoluteFilePath, MAX_PATH, fileData.cFileName);

        wprintf(L"File: %ls\n", fileData.cFileName);

        if (!WritePrivateProfileStringW(
                L"InternetShortcut",
                L"IconFile",
                DEBUG_ICO_PATH,
                absoluteFilePath
        ))
        {
            FatalError("error of setting ico path for url");
            return -1;
        } 

        if(!WritePrivateProfileStringW(
                L"InternetShortcut",
                L"IconIndex",
                L"0",
                absoluteFilePath
        ))
        {
            FatalError("error of setting ico index for url");
            return -1;
        } 
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES)
    {
        FatalError("Unknown error of searching files");
        return -1;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);   //для обновления ярлыков
    wprintf("\n");
    
    CompleteDeallocation(cleanupStack);
    return 0;
}

void FatalError(const char *message)
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

void FindCloseCleanupWarp(const void *arg)
{
    HANDLE *realTypeArg = arg;
    FindClose(*realTypeArg);
}

void CoTaskMemFreeCleanupWarp(const void *arg)
{
    PWSTR *realTypeArg = arg;
    CoTaskMemFree(*realTypeArg);
}