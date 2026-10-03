/* Copyright (c) 2013, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/* minidump_format.h: A cross-platform reimplementation of minidump-related
 * portions of DbgHelp.h from the Windows Platform SDK.
 *
 * (This is C99 source, please don't corrupt it with C++.)
 *
 * This file contains the necessary definitions to read minidump files
 * produced on LOONG64.  These files may be read on any platform provided
 * that the alignments of these structures on the processing system are
 * identical to the alignments of these structures on the producing system.
 * For this reason, precise-sized types are used.  The structures defined
 * by this file have been laid out to minimize alignment problems by
 * ensuring that all members are aligned on their natural boundaries.
 * In some cases, tail-padding may be significant when different ABIs specify
 * different tail-padding behaviors.  To avoid problems when reading or
 * writing affected structures, MD_*_SIZE macros are provided where needed,
 * containing the useful size of the structures without padding.
 *
 * Structures that are defined by Microsoft to contain a zero-length array
 * are instead defined here to contain an array with one element, as
 * zero-length arrays are forbidden by standard C and C++.  In these cases,
 * *_minsize constants are provided to be used in place of sizeof.  For a
 * cleaner interface to these sizes when using C++, see minidump_size.h.
 *
 * These structures are also sufficient to populate minidump files.
 *
 * Because precise data type sizes are crucial for this implementation to
 * function properly and portably, a set of primitive types with known sizes
 * are used as the basis of each structure defined by this file.
 *
 * Author: Chris Dearman
 */

/*
 * LOONG64 support
 */

#ifndef GOOGLE_BREAKPAD_COMMON_MINIDUMP_CPU_LOONG64_H__
#define GOOGLE_BREAKPAD_COMMON_MINIDUMP_CPU_LOONG64_H__

#define MD_CONTEXT_LOONG64_GPR_COUNT 32
#define MD_FLOATINGSAVEAREA_LOONG64_FPR_COUNT 32

/*
 * Note that these structures *do not* map directly to the CONTEXT
 * structure defined in WinNT.h in the Windows Mobile SDK. That structure
 * does not accomodate VFPv3, and I'm unsure if it was ever used in the
 * wild anyway, as Windows CE only seems to produce "cedumps" which
 * are not exactly minidumps.
 */
typedef struct {
  // TODO:LOONG64 Support vector FPU
  /* 32 64-bit floating point registers, f0..f31 */
  uint64_t regs[MD_FLOATINGSAVEAREA_LOONG64_FPR_COUNT];

  uint32_t fcsr; /* FPU control register */
  uint32_t vcsr; /* Vector control register */
  uint64_t fcc;  /* FPU status register */
} MDFloatingSaveAreaLOONG64;

typedef struct {
  /* The next field determines the layout of the structure, and which parts
   * of it are populated.
   */
  uint32_t context_flags;
  uint32_t _pad0;

  /* 32 64-bit integer registers, r0..r31.
   * Note the following fixed uses:
   *   r1 is the return address.
   *   r3 is the stack pointer.
   *   r22 is the frame pointer.
   */
  uint64_t iregs[MD_CONTEXT_LOONG64_GPR_COUNT];

  uint64_t pc;

  /* The next field is included with MD_CONTEXT_LOONG64_FLOATING_POINT. */
  MDFloatingSaveAreaLOONG64 float_save;

} MDRawContextLOONG64;

/* Indices into iregs for registers with a dedicated or conventional
 * purpose.
 */
enum MDLOONG64RegisterNumbers {
  MD_CONTEXT_LOONG64_REG_RA     = 1,
  MD_CONTEXT_LOONG64_REG_SP     = 3,
  MD_CONTEXT_LOONG64_REG_FP     = 22,
  MD_CONTEXT_LOONG64_REG_S0     = 23,
  MD_CONTEXT_LOONG64_REG_S1     = 24,
  MD_CONTEXT_LOONG64_REG_S2     = 25,
  MD_CONTEXT_LOONG64_REG_S3     = 26,
  MD_CONTEXT_LOONG64_REG_S4     = 27,
  MD_CONTEXT_LOONG64_REG_S5     = 28,
  MD_CONTEXT_LOONG64_REG_S6     = 29,
  MD_CONTEXT_LOONG64_REG_S7     = 30,
  MD_CONTEXT_LOONG64_REG_S8     = 31,
};

/* For (MDRawContextLOONG64).context_flags.  These values indicate the type of
 * context stored in the structure. */
/* CONTEXT_LOONG64 from the Windows CE 5.0 SDK. This value isn't correct
 * because this bit can be used for flags. Presumably this value was
 * never actually used in minidumps, but only in "CEDumps" which
 * are a whole parallel minidump file format for Windows CE.
 * Therefore, Breakpad defines its own value for LOONG64 CPUs.
 */

/**
 * Breakpad defines for LOONG64
 */
#define MD_CONTEXT_LOONG64  0x000a0000
#define MD_CONTEXT_LOONG64_INTEGER           (MD_CONTEXT_LOONG64 | 0x00000002)
#define MD_CONTEXT_LOONG64_FLOATING_POINT    (MD_CONTEXT_LOONG64 | 0x00000004)

#define MD_CONTEXT_LOONG64_FULL              (MD_CONTEXT_LOONG64_INTEGER | \
                                             MD_CONTEXT_LOONG64_FLOATING_POINT)

#define MD_CONTEXT_LOONG64_ALL               (MD_CONTEXT_LOONG64_INTEGER | \
                                             MD_CONTEXT_LOONG64_FLOATING_POINT)

#endif  // GOOGLE_BREAKPAD_COMMON_MINIDUMP_CPU_LOONG64_H__
