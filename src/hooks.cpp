// Small hooking helpers: import-table patch, Microsoft hot-patch, COM vtable patch.
#include "common.h"

void* PatchIAT(HMODULE module, const char* dll, const char* func, void* hook)
{
    BYTE* base = (BYTE*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((const char*)(base + imp->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        if (!imp->OriginalFirstThunk) {   // (a rebuilt exe without the name list: match the address instead)
            HMODULE m = GetModuleHandleA(dll);
            void* want = m ? (void*)GetProcAddress(m, func) : nullptr;
            for (; want && thunk->u1.Function; thunk++) {
                if ((void*)thunk->u1.Function != want) continue;
                DWORD old;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                thunk->u1.Function = (DWORD_PTR)hook;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                return want;
            }
            continue;
        }
        IMAGE_THUNK_DATA* names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        for (; names->u1.AddressOfData; names++, thunk++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, func) != 0) continue;
            void* prev = (void*)thunk->u1.Function;
            DWORD old;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            thunk->u1.Function = (DWORD_PTR)hook;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return prev;
        }
    }
    return nullptr;
}

// Windows system functions start with "mov edi, edi" and have 5 bytes of padding in
// front, reserved for exactly this: a long jump in the padding and an atomic 2-byte
// short jump over the mov. Safe even while other threads are calling the function.
bool HotPatch(void* target, void* hook, void** original)
{
    BYTE* p = (BYTE*)target;
    if (!p || p[0] != 0x8B || p[1] != 0xFF) return false;
    for (int i = 1; i <= 5; i++)
        if (p[-i] != 0xCC && p[-i] != 0x90) return false;
    DWORD old;
    if (!VirtualProtect(p - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return false;
    p[-5] = 0xE9;
    *(int32_t*)(p - 4) = (int32_t)((BYTE*)hook - p);
    *original = p + 2;   // skip the 2-byte no-op, continue with the real prologue
    *(volatile WORD*)p = 0xF9EB;   // jmp short -7 -> the long jump
    VirtualProtect(p - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p - 5, 7);
    return true;
}

void* PatchVTable(void* object, int index, void* hook)
{
    void** vt = *(void***)object;
    void* prev = vt[index];
    if (prev == hook) return prev;
    DWORD old;
    VirtualProtect(&vt[index], sizeof(void*), PAGE_READWRITE, &old);
    vt[index] = hook;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
    return prev;
}
