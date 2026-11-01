#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int RickyWebpBegin(void);
void RickyWebpEnd(void);
void* RickyWebpMalloc(size_t size);
void* RickyWebpCalloc(size_t count, size_t size);
void RickyWebpFree(void* pointer);
size_t RickyWebpLive(void);
size_t RickyWebpPeak(void);
int RickyWebpAllocationFailed(void);
#ifdef __cplusplus
}
#endif
