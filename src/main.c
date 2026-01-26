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
        MessageBoxA
        (
            NULL,
            "Could not find path to desktop",
            NULL,
            MB_OK
        );
        return 1;
    }
    wprintf(L"Desktop path: %ls\n", desktopPath);

    CoTaskMemFree(desktopPath);     //освобождение пемяти из com-кучи из-под строки с абсолютным путём до рабочего стола
    return 0;
}