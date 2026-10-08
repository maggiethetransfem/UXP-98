/*
 * ntoswrap.sys
 *
 * Compatibility shim driver that re-exports a set of Windows 8 / 10+
 * ntoskrnl.exe symbols so that drivers built against a newer kernel can
 * load on Windows 7 x64.
 *
 * Each implementation below was derived from a disassembly of the Win10
 * ntoskrnl.exe export (see ntoskrnl_disasm.asm). Because the genuine
 * routines call non-exported internal helpers and depend on Win10-only
 * EPROCESS / KUSER_SHARED_DATA layouts, they cannot be copied verbatim.
 * Instead each symbol is provided as:
 *
 *   (a) a faithful re-implementation, where the logic is self-contained
 *       and ABI-stable across versions (ObReferenceObjectSafe and the CRT
 *       math / ctype helpers);
 *   (b) a forward to the closest Windows 7 export, resolved dynamically via
 *       MmGetSystemRoutineAddress where the target is not in the WDK import
 *       library; or
 *   (c) a sensible-default shim for subsystems that simply do not exist on
 *       Windows 7 (WNF, AppContainer / package claims, protected-process
 *       light, process wake counters, the new power / composition hints and
 *       the new system calls), returning the status a caller would see if
 *       the facility were present-but-unused.
 *
 * The behaviour of every shim is documented inline against the disassembly.
 */

#include <ntddk.h>
#include <intrin.h>

/* ------------------------------------------------------------------------- */
/* Object-header geometry (identical on Win7 and Win10 x64):                 */
/* the object body sits 0x30 bytes after the OBJECT_HEADER, whose first      */
/* field is the LONG_PTR PointerCount.                                       */
/* ------------------------------------------------------------------------- */
#define OBJECT_HEADER_TO_BODY_OFFSET 0x30

/* ========================================================================= */
/* Dynamically-resolved Windows 7 routines                                   */
/*                                                                           */
/* MmMapViewInSystemSpace / MmMapViewInSessionSpace are exported by every    */
/* supported ntoskrnl but are not declared in the WDK headers nor present in */
/* the WDK import library, so we resolve them at load time.                  */
/* ========================================================================= */

typedef NTSTATUS (NTAPI *PFN_MmMapViewInSystemSpace)(
    _In_  PVOID   Section,
    _Outptr_ PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize);

typedef NTSTATUS (NTAPI *PFN_MmMapViewInSessionSpace)(
    _In_  PVOID   Section,
    _Outptr_ PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize);

static PFN_MmMapViewInSystemSpace  g_MmMapViewInSystemSpace  = NULL;
static PFN_MmMapViewInSessionSpace g_MmMapViewInSessionSpace = NULL;

static PVOID
NtosWrapResolve(
    _In_ PCWSTR Name)
{
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, Name);
    return MmGetSystemRoutineAddress(&us);
}

/* ========================================================================= */
/* (a) Data exports                                                          */
/*                                                                           */
/* These are POBJECT_TYPE cells (declared PVOID here to avoid pulling in     */
/* ntifs.h). On Win10 they are populated at boot by the relevant subsystem;  */
/* those subsystems do not exist on Win7, so the cells stay NULL. A caller   */
/* that passes *ExXxxObjectType to Ob* will get a clean failure rather than  */
/* a bogus type pointer.                                                     */
/* ========================================================================= */

PVOID ExCompositionObjectType    = NULL;   /* RVA 0x3c41f8, dq 0 */
PVOID ExRawInputManagerObjectType = NULL;  /* RVA 0x3c41e8, dq 0 */
PVOID ExTimerObjectType          = NULL;   /* RVA 0x3c40b8, dq 0 */

/* ========================================================================= */
/* (b) Faithful re-implementations                                           */
/* ========================================================================= */

/*
 * ObReferenceObjectSafe
 *
 * Disasm: rcx = Object; header = Object - 0x30; read PointerCount; if it is
 * already zero the object is being torn down -> return FALSE; otherwise
 * interlocked-increment and return TRUE. This is a verbatim re-implementation
 * of that "reference only if still alive" primitive.
 */
BOOLEAN
ObReferenceObjectSafe(
    _In_ PVOID Object)
{
    volatile LONG64 *pointerCount =
        (volatile LONG64 *)((PUCHAR)Object - OBJECT_HEADER_TO_BODY_OFFSET);

    for (;;)
    {
        LONG64 current = *pointerCount;

        if (current == 0)
        {
            /* Object is dead / being deleted. */
            return FALSE;
        }

        if (InterlockedCompareExchange64(pointerCount,
                                         current + 1,
                                         current) == current)
        {
            return TRUE;
        }
        /* Lost the race; re-read and retry. */
    }
}

