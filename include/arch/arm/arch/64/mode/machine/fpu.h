/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#ifndef __MODE_MACHINE_FPU_H
#define __MODE_MACHINE_FPU_H

#include <mode/machine/registerset.h>

#ifdef CONFIG_HAVE_FPU
/* Store state in the FPU registers into memory. */
static inline void saveFpuState(user_fpu_state_t *dest)
{
    word_t temp;

    asm volatile(
        /* SIMD and floating-point register file */
        "stp     q0, q1, [%1, #16 * 0]      \n"
        "stp     q2, q3, [%1, #16 * 2]      \n"
        "stp     q4, q5, [%1, #16 * 4]      \n"
        "stp     q6, q7, [%1, #16 * 6]      \n"
        "stp     q8, q9, [%1, #16 * 8]      \n"
        "stp     q10, q11, [%1, #16 * 10]   \n"
        "stp     q12, q13, [%1, #16 * 12]   \n"
        "stp     q14, q15, [%1, #16 * 14]   \n"
        "stp     q16, q17, [%1, #16 * 16]   \n"
        "stp     q18, q19, [%1, #16 * 18]   \n"
        "stp     q20, q21, [%1, #16 * 20]   \n"
        "stp     q22, q23, [%1, #16 * 22]   \n"
        "stp     q24, q25, [%1, #16 * 24]   \n"
        "stp     q26, q27, [%1, #16 * 26]   \n"
        "stp     q28, q29, [%1, #16 * 28]   \n"
        "stp     q30, q31, [%1, #16 * 30]   \n"

        /* FP control and status registers */
        "mrs     %0, fpsr                   \n"
        "str     %w0, [%1, #16 * 32]        \n"
        "mrs     %0, fpcr                   \n"
        "str     %w0, [%1, #16 * 32 + 4]    \n"
        : "=&r" (temp)
        : "r" (dest)
        : "memory"
    );
}

/* Load FPU state from memory into the FPU registers. */
static inline void loadFpuState(user_fpu_state_t *src)
{
    word_t temp;

    asm volatile(
        /* SIMD and floating-point register file */
        "ldp     q0, q1, [%1, #16 * 0]      \n"
        "ldp     q2, q3, [%1, #16 * 2]      \n"
        "ldp     q4, q5, [%1, #16 * 4]      \n"
        "ldp     q6, q7, [%1, #16 * 6]      \n"
        "ldp     q8, q9, [%1, #16 * 8]      \n"
        "ldp     q10, q11, [%1, #16 * 10]   \n"
        "ldp     q12, q13, [%1, #16 * 12]   \n"
        "ldp     q14, q15, [%1, #16 * 14]   \n"
        "ldp     q16, q17, [%1, #16 * 16]   \n"
        "ldp     q18, q19, [%1, #16 * 18]   \n"
        "ldp     q20, q21, [%1, #16 * 20]   \n"
        "ldp     q22, q23, [%1, #16 * 22]   \n"
        "ldp     q24, q25, [%1, #16 * 24]   \n"
        "ldp     q26, q27, [%1, #16 * 26]   \n"
        "ldp     q28, q29, [%1, #16 * 28]   \n"
        "ldp     q30, q31, [%1, #16 * 30]  \n"

        /* FP control and status registers */
        "ldr     %w0, [%1, #16 * 32]        \n"
        "msr     fpsr, %0                   \n"
        "ldr     %w0, [%1, #16 * 32 + 4]    \n"
        "msr     fpcr, %0                   \n"
        : "=&r" (temp)
        : "r" (src)
        : "memory"
    );
}

/* Enable the FPU to be used without faulting.
 * Required even if the kernel attempts to use the FPU. */
static inline void enableFpu(void)
{
    word_t cpacr;

    MRS("cpacr_el1", cpacr);
    cpacr |= (3 << CPACR_EL1_FPEN);
    MSR("cpacr_el1", cpacr);
}

#endif /* CONFIG_HAVE_FPU */

/* Disable the FPU so that usage of it causes a fault */
static inline void disableFpu(void)
{
    word_t cpacr;

    MRS("cpacr_el1", cpacr);
    cpacr &= ~(3 << CPACR_EL1_FPEN);
    cpacr |= (1 << CPACR_EL1_FPEN);
    MSR("cpacr_el1", cpacr);
}

#endif /* __MODE_MACHINE_FPU_H */
