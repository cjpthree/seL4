/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <benchmark.h>
#include <api/syscall.h>
#include <api/failures.h>
#include <api/faults.h>
#include <kernel/cspace.h>
#include <kernel/faulthandler.h>
#include <kernel/thread.h>
#include <kernel/vspace.h>
#include <machine/io.h>
#include <machine/timer.h>
#include <object/interrupt.h>
#include <model/statedata.h>
#include <string.h>

#ifdef DEBUG
#include <arch/machine/capdl.h>
#endif

/* The haskell function 'handleEvent' is split into 'handleXXX' variants
 * for each event causing a kernel entry */

exception_t
handleInterruptEntry(void)
{
    irq_t irq;

    irq = getActiveIRQ();
#ifdef DEBUG
    ksKernelEntry.path = Debug_Interrupt;
    ksKernelEntry.irq = irq;
#endif /* DEBUG */
    if (irq != irqInvalid) {
        handleInterrupt(irq);
    } else {
        printf("Spurious interrupt\n");
        handleSpuriousIRQ();
    }

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}

exception_t
handleUnknownSyscall(word_t w)
{
#ifdef DEBUG
    ksKernelEntry.path = Debug_UnknownSyscall;
    ksKernelEntry.word = w;

    if (w == SysDebugPutChar) {
        kernel_putchar(getRegister(ksCurThread, capRegister));
        return EXCEPTION_NONE;
    }
    if (w == SysDebugHalt) {
        printf("Debug halt syscall from user thread %p\n", ksCurThread);
        halt();
    }
    if (w == SysDebugSnapshot) {
        printf("Debug snapshot syscall from user thread %p\n", ksCurThread);
        capDL();
        return EXCEPTION_NONE;
    }
    if (w == SysDebugCapIdentify) {
        word_t cptr = getRegister(ksCurThread, capRegister);
        lookupCapAndSlot_ret_t lu_ret = lookupCapAndSlot(ksCurThread, cptr);
        word_t cap_type = cap_get_capType(lu_ret.cap);
        setRegister(ksCurThread, capRegister, cap_type);
        return EXCEPTION_NONE;
    }
    if (w == SysDebugNameThread) {
        /* This is a syscall meant to aid debugging, so if anything goes wrong
         * then assume the system is completely misconfigured and halt */
        const char *name;
        word_t cptr = getRegister(ksCurThread, capRegister);
        lookupCapAndSlot_ret_t lu_ret = lookupCapAndSlot(ksCurThread, cptr);
        /* ensure we got a TCB cap */
        word_t cap_type = cap_get_capType(lu_ret.cap);
        if (cap_type != cap_thread_cap) {
            userError("SysDebugNameThread: cap is not a TCB, halting");
            halt();
        }
        /* Add 1 to the IPC buffer to skip the message info word */
        name = (const char*)(lookupIPCBuffer(true, ksCurThread) + 1);
        if (!name) {
            userError("SysDebugNameThread: Failed to lookup IPC buffer, halting");
            halt();
        }
        /* ensure the name isn't too long */
        if (name[strnlen(name, seL4_MsgMaxLength * sizeof(word_t))] != '\0') {
            userError("SysDebugNameThread: Name too long, halting");
            halt();
        }
        setThreadName(TCB_PTR(cap_thread_cap_get_capTCBPtr(lu_ret.cap)), name);
        return EXCEPTION_NONE;
    }
#endif

#ifdef DANGEROUS_CODE_INJECTION
    if (w == SysDebugRun) {
        ((void (*) (void *))getRegister(ksCurThread, capRegister))((void*)getRegister(ksCurThread, msgInfoRegister));
        return EXCEPTION_NONE;
    }
#endif

#if CONFIG_MAX_NUM_TRACE_POINTS > 0
    if (w == SysBenchmarkResetLog) {
        ksLogIndex = 0;
        return EXCEPTION_NONE;
    } else if (w == SysBenchmarkDumpLog) {
        word_t i;
        word_t *buffer = lookupIPCBuffer(true, ksCurThread);
        word_t start = getRegister(ksCurThread, capRegister);
        word_t size = getRegister(ksCurThread, msgInfoRegister);
        word_t logSize = ksLogIndexFinalized > MAX_LOG_SIZE ? MAX_LOG_SIZE : ksLogIndexFinalized;

        if (buffer == NULL) {
            userError("Cannot dump benchmarking log to a thread without an ipc buffer\n");
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (start > logSize) {
            userError("Start > logsize\n");
            current_syscall_error.type = seL4_InvalidArgument;
            return EXCEPTION_SYSCALL_ERROR;
        }

        /* Assume we have access to an ipc buffer 1024 words big.
         * Do no write to the first 4 bytes as these are overwritten */
        if (size > MAX_IPC_BUFFER_STORAGE) {
            size = MAX_IPC_BUFFER_STORAGE;
        }

        /* trim to size */
        if ((start + size) > logSize) {
            size = logSize - start;
        }

        /* write to ipc buffer */
        for (i = 0; i < size; i++) {
            int base_index = i * 2 + 1;
            ks_log_entry_t *log = &ksLog[i + start];
            buffer[base_index] = log->key;
            buffer[base_index + 1] = log->data;
        }

        /* Return the amount written */
        setRegister(ksCurThread, capRegister, size);
        return EXCEPTION_NONE;
    } else if (w == SysBenchmarkLogSize) {
        /* Return the amount of log items we tried to log (may exceed max size) */
        setRegister(ksCurThread, capRegister, ksLogIndexFinalized);
        return EXCEPTION_NONE;
    } else if (w == SysBenchmarkFinalizeLog) {
        ksLogIndexFinalized = ksLogIndex;
        return EXCEPTION_NONE;
    }
#endif /* CONFIG_MAX_NUM_TRACE_POINTS > 0 */

    /* we don't account for unknown syscalls that are for debugging or benchmarking,
     * so don't record the kernel entry time until now */
    updateTimestamp();
    if (likely(checkBudget())) {
        current_fault = fault_unknown_syscall_new(w);
        handleFault(ksCurThread);
    } else {
        /* try again when the thread has budget */
        setThreadState(ksCurThread, ThreadState_Restart);
    }

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}

exception_t
handleUserLevelFault(word_t w_a, word_t w_b)
{
#ifdef DEBUG
    ksKernelEntry.path = Debug_UserLevelFault;
    ksKernelEntry.number = w_a;
    ksKernelEntry.code = w_b;
#endif /* DEBUG */

    updateTimestamp();
    if (likely(checkBudget())) {
        current_fault = fault_user_exception_new(w_a, w_b);
        handleFault(ksCurThread);
    } else {
        /* try again when the thread has budget */
        setThreadState(ksCurThread, ThreadState_Restart);
    }

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}

exception_t
handleVMFaultEvent(vm_fault_type_t vm_faultType)
{
    exception_t status;
#ifdef DEBUG
    ksKernelEntry.path = Debug_VMFault;
    ksKernelEntry.fault_type = vm_faultType;
#endif /* DEBUG */

    updateTimestamp();
    if (likely(checkBudget())) {
        status = handleVMFault(ksCurThread, vm_faultType);
        if (status != EXCEPTION_NONE) {
            handleFault(ksCurThread);
        }
    } else {
        /* try again when the thread has budget */
        setThreadState(ksCurThread, ThreadState_Restart);
    }

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}


static exception_t
handleInvocation(bool_t isCall, bool_t isBlocking)
{
    message_info_t info;
    cptr_t cptr;
    lookupCapAndSlot_ret_t lu_ret;
    word_t *buffer;
    exception_t status;
    word_t length;
    tcb_t *thread;

    thread = ksCurThread;

    info = messageInfoFromWord(getRegister(thread, msgInfoRegister));
    cptr = getRegister(thread, capRegister);

    /* faulting section */
    lu_ret = lookupCapAndSlot(thread, cptr);

#ifdef DEBUG
    ksKernelEntry.cap_type = cap_get_capType(lu_ret.cap);
    ksKernelEntry.invocation_tag = message_info_get_msgLabel(info);
#endif /* DEBUG */

    if (unlikely(lu_ret.status != EXCEPTION_NONE)) {
        userError("Invocation of invalid cap #%lu.", cptr);
        current_fault = fault_cap_fault_new(cptr, false);

        if (isBlocking) {
            handleFault(thread);
        }

        return EXCEPTION_NONE;
    }

    buffer = lookupIPCBuffer(false, thread);

    status = lookupExtraCaps(thread, buffer, info);

    if (unlikely(status != EXCEPTION_NONE)) {
        userError("Lookup of extra caps failed.");
        if (isBlocking) {
            handleFault(thread);
        }
        return EXCEPTION_NONE;
    }

    /* Syscall error/Preemptible section */
    length = message_info_get_msgLength(info);
    if (unlikely(length > n_msgRegisters && !buffer)) {
        length = n_msgRegisters;
    }
    status = decodeInvocation(message_info_get_msgLabel(info), length,
                              cptr, lu_ret.slot, lu_ret.cap,
                              current_extra_caps, isBlocking, isCall,
                              buffer);

    if (unlikely(status == EXCEPTION_PREEMPTED)) {
        return status;
    }

    if (unlikely(status == EXCEPTION_SYSCALL_ERROR)) {
        if (isCall) {
            replyFromKernel_error(thread);
        }
        return EXCEPTION_NONE;
    }

    if (unlikely(
                thread_state_get_tsType(thread->tcbState) == ThreadState_Restart)) {
        if (isCall) {
            replyFromKernel_success_empty(thread);
        }
        setThreadState(thread, ThreadState_Running);
    }

    return EXCEPTION_NONE;
}

static void
handleReply(void)
{
    cte_t *callerSlot;
    cap_t callerCap;

    callerSlot = TCB_PTR_CTE_PTR(ksCurThread, tcbCaller);
    callerCap = callerSlot->cap;
    switch (cap_get_capType(callerCap)) {
    case cap_reply_cap: {
        tcb_t *caller;

        if (cap_reply_cap_get_capReplyMaster(callerCap)) {
            break;
        }
        caller = TCB_PTR(cap_reply_cap_get_capTCBPtr(callerCap));
        /* Haskell error:
         * "handleReply: caller must not be the current thread" */
        assert(caller != ksCurThread);
        doReplyTransfer(ksCurThread, caller, callerSlot);
        return;
    }

    case cap_null_cap:
        userError("Attempted reply operation when no reply cap present.");
        return;

    default:
        break;
    }

    fail("handleReply: invalid caller cap");
}

static void
handleRecv(bool_t isBlocking)
{
    word_t epCPtr;
    lookupCap_ret_t lu_ret;

    epCPtr = getRegister(ksCurThread, capRegister);

    lu_ret = lookupCap(ksCurThread, epCPtr);
    if (unlikely(lu_ret.status != EXCEPTION_NONE)) {
        /* current_lookup_fault has been set by lookupCap */
        current_fault = fault_cap_fault_new(epCPtr, true);
        handleFault(ksCurThread);
        return;
    }

    switch (cap_get_capType(lu_ret.cap)) {
    case cap_endpoint_cap:
        if (unlikely(!cap_endpoint_cap_get_capCanReceive(lu_ret.cap))) {
            current_lookup_fault = lookup_fault_missing_capability_new(0);
            current_fault = fault_cap_fault_new(epCPtr, true);
            handleFault(ksCurThread);
            break;
        }

        deleteCallerCap(ksCurThread);
        receiveIPC(ksCurThread, lu_ret.cap, isBlocking);
        break;

    case cap_notification_cap: {
        notification_t *ntfnPtr;
        tcb_t *boundTCB;
        ntfnPtr = NTFN_PTR(cap_notification_cap_get_capNtfnPtr(lu_ret.cap));
        boundTCB = (tcb_t*)notification_ptr_get_ntfnBoundTCB(ntfnPtr);
        if (unlikely(!cap_notification_cap_get_capNtfnCanReceive(lu_ret.cap)
                     || (boundTCB && boundTCB != ksCurThread))) {
            current_lookup_fault = lookup_fault_missing_capability_new(0);
            current_fault = fault_cap_fault_new(epCPtr, true);
            handleFault(ksCurThread);
            break;
        }

        receiveSignal(ksCurThread, lu_ret.cap, isBlocking);
        break;
    }
    default:
        current_lookup_fault = lookup_fault_missing_capability_new(0);
        current_fault = fault_cap_fault_new(epCPtr, true);
        handleFault(ksCurThread);
        break;
    }
}

static void
handleYield(void)
{

    /* ksCurThread should never be in the scheduler queue */
    assert(!thread_state_get_tcbQueued(ksCurThread->tcbState));

    /* thread has abandoned the rest of its current budget, either: */
    if (ready(ksCurThread)) {
        /* recharge and apply round robin */
        recharge(ksCurThread->tcbSchedContext);
        tcbSchedAppend(ksCurThread);
    } else {
        /* or postpone until budget is due to be recharged again */
        postpone(ksCurThread->tcbSchedContext);
    }

    ksConsumed = 0llu;
    rescheduleRequired();
}

exception_t
handleSyscall(syscall_t syscall)
{
    exception_t ret;
    irq_t irq;

#ifdef DEBUG
    ksKernelEntry.path = Debug_Syscall;
    ksKernelEntry.syscall_no = syscall;
#endif /* DEBUG */

    ret = EXCEPTION_NONE;
    updateTimestamp();
    if (checkBudget()) {
        switch (syscall) {
        case SysSend:
            ret = handleInvocation(false, true);
            break;

        case SysNBSend:
            ret = handleInvocation(false, false);
            break;

        case SysCall:
            ret = handleInvocation(true, true);
            break;

        case SysRecv:
            handleRecv(true);
            break;

        case SysReply:
            handleReply();
            break;

        case SysReplyRecv:
            handleReply();
            handleRecv(true);
            break;

        case SysNBRecv:
            handleRecv(false);
            break;

        case SysYield:
            handleYield();
            break;

        default:
            fail("Invalid syscall");
        }

        /* this will occur if any preemption points where triggered */
        if (unlikely(ret == EXCEPTION_PREEMPTED)) {
            irq = getActiveIRQ();
            if (irq != irqInvalid) {
                handleInterrupt(irq);
            }
        }
    } else {
        /* try again when the thread has budget */
        setThreadState(ksCurThread, ThreadState_Restart);
    }

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}
