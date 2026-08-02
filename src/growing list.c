#include <stdlib.h>     //malloc(), free(), realloc()
#include <stdbool.h>    //bool true false
#include <stddef.h>
#include <stdint.h>
#include "growing list.h"

#define INITIAL_BUFFER_LENGTH (8)
#define BUFFER_ADDITION (12)

bool InitGrowingList(GrowingList *gl, void (*UnitDestroyFunc)(const void *))
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

void *PopGrowingList(GrowingList *gl)
{
    return GetGrowingList(gl, --gl->length);
}

void DestroyGrowingList(GrowingList *gl)
{
    if (gl->UnitDestroyFunc) 
        while(gl->length)
            gl->UnitDestroyFunc(PopGrowingList(gl));
    free(gl->data);
}