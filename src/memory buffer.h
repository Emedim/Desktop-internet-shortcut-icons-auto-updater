#ifndef MEMORY_BUFFER 
#define MEMORY_BUFFER 

typedef struct 
{
    unsigned char *content;
    size_t length;
} MemoryBuffer;

bool WriteMemoryBuffer(MemoryBuffer *memoryBuffer, const unsigned char *data, size_t dataLength);
void FlushMemoryBufferToFile(MemoryBuffer *buffer, FILE *file);
void MemoryBufferDestructor(MemoryBuffer *mb);

#endif