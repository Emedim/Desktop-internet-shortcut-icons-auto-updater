#ifndef MEMORY_BUFFER 
#define MEMORY_BUFFER 

typedef struct 
{
    char *content;
    size_t length;
} MemoryBuffer;

bool WriteMemoryBuffer(MemoryBuffer *memoryBuffer, const char *data, size_t dataLength);
void FlushMemoryBufferToFile(MemoryBuffer *buffer, FILE *file);
void MemoryBufferDestructor(MemoryBuffer *mb);

#endif