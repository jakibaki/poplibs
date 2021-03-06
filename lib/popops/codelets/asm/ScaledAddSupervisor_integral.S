// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifdef __IPU__

#include "poplar/AvailableVTypes.h"
#include "poplar/StackSizeDefs.hpp"

#define VERTEX(ty) __runCodelet_popops__ScaledAddSupervisor___ ## ty
#define VERTEX_SUBTRACT(ty) __runCodelet_popops__ScaledSubtractSupervisor___ ## ty

.globl VERTEX(int_int_int_true_false)
.type VERTEX(int_int_int_true_false), @function

.globl VERTEX(unsigned_int_unsigned_int_unsigned_int_true_false)
.type VERTEX(unsigned_int_unsigned_int_unsigned_int_true_flase), @function

.globl VERTEX(int_int_int_false_false)
.type VERTEX(int_int_int_false_false), @function

.globl VERTEX(unsigned_int_unsigned_int_unsigned_int_false_false)
.type VERTEX(unsigned_int_unsigned_int_unsigned_int_false_false), @function

.globl VERTEX_SUBTRACT(int_int_false)
.type VERTEX_SUBTRACT(int_int_false), @function

.globl VERTEX_SUBTRACT(unsigned_int_unsigned_int_false)
.type VERTEX_SUBTRACT(unsigned_int_unsigned_int_false), @function

// constants
#if defined(VECTOR_AVAIL_SCALED_PTR64)
#define VERTEX_DATA_A_START_OFFSET 0
#define VERTEX_DATA_A_SIZE_OFFSET 2
#define VERTEX_DATA_B_OFFSET 4
//Constant (k) and non-constant (ptr to factor) versions
#define VERTEX_SCALE_B_OFFSET 6
// Const - in 32 bit
#define VERTEX_SCALE_B_OFFSET_CONST 8

#define SCALED_PTR64_SHL_BITS 3
#else
#define VERTEX_DATA_A_START_OFFSET 0
#define VERTEX_DATA_A_SIZE_OFFSET 4
#define VERTEX_DATA_B_OFFSET 8
//Constant (k) and non-constant (ptr to factor) versions
#define VERTEX_SCALE_B_OFFSET 12
// Const - in 32 bit
#define VERTEX_SCALE_B_OFFSET_CONST 12
#endif

// integer variables
#define sizeD3M1 m0
#define dataPtr m1
#define dataBPtr m2
#define rem m3
#define k m4

#define data0 m5
#define data1 m6
#define data2 m7

// we trash $fp (m9) and $lr (m10) here but we keep $sp and use that to store
// them to the stack so this fine.
#define dataB0 m8
#define dataB1 m9
#define dataB2 m10

// scratch registers, these use the same registers as the dataN registers but
// are only used in the prologue.
#define dataSize m5
#define sizeD3 m6

#define STACK_SIZE  8

DEF_STACK_USAGE STACK_SIZE .text.VERTEX(int_int_int_false)
.section .text.VERTEX(int_int_int_false)
.align 4
.supervisor
VERTEX(int_int_int_false_false):
VERTEX(unsigned_int_unsigned_int_unsigned_int_false_false):
  // keeping this before the branch means it doesn't cause a stall later
  add $sp, $sp, -STACK_SIZE

#if defined(VECTOR_AVAIL_SCALED_PTR64)
  ldz16 $k, $m0, $mzero, VERTEX_SCALE_B_OFFSET/2
  shl  $k, $k, SCALED_PTR64_SHL_BITS
#else
  ld32 $k, $m0, $mzero, VERTEX_SCALE_B_OFFSET/4
#endif
  ld32 $k, $mzero, $k, 0 // 6 cycles
  bri  VERTEX(int_common)// 6 cycles
.size VERTEX(int_false), .-VERTEX(int_int_int_false_false)

DEF_STACK_USAGE STACK_SIZE .text.VERTEX(int_true)
.section .text.VERTEX(int_true)
.align 4

VERTEX(int_int_int_true_false):
VERTEX(unsigned_int_unsigned_int_unsigned_int_true_false):
 // keeping this before the branch means it doesn't cause a stall later
  add $sp, $sp, -STACK_SIZE

  ld32 $k, $m0, $mzero, VERTEX_SCALE_B_OFFSET_CONST/4
  bri  VERTEX(int_common) //6 cycles
.size VERTEX(int_true), .-VERTEX(int_int_int_true_false)

DEF_STACK_USAGE STACK_SIZE .text.VERTEX_SUBTRACT(int_int_false_false)
.section .text.VERTEX_SUBTRACT(int_int_false_false)
.align 4
VERTEX_SUBTRACT(int_int_false):
VERTEX_SUBTRACT(unsigned_int_unsigned_int_false):
  // keeping this before the branch means it doesn't cause a stall later
  add $sp, $sp, -STACK_SIZE

#if defined(VECTOR_AVAIL_SCALED_PTR64)
  ldz16 $k, $m0, $mzero, VERTEX_SCALE_B_OFFSET/2
  shl  $k, $k, SCALED_PTR64_SHL_BITS
