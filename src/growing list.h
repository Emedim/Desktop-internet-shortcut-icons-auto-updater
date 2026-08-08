#ifndef GROWING_LIST
#define GROWING_LIST

typedef struct
{
    void **data;
    size_t length, capacity;
    void (*UnitDestroyFunc)(void *);
} GrowingList;

bool InitGrowingList(GrowingList *gl, void (*UnitDestroyFunc)(void *));
bool PushGrowingList(GrowingList *gl, void *unit);
void *GetGrowingList(const GrowingList *gl, size_t index);
void DestroyGrowingList(GrowingList *gl);

void *SearchGrowingList(GrowingList *gl, bool (*callback)(void *, void *), void *userData);

#endif