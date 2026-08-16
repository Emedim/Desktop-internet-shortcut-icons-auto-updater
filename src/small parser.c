#include <stdbool.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#include "growing list.h"
#include "memory buffer.h"
#include "small parser.h"

static bool ParseIniText(const unsigned char *text, const size_t textLength, IniFileInfo *iniFI);

IniFileInfo *NewIniFileInfo()
{
    IniFileInfo *new = malloc(sizeof(IniFileInfo));
    if (new) *new = (IniFileInfo){ 0 };
    return new;
}

IniSection *NewIniSection()
{
    IniSection *new = malloc(sizeof(IniSection));
    if (new) *new = (IniSection){ 0 };
    return new;
}

IniPair *NewIniPair()
{
    IniPair *new = malloc(sizeof(IniPair));
    if (new) *new = (IniPair){ 0 };
    return new;
}

void DestroyIniFileInfo(IniFileInfo *iniFI)
{
    if (iniFI->stream) fclose(iniFI->stream);
    DestroyGrowingList(&iniFI->sections);
    free(iniFI);
}

void DestroyIniSectionU(void *arg)
{
    IniSection *section = arg;
    MemoryBufferDestructor(&section->name);
    DestroyGrowingList(&section->iniPairs);
    free(section);
}

void DestroyIniPairU(void *arg)
{
    IniPair *pair = arg;
    MemoryBufferDestructor(&pair->key);
    MemoryBufferDestructor(&pair->value);
    free(pair);
}

IniFileInfo *InitIniInfo(const wchar_t *filePath)
{
    IniFileInfo *iniFI = NewIniFileInfo();
    if (iniFI) 
    {
        if (iniFI->stream = _wfopen(filePath, L"rb"))
        {
            if (InitGrowingList(&iniFI->sections, DestroyIniSectionU))
            {
                WIN32_FILE_ATTRIBUTE_DATA iniFileData;
                if (GetFileAttributesExW(filePath, GetFileExInfoStandard, &iniFileData))
                {
                    ULARGE_INTEGER iniFileSize = { .LowPart = iniFileData.nFileSizeLow, .HighPart = iniFileData.nFileSizeHigh };
                    unsigned char *text = malloc(iniFileSize.QuadPart);
                    if (text)
                    {
                        fread(text, 1, iniFileSize.QuadPart, iniFI->stream);
                        bool parseResult = ParseIniText(text, iniFileSize.QuadPart, iniFI);
                        free(text);
                        if (parseResult) return iniFI;
                    }
                }
            }
        }
        DestroyIniFileInfo(iniFI);
    }
    return NULL;
}


static unsigned char ToLower(unsigned char symbol)
{
    if (symbol >= 65 && symbol <= 90) symbol += 32;
    return symbol;
}

static bool CompareSymbols(unsigned char symbol1, unsigned char symbol2)
{
    return ToLower(symbol1) == ToLower(symbol2);
}

const unsigned char internetShortcutText[] = "InternetShortcut";
const unsigned char urlText[] = "URL";

static bool CompareMBtoString(const MemoryBuffer *mb, const unsigned char *str)
{
    bool sameSoFar = true;
    size_t index = 0;
    bool tempRun = true;
    while(tempRun)
    {
        if
        (
            index == mb->length ||
            str[index] == '\0'
        )   tempRun = false;
        else
        {
            tempRun = sameSoFar = CompareSymbols(mb->content[index], str[index]);
            ++index;
        }
    }
    return sameSoFar;
}

bool CheckSectionCallback(void *section, void *desiredSection)
{
    if(
        CompareMBtoString(
            &((IniSection *)section)->name, 
            (const unsigned char *)desiredSection
        )
    ) return true;
    return false;
}

bool CheckPairKeyCallback(void *pair, void *desiredKey)
{
    if(
        CompareMBtoString(
            &((IniPair *)pair)->key, 
            (const unsigned char *)desiredKey
        )
    ) return true;
    return false;
}

