#ifndef GROWING_LIST
#define GROWING_LIST

typedef struct
{
    void **data;
    size_t length, capacity;
    void (*UnitDestroyFunc)(const void *);
} GrowingList;

bool InitGrowingList(GrowingList *gl, void (*UnitDestroyFunc)(const void *));
bool PushGrowingList(GrowingList *gl, void *unit);
void DestroyGrowingList(GrowingList *gl);
void *GetGrowingList(const GrowingList *gl, size_t index);

#endif