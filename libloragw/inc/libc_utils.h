#include <stddef.h>
#include <stdint.h>

typedef int (*cmpfun1)(const void *, const void *);
typedef int (*cmpfun2)(const void *, const void *, void *);

void qsort(void *base, size_t nel, size_t width, cmpfun1 cmp1);
void qsort_r(void *base, size_t nel, size_t width, cmpfun2 cmp2, void *arg);