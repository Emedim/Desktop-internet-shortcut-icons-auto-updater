#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "memory buffer.h"

bool WriteMemoryBuffer(MemoryBuffer *memoryBuffer, const char *data, size_t dataLength)
{
    char *temp = realloc(memoryBuffer->content, memoryBuffer->length + dataLength);
    if (!temp) return false;
    memoryBuffer->content = temp;
    memcpy(memoryBuffer->content + memoryBuffer->length, data, dataLength);
    memoryBuffer->length += dataLength;
    return true;
}

void FlushMemoryBufferToFile(MemoryBuffer *buffer, FILE *file)
{
    fwrite(buffer->content, 1, buffer->length, file);
}

void MemoryBufferDestructor(MemoryBuffer *mb)
{
    free(mb->content);
    mb->length = 0;
}