// Compatibility shims for static CRT routines that import Vista+ kernel APIs
// when built with a modern MSVC toolset. The add-in targets Windows XP SP2+.
#define WIN32_LEAN_AND_MEAN
#define WINBASEAPI
#include <windows.h>

#include <stdint.h>

extern "C" {

typedef void (NTAPI *PFLS_CALLBACK_FUNCTION)(PVOID);

BOOL WINAPI InitializeCriticalSectionEx(LPCRITICAL_SECTION criticalSection,
                                        DWORD spinCount, DWORD flags) {
    (void)flags;
    if (!criticalSection) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return InitializeCriticalSectionAndSpinCount(criticalSection, spinCount);
}

DWORD WINAPI FlsAlloc(PFLS_CALLBACK_FUNCTION callback) {
    (void)callback;
    return TlsAlloc();
}

BOOL WINAPI FlsFree(DWORD index) {
    return TlsFree(index);
}

PVOID WINAPI FlsGetValue(DWORD index) {
    return TlsGetValue(index);
}

BOOL WINAPI FlsSetValue(DWORD index, PVOID value) {
    return TlsSetValue(index, value);
}

typedef struct XpSrwLock {
    CRITICAL_SECTION criticalSection;
} XpSrwLock;

static XpSrwLock* GetSrwLock(PSRWLOCK lock) {
    if (!lock) return NULL;
    PVOID current = InterlockedCompareExchangePointer(&lock->Ptr, NULL, NULL);
    if (current) return (XpSrwLock*)current;
    XpSrwLock* created = (XpSrwLock*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(XpSrwLock));
    if (!created) return NULL;
    InitializeCriticalSection(&created->criticalSection);
    PVOID prior = InterlockedCompareExchangePointer(&lock->Ptr, created, NULL);
    if (prior) {
        DeleteCriticalSection(&created->criticalSection);
        HeapFree(GetProcessHeap(), 0, created);
        return (XpSrwLock*)prior;
    }
    return created;
}

VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK lock) {
    XpSrwLock* value = GetSrwLock(lock);
    if (value) EnterCriticalSection(&value->criticalSection);
}

VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK lock) {
    XpSrwLock* value = GetSrwLock(lock);
    if (value) LeaveCriticalSection(&value->criticalSection);
}

VOID WINAPI AcquireSRWLockShared(PSRWLOCK lock) {
    AcquireSRWLockExclusive(lock);
}

VOID WINAPI ReleaseSRWLockShared(PSRWLOCK lock) {
    ReleaseSRWLockExclusive(lock);
}

BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK lock) {
    XpSrwLock* value = GetSrwLock(lock);
    return value && TryEnterCriticalSection(&value->criticalSection);
}

BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK lock) {
    return TryAcquireSRWLockExclusive(lock);
}

typedef struct XpConditionVariable {
    HANDLE event;
} XpConditionVariable;

static XpConditionVariable* GetConditionVariable(PCONDITION_VARIABLE variable, bool create) {
    if (!variable) return NULL;
    PVOID current = InterlockedCompareExchangePointer(&variable->Ptr, NULL, NULL);
    if (current) return (XpConditionVariable*)current;
    if (!create) return NULL;
    XpConditionVariable* created =
        (XpConditionVariable*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(XpConditionVariable));
    if (!created) return NULL;
    created->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!created->event) {
        HeapFree(GetProcessHeap(), 0, created);
        return NULL;
    }
    PVOID prior = InterlockedCompareExchangePointer(&variable->Ptr, created, NULL);
    if (prior) {
        CloseHandle(created->event);
        HeapFree(GetProcessHeap(), 0, created);
        return (XpConditionVariable*)prior;
    }
    return created;
}

VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE variable) {
    variable->Ptr = NULL;
    GetConditionVariable(variable, true);
}

BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE variable, PSRWLOCK lock,
                                      DWORD milliseconds, ULONG flags) {
    XpConditionVariable* condition = GetConditionVariable(variable, true);
    if (!condition) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    bool shared = (flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED) != 0;
    if (shared) ReleaseSRWLockShared(lock);
    else ReleaseSRWLockExclusive(lock);
    DWORD wait = WaitForSingleObject(condition->event, milliseconds);
    if (shared) AcquireSRWLockShared(lock);
    else AcquireSRWLockExclusive(lock);
    if (wait == WAIT_OBJECT_0) {
        ResetEvent(condition->event);
        return TRUE;
    }
    if (wait == WAIT_TIMEOUT) {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE variable) {
    XpConditionVariable* condition = GetConditionVariable(variable, false);
    if (condition && condition->event) SetEvent(condition->event);
}

VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE variable) {
    WakeConditionVariable(variable);
}

} // extern "C"
