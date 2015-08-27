#ifndef __PRNG_H
#define __PRNG_H

typedef unsigned long int  u4;
typedef struct ranctx { u4 a; u4 b; u4 c; u4 d; } ranctx;

#define rot(x,k) (((x)<<(k))|((x)>>(32-(k))))

void raninit (ranctx *x, u4 seed);
u4 ranval (ranctx *x);

#endif
