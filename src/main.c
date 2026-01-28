#include <windows.h>
#include <shlobj.h> //SHGetKnownFolderPath

#include <stdio.h>
#include <stdbool.h>
#include <strsafe.h>    //StringCchCatW; StringCchCopyW

//для _setmode(_fileno(stdout), _O_U16TEXT);
#include <io.h>
#include <fcntl.h>

int main(void)
{
    _setmode(_fileno(stdout), _O_U16TEXT);  //CRT теперь печатает в консоль только unicode

    PWSTR desktopPath = NULL;
    HRESULT desktopPathResult = NULL;
    desktopPathResult = SHGetKnownFolderPath // взять путь до определённой папки
    (
        &FOLDERID_Desktop, // рабочий стол
        0,
        NULL,
        &desktopPath
    );

    if (FAILED(desktopPathResult))
    {
        CoTaskMemFree(desktopPath);
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
    StringCchCopyW(searchPath, MAX_PATH, desktopPath);
    StringCchCatW(searchPath, MAX_PATH, L"\\*");

    LARGE_INTEGER filesize;
    WIN32_FIND_DATAW fileData;                                              // информация о файле
    HANDLE searchingFilesHandle = FindFirstFileW(searchPath, &fileData);    // получить дескриптор поиска и получить первый файл
    if (INVALID_HANDLE_VALUE == searchingFilesHandle)                       // если дескриптор неверный
    {
        CoTaskMemFree(desktopPath);
        MessageBoxA
        (
            NULL,
            "Invalid files searching descriptor value",
            NULL,
            MB_OK
        );
        return -1;
    }

    do
    {
        if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            wprintf(L"Foldr: %ls\n", fileData.cFileName);
        }
        else
        {
            filesize.LowPart = fileData.nFileSizeLow;
            filesize.HighPart = fileData.nFileSizeHigh;
            wprintf(L"File: %ls\n", fileData.cFileName);
        }
    }
    while (FindNextFileW(searchingFilesHandle, &fileData));

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES) 
    {
        CoTaskMemFree(desktopPath);
        FindClose(searchingFilesHandle);
        MessageBoxA
        (
            NULL,
            "Unknown error of searching files",
            NULL,
            MB_OK
        );
        return -1;
    }

    FindClose(searchingFilesHandle); // закрыть дескриптор поска
    CoTaskMemFree(desktopPath);      // освобождение пемяти из com-кучи из-под строки с абсолютным путём до рабочего стола
    return 0;
}