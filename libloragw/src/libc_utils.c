#include "libc_utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ntz(x) a_ctz_l((x))

#define compare(a, b) (cmp1 ? cmp1(a, b) : cmp2(a, b, arg))
#define cmp cmp1, cmp2, arg

static inline int a_ctz_64(uint64_t x) {
  static const char debruijn64[64] = {
      0,  1,  2,  53, 3,  7,  54, 27, 4,  38, 41, 8,  34, 55, 48, 28,
      62, 5,  39, 46, 44, 42, 22, 9,  24, 35, 59, 56, 49, 18, 29, 11,
      63, 52, 6,  26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
      51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12};
  static const char debruijn32[32] = {
      0,  1,  23, 2,  29, 24, 19, 3,  30, 27, 25, 11, 20, 8, 4,  13,
      31, 22, 28, 18, 26, 10, 7,  12, 21, 17, 9,  6,  16, 5, 15, 14};
  if (sizeof(long) < 8) {
    uint32_t y = x;
    if (!y) {
      y = x >> 32;
      return 32 + debruijn32[(y & -y) * 0x076be629 >> 27];
    }
    return debruijn32[(y & -y) * 0x076be629 >> 27];
  }
  return debruijn64[(x & -x) * 0x022fdd63cc95386dull >> 58];
}

static inline int a_ctz_l(unsigned long x) {
  static const char debruijn32[32] = {
      0,  1,  23, 2,  29, 24, 19, 3,  30, 27, 25, 11, 20, 8, 4,  13,
      31, 22, 28, 18, 26, 10, 7,  12, 21, 17, 9,  6,  16, 5, 15, 14};
  if (sizeof(long) == 8)
    return a_ctz_64(x);
  return debruijn32[(x & -x) * 0x076be629 >> 27];
}

static inline int pntz(size_t p[2]) {
  int r = ntz(p[0] - 1);
  if (r != 0 || (r = 8 * sizeof(size_t) + ntz(p[1])) != 8 * sizeof(size_t)) {
    return r;
  }
  return 0;
}

static void cycle(size_t width, unsigned char *ar[], int n) {
  unsigned char tmp[256];
  size_t l;
  int i;

  if (n < 2) {
    return;
  }

  ar[n] = tmp;
  while (width) {
    l = sizeof(tmp) < width ? sizeof(tmp) : width;
    memcpy(ar[n], ar[0], l);
    for (i = 0; i < n; i++) {
      memcpy(ar[i], ar[i + 1], l);
      ar[i] += l;
    }
    width -= l;
  }
}

/* shl() and shr() need n > 0 */
static inline void shl(size_t p[2], unsigned int n) {
  if (n >= 8 * sizeof(size_t)) {
    n -= 8 * sizeof(size_t);
    p[1] = p[0];
    p[0] = 0;
  }
  p[1] <<= n;
  p[1] |= p[0] >> (sizeof(size_t) * 8 - n);
  p[0] <<= n;
}

static inline void shr(size_t p[2], unsigned int n) {
  if (n >= 8 * sizeof(size_t)) {
    n -= 8 * sizeof(size_t);
    p[0] = p[1];
    p[1] = 0;
  }
  p[0] >>= n;
  p[0] |= p[1] << (sizeof(size_t) * 8 - n);
  p[1] >>= n;
}

static void sift(unsigned char *head, size_t width, cmpfun1 cmp1, cmpfun2 cmp2,
                 void *arg, int pshift, size_t lp[]) {
  unsigned char *rt, *lf;
  unsigned char *ar[14 * sizeof(size_t) + 1];
  int i = 1;

  ar[0] = head;
  while (pshift > 1) {
    rt = head - width;
    lf = head - width - lp[pshift - 2];

    if (compare(ar[0], lf) >= 0 && compare(ar[0], rt) >= 0) {
      break;
    }
    if (compare(lf, rt) >= 0) {
      ar[i++] = lf;
      head = lf;
      pshift -= 1;
    } else {
      ar[i++] = rt;
      head = rt;
      pshift -= 2;
    }
  }
  cycle(width, ar, i);
}

static void trinkle(unsigned char *head, size_t width, cmpfun1 cmp1,
                    cmpfun2 cmp2, void *arg, size_t pp[2], int pshift,
                    int trusty, size_t lp[]) {
  unsigned char *stepson, *rt, *lf;
  size_t p[2];
  unsigned char *ar[14 * sizeof(size_t) + 1];
  int i = 1;
  int trail;

  p[0] = pp[0];
  p[1] = pp[1];

  ar[0] = head;
  while (p[0] != 1 || p[1] != 0) {
    stepson = head - lp[pshift];
    if (compare(stepson, ar[0]) <= 0) {
      break;
    }
    if (!trusty && pshift > 1) {
      rt = head - width;
      lf = head - width - lp[pshift - 2];
      if (compare(rt, stepson) >= 0 || compare(lf, stepson) >= 0) {
        break;
      }
    }

    ar[i++] = stepson;
    head = stepson;
    trail = pntz(p);
    shr(p, trail);
    pshift += trail;
    trusty = 0;
  }
  if (!trusty) {
    cycle(width, ar, i);
    sift(head, width, cmp, pshift, lp);
  }
}

static void actual_qsort(void *base, size_t nel, size_t width, cmpfun1 cmp1,
                         cmpfun2 cmp2, void *arg) {
  size_t lp[12 * sizeof(size_t)];
  size_t i, size = width * nel;
  unsigned char *head, *high;
  size_t p[2] = {1, 0};
  int pshift = 1;
  int trail;

  if (!size)
    return;

  head = base;
  high = head + size - width;

  /* Precompute Leonardo numbers, scaled by element width */
  for (lp[0] = lp[1] = width, i = 2;
       (lp[i] = lp[i - 2] + lp[i - 1] + width) < size; i++)
    ;

  while (head < high) {
    if ((p[0] & 3) == 3) {
      sift(head, width, cmp, pshift, lp);
      shr(p, 2);
      pshift += 2;
    } else {
      if (lp[pshift - 1] >= (size_t)(high - head)) {
        trinkle(head, width, cmp, p, pshift, 0, lp);
      } else {
        sift(head, width, cmp, pshift, lp);
      }

      if (pshift == 1) {
        shl(p, 1);
        pshift = 0;
      } else {
        shl(p, pshift - 1);
        pshift = 1;
      }
    }

    p[0] |= 1;
    head += width;
  }

  trinkle(head, width, cmp, p, pshift, 0, lp);

  while (pshift != 1 || p[0] != 1 || p[1] != 0) {
    if (pshift <= 1) {
      trail = pntz(p);
      shr(p, trail);
      pshift += trail;
    } else {
      shl(p, 2);
      pshift -= 2;
      p[0] ^= 7;
      shr(p, 1);
      trinkle(head - lp[pshift] - width, width, cmp, p, pshift + 1, 1, lp);
      shl(p, 1);
      p[0] |= 1;
      trinkle(head - width, width, cmp, p, pshift, 1, lp);
    }
    head -= width;
  }
}

void qsort(void *base, size_t nel, size_t width, cmpfun1 cmp1) {
  actual_qsort(base, nel, width, cmp1, NULL, NULL);
}

void qsort_r(void *base, size_t nel, size_t width, cmpfun2 cmp2, void *arg) {
  actual_qsort(base, nel, width, NULL, cmp2, arg);
}
