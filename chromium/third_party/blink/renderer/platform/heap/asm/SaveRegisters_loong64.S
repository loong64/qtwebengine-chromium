// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * typedef void (*PushAllRegistersCallback)(ThreadState*, intptr_t*);
 * extern "C" void PushAllRegisters(ThreadState*, PushAllRegistersCallback)
 */

.type PushAllRegisters, %function
.global PushAllRegisters
.hidden PushAllRegisters
PushAllRegisters:
        // Push all callee-saves registers to get them
        // on the stack for conservative stack scanning.
        // Reserve space for callee-saved registers and return address.
        addi.d $sp,$sp,-80
        // Save the callee-saved registers and the return address.
        st.d $s0, $sp, 0
        st.d $s1, $sp, 8
        st.d $s2, $sp, 16
        st.d $s3, $sp, 24
        st.d $s4, $sp, 32
        st.d $s5, $sp, 40
        st.d $s6, $sp, 48
        st.d $s7, $sp, 56
        st.d $ra, $sp, 64
        // Note: the callee-saved floating point registers do not need to be
        // copied to the stack, because fp registers never hold heap pointers
        // and so do not need to be kept visible to the garbage collector.
        // Pass the first argument untouched in a0 and the
        // stack pointer to the callback.
        ori $t7, $a1, 0
        ori $a1, $sp, 0
        jirl $ra, $t7, 0
        // Restore return address, adjust stack and return.
        // Note: the copied registers do not need to be reloaded here,
        // because they were preserved by the called routine.
        ld.d $ra, $sp, 64
        addi.d $sp, $sp, 80
        jirl $r0, $ra, 0
