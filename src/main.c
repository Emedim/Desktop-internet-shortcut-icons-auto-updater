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
#define NEW_LINE_SYMBOL '\n'
#define SPACE_SYMBOL ' '

static void FatalError(const byte *message);
static void ParceFileText(const byte *text, size_t textLength, byte *result);

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
        //создать буффер для хранения информации о ярлыках
        ++occupedProcessingFilesContainerUnits;
        if (occupedProcessingFilesContainerUnits > processingFilesContainerLength)
        {
            processingFilesContainerLength += BUFFER_ADDITION;
            IconProcessUnit *temp = realloc(processingFilesContainer, processingFilesContainerLength * sizeof(IconProcessUnit)); //выделили новый массив, количество элементов: старое количество + немного ещё
            if (!temp)  // temp == NULL
            {
                --occupedProcessingFilesContainerUnits;
                FatalError("error of allocation heap");
                return STANDARD_ERROR;
            }
            processingFilesContainer = temp;
        }
        processingFilesContainer[occupedProcessingFilesContainerUnits - 1].url = NULL;

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
        ParceFileText(fileContent, fileSize.QuadPart, &(processingFilesContainer[occupedProcessingFilesContainerUnits - 1].url));

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
    wprintf(L"\nИтого файлов: %d\n\n", occupedProcessingFilesContainerUnits);
    
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



byte ToLower(byte symbol)
{
    if (symbol >= 65 && symbol <= 90) symbol += 32;
    return symbol;
}

bool CompareSymbols(byte symbol1, byte symbol2)
{
    return ToLower(symbol1) == ToLower(symbol2);
}

typedef struct tag_BufferContext
{
    const byte *text;
    const size_t textLength;
    size_t seek;
} BufferContext;

//не закончился ли буффер
#define BUFFER_NOT_FINISHED(ctx) ((ctx)->seek < (ctx)->textLength)
//сместить указатель на 1
#define BUFFER_INCREASE_PTR(ctx) (++(ctx)->seek)
//взять текущий байт
#define BUFFER_CURRENT_SYMBOL(ctx) ((ctx)->text[(ctx)->seek])
//САМ ОБЪЕКТ!
//не закончился ли буффер
#define BUFFER_NOT_FINISHED_OBJ(ctx) ((ctx).seek < (ctx).textLength)
//сместить указатель на 1
#define BUFFER_INCREASE_PTR_OBJ(ctx) (++(ctx).seek)
//взять текущий байт
#define BUFFER_CURRENT_SYMBOL_OBJ(ctx) ((ctx).text[(ctx).seek])

const byte internetShortcut[] = "internetshortcut";
const byte url[] = "url";

static bool Condition_StopWhen(const BufferContext *bfctx, const byte symbol)
{   return BUFFER_CURRENT_SYMBOL(bfctx) != symbol; }

static bool Condition_ContinueWhile(const BufferContext *bfctx, const byte symbol)
{   return BUFFER_CURRENT_SYMBOL(bfctx) == symbol; }

static size_t SkipByCondition(BufferContext *bfctx, bool (*condition)(const BufferContext *, const byte), const byte symbol)
{
    size_t steps = 0;
    while
    (
        BUFFER_NOT_FINISHED(bfctx) &&   //если буфер закончился, цикл продолжать нельзя
        condition(bfctx, symbol)
    )
    {
        BUFFER_INCREASE_PTR(bfctx);
        ++steps;
    }
    return steps;
}

static size_t SkipCurrentLine(BufferContext *bfctx)
{
    size_t steps = SkipByCondition(bfctx, Condition_StopWhen, NEW_LINE_SYMBOL);
    BUFFER_INCREASE_PTR(bfctx);
    return ++steps;
}

static bool CompareTexts(BufferContext *bfctx, const byte interrupter, const byte *subString, const bool doSkipSpaces)
{
    bool sameSoFar = true;
    size_t tempIndex = 0;
    bool tempRun = true;
    while(tempRun)
    {
        if
        (
            BUFFER_CURRENT_SYMBOL(bfctx) == interrupter &&
            subString[tempIndex] == '\0'
        )   tempRun = false;
        else
        {
            sameSoFar = CompareSymbols(BUFFER_CURRENT_SYMBOL(bfctx), subString[tempIndex]);
            tempRun = sameSoFar;
            ++tempIndex;
        }
        BUFFER_INCREASE_PTR(bfctx);
        if (doSkipSpaces) SkipByCondition(bfctx, Condition_ContinueWhile, SPACE_SYMBOL);
    }
    return sameSoFar;
}

static void ParceFileText(const byte *text, size_t textLength, byte **result)
{
    BufferContext bufferCtx = { text, textLength, 0 };
    bool inTargetSection = false;
    while (BUFFER_NOT_FINISHED_OBJ(bufferCtx))   //разница =! 0
    {
        if (BUFFER_CURRENT_SYMBOL_OBJ(bufferCtx) == '[')
        {
            if (inTargetSection) return; //началась новая секция
            BUFFER_INCREASE_PTR_OBJ(bufferCtx);
            bool same = CompareTexts(&bufferCtx, ']', internetShortcut, false);
            if (same) inTargetSection = true;
            SkipCurrentLine(&bufferCtx);
        }
        if (inTargetSection)
        {
            bool same = CompareTexts(&bufferCtx, '=', url, true);
            if (same)
            {
                SkipByCondition(&bufferCtx, Condition_ContinueWhile, SPACE_SYMBOL);
                size_t urlLength = SkipCurrentLine(&bufferCtx);
                bufferCtx.seek -= urlLength;
                --urlLength;    //размер массива с \0 на конце
                *result = malloc(urlLength);
                if (*result)
                {
                    --urlLength;    //размер массива без \0 на конце
                    size_t tempIndex = 0;
                    while(tempIndex < urlLength)
                    {
                        (*result)[tempIndex++] = BUFFER_CURRENT_SYMBOL_OBJ(bufferCtx);
                        BUFFER_INCREASE_PTR_OBJ(bufferCtx);
                    }
                    (*result)[urlLength] = '\0';
                }
                return;
            }
        }
        SkipCurrentLine(&bufferCtx);
    }
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
    IconProcessContainer realTypeArg = *(IconProcessContainer*)arg;
    while (*realTypeArg.occupedUnitsPtr > 0)
    {
        --*realTypeArg.occupedUnitsPtr;
        if (realTypeArg.array[*realTypeArg.occupedUnitsPtr].url) free(realTypeArg.array[*realTypeArg.occupedUnitsPtr].url);
    }
    free(realTypeArg.array);
}

void Warp_FClose(const void *arg)
{
    FILE **realTypeArg = arg;
    fclose(*realTypeArg);
}