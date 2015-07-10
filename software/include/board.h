#ifndef __BOARD_H
#define __BOARD_H

#if BOARD == TRIPOINT
#include "tripoint.h"
#else
#error "MUST #define BOARD"
#endif

#endif
