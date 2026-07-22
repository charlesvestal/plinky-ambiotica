/* Prototypes for the instrumented fixed-arena allocator that the vendored DSP
 * is redirected onto (via -Dcalloc=amb_calloc etc.). Force-included into the
 * DSP translation units so the prototypes exist even without <stdlib.h>. */
#ifndef AMB_ALLOC_H
#define AMB_ALLOC_H
#include <stddef.h>
void* amb_calloc(size_t nmemb, size_t size);
void* amb_malloc(size_t size);
void  amb_free(void* p);
void* amb_realloc(void* p, size_t size);
#endif