/*
 * _finite
 *
 * Disasm: returns 1 when (|bits| - 0x7ff0000000000000) has its sign bit set,
 * i.e. when the magnitude is strictly below +inf's bit pattern (finite,
 * non-NaN). Re-implemented by bit inspection.
 */
int
_finite(double x)
{
    union { double d; unsigned __int64 u; } v;
    v.d = x;
    v.u &= 0x7fffffffffffffffULL;              /* strip sign */
    return (v.u < 0x7ff0000000000000ULL) ? 1 : 0;
}

/*
 * iswdigit
 *
 * Disasm jumps into the wide ctype helper with the _DIGIT (0x4) mask. In the
 * C locale that is exactly the ASCII decimal digits.
 */
int
iswdigit(wchar_t c)
{
    return (c >= L'0' && c <= L'9') ? 1 : 0;
}

/*
 * sqrt / sqrtf
 *
 * Disasm is the MSVCRT fast path: sqrtsd / sqrtss over the argument, with an
 * exception-raising slow path for domain errors. The SSE2 instruction already
 * yields the correct IEEE result (incl. NaN for negative input), so the fast
 * path alone is sufficient for a kernel shim.
 */
double
sqrt(double x)
{
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(x)));
}

float
sqrtf(float x)
{
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
}

/* ========================================================================= */
/* (b) Forwarders                                                            */
/* ========================================================================= */

/*
 * MmMapViewInSystemSpaceEx
 *
 * Disasm forwards to the common internal mapper, passing a section offset and
 * size in addition to the base/size. Windows 7 only offers the 3-argument
 * MmMapViewInSystemSpace (offset is always 0). We forward to it; a non-zero
 * SectionOffset cannot be honoured and is reported as unsupported.
 */
