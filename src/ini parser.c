#include <stdbool.h>
#include <windows.h>

#define NEW_LINE_SYMBOL '\n'
#define SPACE_SYMBOL ' '

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
//буфер закончился?
#define BUFFER_FINISHED(ctx) ((ctx)->seek >= (ctx)->textLength)
//сместить указатель на 1
#define BUFFER_INCREASE_PTR(ctx) (++(ctx)->seek)
//взять текущий байт
#define BUFFER_CURRENT_SYMBOL(ctx) ((ctx)->text[(ctx)->seek])
//САМ ОБЪЕКТ!
//не закончился ли буффер
#define BUFFER_NOT_FINISHED_OBJ(ctx) ((ctx).seek < (ctx).textLength)
//буфер закончился?
#define BUFFER_FINISHED_OBJ(ctx) ((ctx).seek >= (ctx).textLength)
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
{   //в винде в файлах перевод на новую строку состоит из комбинации символов: /r/n – порядок именно такой
    size_t steps = SkipByCondition(bfctx, Condition_StopWhen, NEW_LINE_SYMBOL);
    if (BUFFER_FINISHED(bfctx)) return steps;
    BUFFER_INCREASE_PTR(bfctx);
    return ++steps;
}

static bool CompareBufferToSubstring(BufferContext *bfctx, const byte interrupter, const byte *subString, const bool doSkipSpaces)
{
    bool sameSoFar = true;
    size_t substringIndex = 0;
    bool tempRun = true;
    while(tempRun)
    {
        if (BUFFER_FINISHED(bfctx)) return false;
        if
        (
            BUFFER_CURRENT_SYMBOL(bfctx) == interrupter &&
            subString[substringIndex] == '\0'
        )   tempRun = false;
        else
        {
            sameSoFar = CompareSymbols(BUFFER_CURRENT_SYMBOL(bfctx), subString[substringIndex]);
            tempRun = sameSoFar;
            ++substringIndex;
        }
        BUFFER_INCREASE_PTR(bfctx);
        if (doSkipSpaces) SkipByCondition(bfctx, Condition_ContinueWhile, SPACE_SYMBOL);
    }
    return sameSoFar;
}

void ParceFileText(const byte *text, size_t textLength, byte **result)
{
    BufferContext bufferCtx = { text, textLength, 0 };
    bool inTargetSection = false;
    while (BUFFER_NOT_FINISHED_OBJ(bufferCtx))
    {
        if (BUFFER_CURRENT_SYMBOL_OBJ(bufferCtx) == '[')
        {
            if (inTargetSection) return; //началась новая секция
            BUFFER_INCREASE_PTR_OBJ(bufferCtx);
            bool same = CompareBufferToSubstring(&bufferCtx, ']', internetShortcut, false);
            if (same) inTargetSection = true;
            SkipCurrentLine(&bufferCtx);
        }
        if (BUFFER_FINISHED_OBJ(bufferCtx)) return;
        if (inTargetSection)
        {
            bool same = CompareBufferToSubstring(&bufferCtx, '=', url, true);
            if (same)
            {
                SkipByCondition(&bufferCtx, Condition_ContinueWhile, SPACE_SYMBOL);
                size_t urlLength = SkipCurrentLine(&bufferCtx); //urlLength содержит длину строки, в которой есть ссылка и /r/n на конце
                while 
                (
                    bufferCtx.text[bufferCtx.seek] == '\n' ||
                    bufferCtx.text[bufferCtx.seek] == '\r'
                )
                {
                    --bufferCtx.seek;
                    --urlLength;
                }
                bufferCtx.seek -= urlLength;
                if (!urlLength) return;
                *result = malloc(urlLength + 1);    //для \0
                if (*result)
                {
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