IniSection *GetIniSectionByName(const IniFileInfo *iniFI, const unsigned char* desiredSection)
{
    return (IniSection *)SearchGrowingList((void *)&iniFI->sections, CheckSectionCallback, (void *)desiredSection);
}

IniPair *GetIniPairByKey(const IniSection *section, const unsigned char *desiredKey)
{
    return (IniPair *)SearchGrowingList((void *)&section->iniPairs, CheckPairKeyCallback, (void *)desiredKey);
}

unsigned char *GetIniUrl(const IniFileInfo *iniFI)
{
    IniSection *section = GetIniSectionByName(iniFI, internetShortcutText);
    if (section)
    {
        IniPair *pair = GetIniPairByKey(section, urlText);
        if (pair)
        {
            unsigned char *url = malloc(sizeof(char) * (pair->value.length + 1));
            if (url)
            {
                memcpy(url, pair->value.content, pair->value.length);
                url[pair->value.length] = '\0';
            }
            return url;
        }
    }
    return NULL;
}



typedef struct
{
    const byte *text;
    const size_t textLength;
    size_t seek;
} BufferContext;

#define CR '\r'
#define LF '\n'
#define SPACE ' '
#define INI_PAIR_DIVIDER '='
#define INI_SECTION_START '['
#define INI_SECTION_END ']'

static inline bool BfctxUnlessFinished(const BufferContext *bfctx)
{ return bfctx->seek < bfctx->textLength; }
static inline bool BfctxIfFinished(const BufferContext *bfctx)
{ return bfctx->seek >= bfctx->textLength; }
static inline void BfctxIncreaseSeek(BufferContext *bfctx)
{ ++bfctx->seek; }
static inline void BfctxReduceSeek(BufferContext *bfctx)
{ --bfctx->seek; }
static inline unsigned char BfctxGetCurrentSymbol(const BufferContext *bfctx)
{ return bfctx->text[bfctx->seek]; }
static inline unsigned char BfctxGet(const BufferContext *bfctx, const size_t index)
{ return bfctx->text[index]; }
static inline size_t BfctxGetSeek(const BufferContext *bfctx)
{ return bfctx->seek; }

static bool Condition_StopWhen(const BufferContext *bfctx, const byte symbol)
{   return BfctxGetCurrentSymbol(bfctx) != symbol; }

static bool Condition_ContinueWhile(const BufferContext *bfctx, const byte symbol)
{   return BfctxGetCurrentSymbol(bfctx) == symbol; }

static size_t SkipByCondition(const BufferContext *bfctx, bool (*condition)(const BufferContext *, const byte), const byte symbol, bool back)
{
    size_t steps = 0;
    while
    (
        BfctxUnlessFinished(bfctx) &&   //если буфер закончился, цикл продолжать нельзя
        condition(bfctx, symbol)
    )
    {
        if (back) BfctxReduceSeek(bfctx);
        else BfctxIncreaseSeek(bfctx);
        ++steps;
    }
    return steps;
}

static size_t SkipCurrentLine(const BufferContext *bfctx)
{   //в винде в файлах перевод на новую строку состоит из комбинации символов: /r/n – порядок именно такой
    size_t steps = SkipByCondition(bfctx, Condition_StopWhen, LF, false);
    if (BfctxIfFinished(bfctx)) return steps;
    BfctxIncreaseSeek(bfctx);
    return ++steps;
}

size_t GetLengthWithNoStartSpaces(const BufferContext *bfctx, size_t *length, const char interruptionSymbol)
{
    SkipByCondition(bfctx, Condition_ContinueWhile, SPACE, false);
    if (BfctxIfFinished(bfctx) || BfctxGetCurrentSymbol(bfctx) == CR) return 0; //startIndex изначально не может быть нулём, так как самый первый символ в файле всегда "[" иначе ошибка
    size_t startIndex = BfctxGetSeek(bfctx);
    *length = SkipByCondition(bfctx, Condition_StopWhen, interruptionSymbol, false);
    return startIndex;
}