#else
  ld32 $k, $m0, $mzero, VERTEX_SCALE_B_OFFSET/4
#endif
  ld32 $k, $mzero, $k, 0 // 6 cycles
  bri  VERTEX_SUBTRACT(int_common)// 6 cycles
.size VERTEX_SUBTRACT(int), .-VERTEX_SUBTRACT(int_int_false)

.section .text.VERTEX(int_common)
.align 4
VERTEX_SUBTRACT(int_common):
  mul  $k, $k, -1
VERTEX(int_common):
  // Ordered to avoid pipe hits
#if defined(VECTOR_AVAIL_SCALED_PTR64)
  ldz16 $dataBPtr, $m0, $mzero, VERTEX_DATA_B_OFFSET/2
  ldz16 $dataPtr, $m0, $mzero, VERTEX_DATA_A_START_OFFSET/2
#else
  ld32 $dataBPtr, $m0, $mzero, VERTEX_DATA_B_OFFSET/4
  ld32 $dataPtr, $m0, $mzero, VERTEX_DATA_A_START_OFFSET/4
#endif
  ldz16 $dataSize, $m0, $mzero, VERTEX_DATA_A_SIZE_OFFSET/2
  // NOTE: this trashes the vertex state pointer because $dataBPtr is mapped to $m0.

  setzi $sizeD3, 0xAAAB

  // store the registers we need to restore at the end to the stack
  st32 $fp, $sp, $mzero, 0
  st32 $lr, $sp, $mzero, 1

  // calculate n/3 using (n * 0xAAAB) >> 17
  // see recipe #1 for how these constants were derived:
  //   https://embeddedgurus.com/stack-overflow/2009/06/division-of-integers-by-constants/
  mul $sizeD3, $dataSize, $sizeD3 // 6 cycles

#if defined(VECTOR_AVAIL_SCALED_PTR64)
  // Inserted here to avoid pipe hits
  shl $dataBPtr, $dataBPtr, SCALED_PTR64_SHL_BITS
  shl $dataPtr, $dataPtr, SCALED_PTR64_SHL_BITS
#endif

  shr $sizeD3, $sizeD3, 17 // 6 cycles

  // calculate n%3 using n - (n/3)*3
  mul $rem, $sizeD3, 3 // 6 cycles
  sub $rem, $dataSize, $rem // 6 cycles

  // minus 1 from each size for the brnzdec
  add $sizeD3M1, $sizeD3, -1

  // skip the x3 loop if we have less than 3 to process. 6 cycles if
  // sizeD3M1 < 0, 1 otherwise.
  brneg $sizeD3M1, .Lloop1_prologue // 6 cycles

.Lloop3:
  // we must make sure we won't use any of the resulting values from a load
  // until at least 6 cycles later. processing three at a time here enforces
  // that guarantee.
  ld32step $dataB0, $mzero, $dataBPtr+=, 1
  ld32step $dataB1, $mzero, $dataBPtr+=, 1
  ld32step $dataB2, $mzero, $dataBPtr+=, 1

  ld32 $data0, $mzero, $dataPtr, 0
  ld32 $data1, $mzero, $dataPtr, 1
  ld32 $data2, $mzero, $dataPtr, 2

  // avoid register bubble
  nop

  mul $dataB0, $dataB0, $k
  mul $dataB1, $dataB1, $k
  mul $dataB2, $dataB2, $k

  add $data0, $data0, $dataB0
  add $data1, $data1, $dataB1
  add $data2, $data2, $dataB2

  // avoid register bubble
  nop ; nop ; nop ; nop

  st32step $data0, $mzero, $dataPtr+=, 1
  st32step $data1, $mzero, $dataPtr+=, 1
  st32step $data2, $mzero, $dataPtr+=, 1

  // 6 cycle penalty here each iteration.
  brnzdec $sizeD3M1, .Lloop3

.Lloop1_prologue:
  // process the remainder (if any). 6 cycles if rem == 0, 1 otherwise.
  brz $rem, .Lepilogue

  // minus 1 from the remainder for the brnzdec. do this before we check for
  add $rem, $rem, -1 // 6 cycles
.Lloop1:
  ld32 $data0, $mzero, $dataPtr, 0
  ld32step $dataB0, $mzero, $dataBPtr+=, 1

  mul $dataB0, $dataB0, $k // 6 cycles
  add $data0, $data0, $dataB0 // 6 cycles
  st32step $data0, $mzero, $dataPtr+=, 1 // 6 cycles

  // 6 cycle penalty here each iteration. perhaps it's worth unrolling this
  // loop as it will only ever be 2 iterations at most.
  brnzdec $rem, .Lloop1

  // intentional fallthrough

.Lepilogue:
  // restore $fp, $lr and $sp
  ld32step $fp, $mzero, $sp+=, 1
  ld32step $lr, $mzero, $sp+=, 1

  br $lr // 6 cycles

.size VERTEX(int_common), .-VERTEX(int_common)

#endif // __IPU__
