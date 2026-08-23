#ifndef CLEANUP_STACK
#define CLEANUP_STACK

#include <stdint.h>

typedef struct tag_cleanupStack* CleanupStack;
CleanupStack InitCleanupStack(uint8_t totalResourcesQuantity);
void PushCleanupStack(CleanupStack cs, void (*func)(void *), void *arg);
void SingleDeallocation(CleanupStack cs);
void PartialDeallocation(CleanupStack cs, uint8_t iterations);
void CompleteDeallocation(CleanupStack cs);

#endif