void RemoveEndingSpaces(const BufferContext *bfctx, size_t *length)
{
    BfctxReduceSeek(bfctx);
    *length -= SkipByCondition(bfctx, Condition_ContinueWhile, SPACE, true);
}

static const unsigned char *GetTrimmedStrUntilSymb(const BufferContext *bfctx, size_t *length, const char interruptionSymbol)
{
    size_t startIndex = GetLengthWithNoStartSpaces(bfctx, length, interruptionSymbol);
    if (!startIndex) return NULL;
    if (BfctxIfFinished(bfctx) || BfctxGetCurrentSymbol(bfctx) == CR || *length == 0) return NULL;
    RemoveEndingSpaces(bfctx, length);
    return bfctx->text + startIndex;
}

static const unsigned char *GetTrimmedValueText(const BufferContext *bfctx, size_t *length)
{
    size_t startIndex = GetLengthWithNoStartSpaces(bfctx, length, CR);
    if (!startIndex) return NULL;
    RemoveEndingSpaces(bfctx, length);
    return bfctx->text + startIndex;
}

bool ParsePairLine(const BufferContext *bfctx, IniPair **pair)
{
    size_t lengthRe;
    const unsigned char *keyText = GetTrimmedStrUntilSymb(bfctx, &lengthRe, INI_PAIR_DIVIDER);
    if (!keyText)
    {
        if (BfctxGetCurrentSymbol(bfctx) == CR)
            SkipCurrentLine(bfctx);
        *pair = NULL;
        return true;
    }

    *pair = NewIniPair();
    if (!*pair) return false;
    if (!WriteMemoryBuffer(&(*pair)->key, keyText, lengthRe))
    {
        DestroyIniPairU((void *)(*pair));
        return false;
    }
    SkipByCondition(bfctx , Condition_StopWhen, INI_PAIR_DIVIDER, false);
    BfctxIncreaseSeek(bfctx);
    const unsigned char *valueText = GetTrimmedValueText(bfctx, &lengthRe); //предусмотреть что после (=) может быть пустая строка с пробелами или окончание файла
    if (valueText)
    {
        if (!WriteMemoryBuffer(&(*pair)->value, valueText, lengthRe))
        {
            DestroyIniPairU((void *)*pair);
            return false;
        }
    }
    SkipCurrentLine(bfctx);
    return true;
}

static bool ParseIniText(const unsigned char *text, const size_t textLength, IniFileInfo *iniFI)
{
    BufferContext bufferContext = { text, textLength, 0 };
    IniSection *currentSection = NULL;
    while (BfctxUnlessFinished(&bufferContext))
    {
        if (BfctxGetCurrentSymbol(&bufferContext) == INI_SECTION_START)
        {
            BfctxIncreaseSeek(&bufferContext);
            size_t sectionSize;
            const unsigned char *trimmedSection = GetTrimmedStrUntilSymb(&bufferContext, &sectionSize, INI_SECTION_END);
            if (trimmedSection)
            {
                IniSection *newSection = NewIniSection();
                if (newSection)
                {
                    if (WriteMemoryBuffer(&newSection->name, trimmedSection, sectionSize))
                    {
                        if (InitGrowingList(&newSection->iniPairs, DestroyIniPairU))
                        {
                            if (PushGrowingList(&iniFI->sections, newSection))
                            {
                                currentSection = newSection;
                                SkipCurrentLine(&bufferContext);
                                continue;
                            }
                        }
                    }
                    DestroyIniSectionU((void *)newSection);
                }
            }
            return false;
        }
        else
        {
            if (!currentSection) return false;
            IniPair *newPair;
            if (!ParsePairLine(&bufferContext, &newPair)) return false;
            if (newPair)
            {
                if (!PushGrowingList(&currentSection->iniPairs, newPair))
                {
                    DestroyIniPairU((void *)newPair);
                    return false;
                }
            }
            continue;
        }
    }
    return true;
}