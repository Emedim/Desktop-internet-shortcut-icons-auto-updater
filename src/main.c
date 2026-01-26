#include <windows.h>
#include <shlobj.h>     //SHGetKnownFolderPath
#include <stdio.h>

int main(void)
{
    PWSTR desktopPath = NULL;
    HRESULT desktopPathResult = SHGetKnownFolderPath
    (
        &FOLDERID_Desktop,
        0,
        NULL,
        &desktopPath
    );

    if (FAILED(desktopPathResult))
    {
        printf("SHGetKnownFolderPath failed, HRESULT = 0x%08X\n", (unsigned)desktopPathResult);
        return 1;
    }
    wprintf(L"Desktop path: %ls\n", desktopPath);
    CoTaskMemFree(desktopPath);     //освобождение пемяти из com-кучи

    return 0;
}