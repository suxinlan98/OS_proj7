// SPDX-License-Identifier: BSD-2-Clause
#ifndef XV6_BLOCK_H
#define XV6_BLOCK_H

#include "types.h"

int block_read_bytes(uint dev, uint off, uchar *dst, uint n);
int block_write_bytes(uint dev, uint off, uchar *src, uint n);

#endif