NTSTATUS
MmMapViewInSystemSpaceEx(
    _In_      PVOID          Section,
    _Outptr_  PVOID         *MappedBase,
    _Inout_   PSIZE_T        ViewSize,
    _In_opt_  PLARGE_INTEGER SectionOffset,
    _In_      ULONG          Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (SectionOffset != NULL && SectionOffset->QuadPart != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (g_MmMapViewInSystemSpace == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    return g_MmMapViewInSystemSpace(Section, MappedBase, ViewSize);
}

/*
 * MmMapViewInSessionSpaceEx
 *
 * As above, forwarding to the 3-argument Win7 MmMapViewInSessionSpace.
 */
NTSTATUS
MmMapViewInSessionSpaceEx(
    _In_      PVOID          Section,
    _Outptr_  PVOID         *MappedBase,
    _Inout_   PSIZE_T        ViewSize,
    _In_opt_  PLARGE_INTEGER SectionOffset,
    _In_      ULONG          Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (SectionOffset != NULL && SectionOffset->QuadPart != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (g_MmMapViewInSessionSpace == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    return g_MmMapViewInSessionSpace(Section, MappedBase, ViewSize);
}

/*
 * ObWaitForSingleObject
 *
 * Disasm: reference the handle for SYNCHRONIZE, KeWaitForSingleObject on the
 * referenced object, dereference. That is exactly ZwWaitForSingleObject, which
 * Windows 7 exports. Argument order follows the disassembly
 * (Handle, WaitMode, Alertable, Timeout); only Alertable/Timeout are
 * meaningful to the Zw form.
 */
NTSTATUS
ObWaitForSingleObject(
    _In_      HANDLE         Handle,
    _In_      KPROCESSOR_MODE WaitMode,
    _In_      BOOLEAN        Alertable,
    _In_opt_  PLARGE_INTEGER Timeout)
{
    UNREFERENCED_PARAMETER(WaitMode);
    return ZwWaitForSingleObject(Handle, Alertable, Timeout);
}

/*
 * ObWaitForMultipleObjects
 *
 * Disasm references each handle in turn and performs a KeWaitForMultipleObjects
 * style wait. ZwWaitForMultipleObjects provides the equivalent handle-based
 * wait on Windows 7.
 */
NTSTATUS
ObWaitForMultipleObjects(
    _In_      ULONG          Count,
    _In_      PHANDLE        Handles,
    _In_      WAIT_TYPE      WaitType,
    _In_      KPROCESSOR_MODE WaitMode,
    _In_      BOOLEAN        Alertable,
    _In_opt_  PLARGE_INTEGER Timeout)
{
    UNREFERENCED_PARAMETER(WaitMode);
    return ZwWaitForMultipleObjects(Count,
                                    Handles,
                                    WaitType,
                                    Alertable,
                                    Timeout);
}

/* ========================================================================= */
/* (c) Sensible-default shims for Win8/10-only facilities                    */
/* ========================================================================= */

/*
 * ExQueryFastCacheDevLicense
 *
 * Disasm queries a licence value ("BD") and returns a BOOLEAN. No such licence
 * exists on Windows 7; report "not licensed".
 */
BOOLEAN
ExQueryFastCacheDevLicense(VOID)
{
    return FALSE;
}

/*
 * ExShareAddressSpaceWithDevice
 *
 * Win10-only cross-device address-space sharing (SVM). Unavailable on Win7.
 */
NTSTATUS
ExShareAddressSpaceWithDevice(
    _In_ PVOID DeviceObject,
    _In_ PVOID Process,
    _In_ PVOID Parameters)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Parameters);
    return STATUS_NOT_SUPPORTED;
}

/*
 * ExUnsubscribeWnfStateChange
 *
 * The Windows Notification Facility does not exist on Windows 7, so there is
 * never an active subscription to tear down; report success.
 */
NTSTATUS
ExUnsubscribeWnfStateChange(
    _In_ PVOID Subscription)
{
    UNREFERENCED_PARAMETER(Subscription);
    return STATUS_SUCCESS;
}

/*
 * MmPrefetchVirtualAddresses
 *
 * Prefetching is purely advisory. Treating it as a successful no-op is
 * behaviourally correct.
 */
NTSTATUS
MmPrefetchVirtualAddresses(
    _In_ PVOID PrefetchInformation)
{
    UNREFERENCED_PARAMETER(PrefetchInformation);
    return STATUS_SUCCESS;
}

/*
 * ObDuplicateObject
 *
 * The genuine routine performs cross-process handle duplication through
 * internal object-manager helpers that are not reachable from a driver on
 * Windows 7. Report "not implemented" and null the output handle so callers
 * never consume an uninitialised value.
 */
NTSTATUS
ObDuplicateObject(
    _In_      PEPROCESS       SourceProcess,
    _In_      HANDLE          SourceHandle,
    _In_opt_  PEPROCESS       TargetProcess,
    _Out_opt_ PHANDLE         TargetHandle,
    _In_      ACCESS_MASK     DesiredAccess,
    _In_      ULONG           HandleAttributes,
    _In_      ULONG           Options,
    _In_      KPROCESSOR_MODE PreviousMode)
{
    UNREFERENCED_PARAMETER(SourceProcess);
    UNREFERENCED_PARAMETER(SourceHandle);
    UNREFERENCED_PARAMETER(TargetProcess);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(HandleAttributes);
    UNREFERENCED_PARAMETER(Options);
    UNREFERENCED_PARAMETER(PreviousMode);

    if (TargetHandle != NULL)
    {
        *TargetHandle = NULL;
    }
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * PoLatencySensitivityHint / PoNotifyVSyncChange / PoSetUserPresent /
 * PoUserShutdownCancelled
 *
 * Win8/10 power-policy hints. They have no backing mechanism on Windows 7 and
 * simply return on that platform in their degenerate paths; implement as
 * no-ops.
 */
VOID
PoLatencySensitivityHint(
    _In_ ULONG Hint)
{
    UNREFERENCED_PARAMETER(Hint);
}

VOID
PoNotifyVSyncChange(
    _In_ BOOLEAN VSyncActive)
{
    UNREFERENCED_PARAMETER(VSyncActive);
}

VOID
PoSetUserPresent(
    _In_ ULONG UserPresent)
{
    UNREFERENCED_PARAMETER(UserPresent);
}

VOID
PoUserShutdownCancelled(
    _In_ BOOLEAN Cancelled)
{
    UNREFERENCED_PARAMETER(Cancelled);
}

/*
 * PsChargeProcessWakeCounter / PsReleaseProcessWakeCounter
 *
 * Process wake counters gate connected-standby wake charges, a concept absent
 * from Windows 7. The disassembled charge path returns STATUS_SUCCESS when the
 * process has no associated job/counter, which is always the case here.
 */
NTSTATUS
PsChargeProcessWakeCounter(
    _In_ PEPROCESS Process,
    _In_ ULONG     WakeReason,
    _In_ ULONG     Count,
    _In_opt_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(WakeReason);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Reserved);
    return STATUS_SUCCESS;
}

VOID
PsReleaseProcessWakeCounter(
    _In_ PVOID WakeReference)
{
    UNREFERENCED_PARAMETER(WakeReference);
}

/*
 * PsGetProcessCommonJob
 *
 * Returns the shared job object of a process pair. The genuine routine walks
 * Win10 EPROCESS offsets (+0x3b0 -> +0x430) that are not valid on Windows 7,
 * so re-implementing against them would read garbage. Report "no common job".
 */
PVOID
PsGetProcessCommonJob(
    _In_ PEPROCESS Process)
{
    UNREFERENCED_PARAMETER(Process);
    return NULL;
}

/*
 * PsIsProtectedProcessLight
 *
 * Protected Process Light was introduced in Windows 8.1. No process on
 * Windows 7 is PPL.
 */
BOOLEAN
PsIsProtectedProcessLight(
    _In_ PEPROCESS Process)
{
    UNREFERENCED_PARAMETER(Process);
    return FALSE;
}

/*
 * PsUpdateComponentPower
 *
 * PoFx component power accounting; no-op on Windows 7 (the disassembled path
 * returns immediately when no PoFx device is registered).
 */
VOID
PsUpdateComponentPower(
    _In_ PVOID Component)
{
    UNREFERENCED_PARAMETER(Component);
}

/*
 * RtlAvlInsertNodeEx / RtlAvlRemoveNode
 *
 * These manipulate RTL_BALANCED_NODE AVL trees via internal balancing helpers.
 * A partial re-implementation would risk corrupting a caller's tree, so these
 * are intentionally left as non-functional shims until a full, verified AVL
 * port is available. Callers that merely load the import will still resolve.
 */
VOID
RtlAvlInsertNodeEx(
    _In_ PVOID Table,
    _In_ PVOID ParentNode,
    _In_ BOOLEAN Right,
    _In_ PVOID Node)
{
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(ParentNode);
    UNREFERENCED_PARAMETER(Right);
    UNREFERENCED_PARAMETER(Node);
}

VOID
RtlAvlRemoveNode(
    _In_ PVOID Table,
    _In_ PVOID Node)
{
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(Node);
}

/*
 * RtlCapabilityCheck
 *
 * AppContainer capability checks. Windows 7 has no capabilities, so no token
 * holds any; report "capability absent" while returning success for the check
 * itself.
 */
NTSTATUS
RtlCapabilityCheck(
    _In_opt_ HANDLE           TokenHandle,
    _In_     PUNICODE_STRING  CapabilityName,
    _Out_    PBOOLEAN         HasCapability)
{
    UNREFERENCED_PARAMETER(TokenHandle);
    UNREFERENCED_PARAMETER(CapabilityName);

    if (HasCapability != NULL)
    {
        *HasCapability = FALSE;
    }
    return STATUS_SUCCESS;
}

/*
 * RtlCheckTokenMembership
 *
 * The genuine routine resolves this through SeTokenIsAdmin-style helpers that
 * are not reachable here. Report "not a member" (the safe, access-denying
 * default) with a successful status.
 */
NTSTATUS
RtlCheckTokenMembership(
    _In_opt_ HANDLE   TokenHandle,
    _In_     PSID     SidToCheck,
    _Out_    PUCHAR   IsMember)
{
    UNREFERENCED_PARAMETER(TokenHandle);
    UNREFERENCED_PARAMETER(SidToCheck);

    if (IsMember != NULL)
    {
        *IsMember = FALSE;
    }
    return STATUS_SUCCESS;
}

/*
 * RtlIsMultiSessionSku
 *
 * Reads a KUSER_SHARED_DATA SKU flag whose offset (0x2f0) is Win10-specific.
 * Windows 7 client is not a multi-session SKU; report FALSE rather than risk
 * reading an unrelated Win7 shared-data field.
 */
BOOLEAN
RtlIsMultiSessionSku(VOID)
{
    return FALSE;
}

/*
 * RtlQueryPackageClaims
 *
 * Package (AppX) claims do not exist on Windows 7.
 */
NTSTATUS
RtlQueryPackageClaims(
    _In_      HANDLE  TokenHandle,
    _Out_opt_ PVOID   PackageFullName,
    _Inout_opt_ PVOID PackageSize,
    _Out_opt_ PVOID   AppId,
    _Inout_opt_ PVOID AppIdSize,
    _Out_opt_ PVOID   Pkg,
    _Out_opt_ PVOID   Flags,
    _Out_opt_ PVOID   Attributes)
{
    UNREFERENCED_PARAMETER(TokenHandle);
    UNREFERENCED_PARAMETER(PackageFullName);
    UNREFERENCED_PARAMETER(PackageSize);
    UNREFERENCED_PARAMETER(AppId);
    UNREFERENCED_PARAMETER(AppIdSize);
    UNREFERENCED_PARAMETER(Pkg);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Attributes);
    return STATUS_NOT_FOUND;
}

/*
 * SeIsParentOfChildAppContainer
 *
 * AppContainers do not exist on Windows 7.
 */
BOOLEAN
SeIsParentOfChildAppContainer(
    _In_ ULONG ParentPackageSidCount,
    _In_ PVOID ParentPackageSid,
    _In_ PVOID ChildPackageSid)
{
    UNREFERENCED_PARAMETER(ParentPackageSidCount);
    UNREFERENCED_PARAMETER(ParentPackageSid);
    UNREFERENCED_PARAMETER(ChildPackageSid);
    return FALSE;
}

/*
 * ZwAssociateWaitCompletionPacket / ZwCreateWaitCompletionPacket /
 * ZwSetInformationVirtualMemory / ZwUnlockVirtualMemory
 *
 * These are thin syscall thunks (disasm sets a Win10 service index and traps).
 * The corresponding system services do not exist in the Windows 7 SSDT, so the
 * calls cannot be forwarded; report "not implemented".
 */
NTSTATUS
ZwAssociateWaitCompletionPacket(
    _In_ HANDLE   WaitCompletionPacketHandle,
    _In_ HANDLE   IoCompletionHandle,
    _In_ HANDLE   TargetObjectHandle,
    _In_opt_ PVOID KeyContext,
    _In_opt_ PVOID ApcContext,
    _In_ NTSTATUS IoStatus,
    _In_ ULONG_PTR IoStatusInformation,
    _Out_opt_ PBOOLEAN AlreadySignaled)
{
    UNREFERENCED_PARAMETER(WaitCompletionPacketHandle);
    UNREFERENCED_PARAMETER(IoCompletionHandle);
    UNREFERENCED_PARAMETER(TargetObjectHandle);
    UNREFERENCED_PARAMETER(KeyContext);
    UNREFERENCED_PARAMETER(ApcContext);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(IoStatusInformation);

    if (AlreadySignaled != NULL)
    {
        *AlreadySignaled = FALSE;
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
ZwCreateWaitCompletionPacket(
    _Out_    PHANDLE     WaitCompletionPacketHandle,
    _In_     ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);

    if (WaitCompletionPacketHandle != NULL)
    {
        *WaitCompletionPacketHandle = NULL;
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
ZwSetInformationVirtualMemory(
    _In_ HANDLE    ProcessHandle,
    _In_ ULONG     VmInformationClass,
    _In_ ULONG_PTR NumberOfEntries,
    _In_ PVOID     VirtualAddresses,
    _In_ PVOID     VmInformation,
    _In_ ULONG     VmInformationLength)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(VmInformationClass);
    UNREFERENCED_PARAMETER(NumberOfEntries);
    UNREFERENCED_PARAMETER(VirtualAddresses);
    UNREFERENCED_PARAMETER(VmInformation);
    UNREFERENCED_PARAMETER(VmInformationLength);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
ZwUnlockVirtualMemory(
    _In_    HANDLE  ProcessHandle,
    _Inout_ PVOID  *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_    ULONG   MapType)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(RegionSize);
    UNREFERENCED_PARAMETER(MapType);
    return STATUS_NOT_IMPLEMENTED;
}

/* ========================================================================= */
/* Driver plumbing                                                           */
/* ========================================================================= */

DRIVER_UNLOAD DriverUnload;
DRIVER_INITIALIZE DriverEntry;

VOID
DriverUnload(
    PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(RegistryPath);

    /* Resolve the Win7 routines used by the Mm*Ex forwarders. */
    g_MmMapViewInSystemSpace =
        (PFN_MmMapViewInSystemSpace)NtosWrapResolve(L"MmMapViewInSystemSpace");
    g_MmMapViewInSessionSpace =
        (PFN_MmMapViewInSessionSpace)NtosWrapResolve(L"MmMapViewInSessionSpace");

    DriverObject->DriverUnload = DriverUnload;
    return STATUS_SUCCESS;
}
