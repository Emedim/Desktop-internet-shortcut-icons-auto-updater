#include <stdlib.h>     //malloc(), free(), realloc()
#include <stdbool.h>    //bool true false
#include <stddef.h>
#include <stdint.h>
#include "growing list.h"

#define INITIAL_BUFFER_LENGTH (8)
#define BUFFER_ADDITION (12)

bool InitGrowingList(GrowingList *gl, void (*UnitDestroyFunc)(void *))
{
    gl->length = 0;
    gl->data = malloc(INITIAL_BUFFER_LENGTH * sizeof(void *));
    if (!gl->data) return false;
    gl->capacity = INITIAL_BUFFER_LENGTH;
    gl->UnitDestroyFunc = UnitDestroyFunc;
    return true;
}

bool PushGrowingList(GrowingList *gl, void *unit)
{
    if (gl->length >= gl->capacity)
    {
        void **temp = realloc(gl->data, (gl->capacity + BUFFER_ADDITION) * sizeof(void *));
        if (!temp) return false;
        gl->data = temp;
        gl->capacity += BUFFER_ADDITION;
    }
    gl->data[gl->length++] = unit;
    return true;
}

void *GetGrowingList(const GrowingList *gl, size_t index)
{
    return gl->data[index];
}

void DestroyGrowingList(GrowingList *gl)
{
    if (gl->UnitDestroyFunc) 
    {
        while(gl->length)
        {
            --gl->length;
            gl->UnitDestroyFunc(GetGrowingList(gl, gl->length));
        }
    }
    free(gl->data);
}

void *SearchGrowingList(const GrowingList *gl, bool (*callbackChecker)(void *, void *), void *userData)
{
    for (size_t index = 0; index < gl->length; ++index)
    {
        void *objPtr = GetGrowingList(gl, index);
        if (callbackChecker(objPtr, userData)) return objPtr;
    }
    return NULL;
}


#include <windows.h>
typedef struct
{
    wchar_t path[MAX_PATH];
    wchar_t name[MAX_PATH];
    bool isBound;
} IconFileInfo;

void DestroyGrowingListIcon(GrowingList *gl)
{
    if (gl->UnitDestroyFunc) 
    {
        while(gl->length)
        {
            --gl->length;
            IconFileInfo *ifi = GetGrowingList(gl, gl->length);
            gl->UnitDestroyFunc(ifi);
        }
    }
    free(gl->data);
}
