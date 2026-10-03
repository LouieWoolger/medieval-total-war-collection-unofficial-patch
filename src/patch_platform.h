#ifndef MTW_PATCH_PLATFORM_H
#define MTW_PATCH_PLATFORM_H

/* C99 ownership primitives. A zero-initialized context is ready to use.
   Error code/message pointers are borrowed immutable strings with static lifetime.
   Resources are released in reverse acquisition order; cleanup never deletes files. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>

#define PATCH_CONTEXT_DEFAULT_LIMIT ((size_t)64 * 1024 * 1024)

typedef struct {
    const char *code;
    const char *message;
    DWORD win32;
} PatchError;

typedef struct {
    void *user;
    void *(*allocate)(void *user, size_t bytes);
    void (*deallocate)(void *user, void *value);
} PatchAllocator;

/* Return ERROR_SUCCESS or an already captured Win32 failure. Must not reenter
   this context. The callback and its dependencies must outlive the resource. */
typedef DWORD (*PatchCleanupFn)(void *value);

typedef struct PatchResource {
    struct PatchResource *next;
    void *value;
    PatchCleanupFn cleanup;
    size_t reserved_bytes;
    size_t payload_bytes;
    /* Preserve malloc's alignment for an immediately following C99 payload. */
    union { long double number; uint64_t integer; void *pointer; } alignment;
} PatchResource;

typedef struct {
    PatchError error;
    PatchAllocator allocator;
    PatchResource *resources;
    size_t bytes_owned;
    /* Zero selects PATCH_CONTEXT_DEFAULT_LIMIT. Includes ownership metadata.
       Configure allocator/limit before acquisition, then leave them unchanged. */
    size_t memory_limit;
} PatchContext;

static inline void patch_error_set(PatchError *error, const char *code,
                                   const char *message, DWORD win32)
{
    if (error && !error->code) {
        error->code = code;
        error->message = message;
        error->win32 = win32;
    }
    /* The first error remains the operation result. GetLastError also exposes
       this immediate failure to callers which need an independent error (logs).
       All cleanup helpers save and restore the incoming last-error value. */
    SetLastError(win32);
}

static inline int patch_size_add(size_t left, size_t right, size_t *result)
{
    if (!result) return 0;
    *result = 0;
    if (left > SIZE_MAX - right) return 0;
    *result = left + right;
    return 1;
}

static inline int patch_size_multiply(size_t count, size_t size, size_t *result)
{
    if (!result) return 0;
    *result = 0;
    if (size && count > SIZE_MAX / size) return 0;
    *result = count * size;
    return 1;
}

static inline PatchResource *patch_context_new_resource(PatchContext *context, size_t payload)
{
    size_t reserved, total, limit;
    PatchResource *resource;
    if (!context) return NULL;
    if (!!context->allocator.allocate != !!context->allocator.deallocate) {
        patch_error_set(&context->error, "invalid_argument", "Allocator callbacks must be paired.",
                        ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!patch_size_add(sizeof(PatchResource), payload, &reserved) ||
        !patch_size_add(context->bytes_owned, reserved, &total)) {
        patch_error_set(&context->error, "allocation_overflow", "Allocation size overflow.",
                        ERROR_ARITHMETIC_OVERFLOW);
        return NULL;
    }
    limit = context->memory_limit ? context->memory_limit : PATCH_CONTEXT_DEFAULT_LIMIT;
    if (total > limit) {
        patch_error_set(&context->error, "allocation_limit", "Operation memory limit exceeded.",
                        ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    resource = (PatchResource *)(context->allocator.allocate ?
        context->allocator.allocate(context->allocator.user, reserved) : malloc(reserved));
    if (!resource) {
        patch_error_set(&context->error, "allocation_failed", "Could not allocate operation memory.",
                        ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memset(resource, 0, reserved);
    resource->reserved_bytes = reserved;
    resource->payload_bytes = payload;
    resource->next = context->resources;
    context->resources = resource;
    context->bytes_owned = total;
    return resource;
}

/* On failure *output is NULL. Zero bytes succeed without acquiring anything. */
static inline int patch_context_alloc(PatchContext *context, size_t count,
                                      size_t size, void **output)
{
    size_t bytes;
    PatchResource *resource;
    if (output) *output = NULL;
    if (!context) return 0;
    if (!output) {
        patch_error_set(&context->error, "invalid_argument", "Allocation output is required.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_size_multiply(count, size, &bytes)) {
        patch_error_set(&context->error, "allocation_overflow", "Array allocation size overflow.",
                        ERROR_ARITHMETIC_OVERFLOW);
        return 0;
    }
    if (!bytes) return 1;
    resource = patch_context_new_resource(context, bytes);
    if (!resource) return 0;
    resource->value = resource + 1;
    *output = resource->value;
    return 1;
}

static inline void patch_context_delete_resource(PatchContext *context, PatchResource *resource)
{
    context->bytes_owned -= resource->reserved_bytes;
    if (context->allocator.deallocate)
        context->allocator.deallocate(context->allocator.user, resource);
    else
        free(resource);
}

/* Takes ownership even when resource bookkeeping allocation fails: cleanup is
   called on that failure before returning. *output is a borrowed release token. */
static inline int patch_context_take(PatchContext *context, void *value,
                                     PatchCleanupFn cleanup, PatchResource **output)
{
    PatchResource *resource = NULL;
    if (output) *output = NULL;
    if (!cleanup) {
        if (context) patch_error_set(&context->error, "invalid_argument", "Cleanup callback is required.",
                                     ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (context) resource = patch_context_new_resource(context, 0);
    if (!resource) {
        DWORD saved = GetLastError();
        cleanup(value);
        SetLastError(saved);
        return 0;
    }
    resource->value = value;
    resource->cleanup = cleanup;
    if (output) *output = resource;
    return 1;
}

static inline DWORD patch_close_handle(void *value)
{
    return CloseHandle((HANDLE)value) ? ERROR_SUCCESS : GetLastError();
}

static inline int patch_context_take_handle(PatchContext *context, HANDLE handle,
                                            PatchResource **output)
{
    if (output) *output = NULL;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DWORD native = GetLastError();
        if (context) patch_error_set(&context->error, "io_error", "A valid acquired handle is required.",
                                     native ? native : ERROR_INVALID_HANDLE);
        return 0;
    }
    return patch_context_take(context, handle, patch_close_handle, output);
}

static inline DWORD patch_close_owned_mutex(void *value)
{
    DWORD native = ERROR_SUCCESS;
    if (!ReleaseMutex((HANDLE)value)) native = GetLastError();
    if (!CloseHandle((HANDLE)value) && !native) native = GetLastError();
    return native;
}

/* Transfer a mutex which this thread owns exactly once, and close the context
   on this same thread. An unowned mutex uses take_handle, not this function. */
static inline int patch_context_take_mutex(PatchContext *context, HANDLE handle,
                                           PatchResource **output)
{
    if (output) *output = NULL;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DWORD native = GetLastError();
        if (context) patch_error_set(&context->error, "io_error", "A valid owned mutex is required.",
                                     native ? native : ERROR_INVALID_HANDLE);
        return 0;
    }
    return patch_context_take(context, handle, patch_close_owned_mutex, output);
}

static inline int patch_context_release(PatchContext *context, PatchResource *resource)
{
    PatchResource **link;
    DWORD native = ERROR_SUCCESS, saved = GetLastError();
    if (!context) return 0;
    if (!resource) return 1;
    for (link = &context->resources; *link && *link != resource; link = &(*link)->next) {}
    if (!*link) {
        patch_error_set(&context->error, "invalid_argument", "Resource does not belong to this context.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    *link = resource->next;
    if (resource->cleanup) native = resource->cleanup(resource->value);
    patch_context_delete_resource(context, resource);
    if (native) patch_error_set(&context->error, "io_error", "Resource cleanup failed.", native);
    SetLastError(saved);
    return native == ERROR_SUCCESS;
}

static inline int patch_context_free(PatchContext *context, void *value)
{
    PatchResource *resource;
    if (!context) return 0;
    if (!value) return 1;
    for (resource = context->resources; resource; resource = resource->next)
        if (!resource->cleanup && resource->value == value)
            return patch_context_release(context, resource);
    patch_error_set(&context->error, "invalid_argument", "Buffer does not belong to this context.",
                    ERROR_INVALID_PARAMETER);
    return 0;
}

/* Growth allocates first, so failure preserves the old buffer and its contents.
   Both allocations count against the limit while they coexist. */
static inline int patch_context_resize(PatchContext *context, void **value,
                                       size_t count, size_t size)
{
    PatchResource *resource;
    size_t bytes;
    void *replacement;
    if (!context) return 0;
    if (!value) {
        patch_error_set(&context->error, "invalid_argument", "Resize output is required.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!*value) return patch_context_alloc(context, count, size, value);
    for (resource = context->resources; resource; resource = resource->next)
        if (!resource->cleanup && resource->value == *value) break;
    if (!resource) {
        patch_error_set(&context->error, "invalid_argument", "Buffer does not belong to this context.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_size_multiply(count, size, &bytes)) {
        patch_error_set(&context->error, "allocation_overflow", "Array allocation size overflow.",
                        ERROR_ARITHMETIC_OVERFLOW);
        return 0;
    }
    if (!bytes) {
        patch_context_release(context, resource);
        *value = NULL;
        return 1;
    }
    if (!patch_context_alloc(context, count, size, &replacement)) return 0;
    memcpy(replacement, *value, bytes < resource->payload_bytes ? bytes : resource->payload_bytes);
    patch_context_release(context, resource);
    *value = replacement;
    return 1;
}

static inline void patch_context_close(PatchContext *context)
{
    DWORD saved = GetLastError();
    if (!context) return;
    while (context->resources) patch_context_release(context, context->resources);
    SetLastError(saved);
}

/* Optional APIs never appear in the import table. A capability set is immutable
   while any guard using it is open. Tests may model an older OS by removing an
   optional function before opening the guard. */
typedef DWORD (WINAPI *PatchFinalPathFn)(HANDLE, LPWSTR, DWORD, DWORD);
typedef BOOL (WINAPI *PatchSetInfoFn)(HANDLE, int, LPVOID, DWORD);
typedef int (WINAPI *PatchCompareOrdinalFn)(LPCWSTR, int, LPCWSTR, int, BOOL);
typedef WCHAR (NTAPI *PatchUpcaseFn)(WCHAR);
typedef struct { union { LONG status; PVOID pointer; } value; ULONG_PTR information; } PatchIoStatus;
typedef LONG (NTAPI *PatchNtSetInfoFn)(HANDLE, PatchIoStatus *, PVOID, ULONG, int);
typedef ULONG (WINAPI *PatchNtErrorFn)(LONG);
typedef BOOL (WINAPI *PatchQueryImageFn)(HANDLE, DWORD, LPWSTR, PDWORD);
typedef DWORD (WINAPI *PatchModuleImageFn)(HANDLE, HMODULE, LPWSTR, DWORD);
/* The original (non-Ex) native and Win32 rename layouts have identical field
   offsets. This definition deliberately does not depend on Vista declarations. */
typedef struct { BOOLEAN ReplaceIfExists; HANDLE RootDirectory; DWORD FileNameLength; WCHAR FileName[1]; } PatchRenameInfo;
typedef struct {
    int initialized;
    PatchFinalPathFn final_path;
    PatchSetInfoFn set_info;
    PatchCompareOrdinalFn compare_ordinal;
    PatchUpcaseFn upcase;
    PatchNtSetInfoFn nt_set_info;
    PatchNtErrorFn nt_error;
    PatchQueryImageFn query_image;
    PatchModuleImageFn module_image;
} PatchPlatform;

/* Avoid loading psapi from the game directory. Keep its reference alive only
   during the query; no module/resource ownership is hidden in PatchPlatform. */
static inline DWORD WINAPI patch_module_image(HANDLE process, HMODULE module, LPWSTR out, DWORD size)
{
    wchar_t system[MAX_PATH + 16];
    UINT n = GetSystemDirectoryW(system, MAX_PATH);
    HMODULE library;
    PatchModuleImageFn query = NULL;
    FARPROC proc;
    DWORD result = 0, saved;
    if (!n || n >= MAX_PATH) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    wcscat(system, L"\\psapi.dll");
    library = LoadLibraryW(system);
    if (!library) return 0;
    proc = GetProcAddress(library, "GetModuleFileNameExW");
    memcpy(&query, &proc, sizeof(query));
    if (query) result = query(process, module, out, size);
    else SetLastError(ERROR_PROC_NOT_FOUND);
    saved = GetLastError();
    FreeLibrary(library);
    SetLastError(saved);
    return result;
}

static inline void patch_platform_init(PatchPlatform *platform)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll"), nt = GetModuleHandleW(L"ntdll.dll");
    FARPROC proc;
    if (!platform) return;
    memset(platform, 0, sizeof(*platform));
#define PATCH_RESOLVE(member, library, name) do { \
    proc = (library) ? GetProcAddress((library), (name)) : NULL; \
    memcpy(&platform->member, &proc, sizeof(platform->member)); \
} while (0)
    PATCH_RESOLVE(final_path, kernel, "GetFinalPathNameByHandleW");
    PATCH_RESOLVE(set_info, kernel, "SetFileInformationByHandle");
    PATCH_RESOLVE(compare_ordinal, kernel, "CompareStringOrdinal");
    PATCH_RESOLVE(upcase, nt, "RtlUpcaseUnicodeChar");
    PATCH_RESOLVE(nt_set_info, nt, "NtSetInformationFile");
    PATCH_RESOLVE(nt_error, nt, "RtlNtStatusToDosError");
    PATCH_RESOLVE(query_image, kernel, "QueryFullProcessImageNameW");
#undef PATCH_RESOLVE
    platform->module_image = patch_module_image;
    platform->initialized = 1;
}

static inline int patch_alloc(PatchContext *context, size_t count, size_t size, void **out, PatchError *error)
{
    if (patch_context_alloc(context, count, size, out)) return 1;
    if (context && context->error.code)
        patch_error_set(error, context->error.code, context->error.message, GetLastError());
    else patch_error_set(error, "invalid_argument", "An allocation context is required.", ERROR_INVALID_PARAMETER);
    return 0;
}

/* Manual scalar validation avoids the different invalid-sequence behaviour of
   the XP UTF conversion APIs, and never uses locale-dependent CRT conversion. */
static inline int patch_utf8_scalar(const unsigned char *bytes, size_t size, size_t *pos, uint32_t *scalar)
{
    unsigned count, i; uint32_t value, minimum; unsigned char first;
    if (*pos >= size) return 0;
    first = bytes[(*pos)++];
    if (first < 0x80) { *scalar = first; return 1; }
    if (first >= 0xc2 && first <= 0xdf) { count=1; value=first&31; minimum=0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count=2; value=first&15; minimum=0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count=3; value=first&7; minimum=0x10000; }
    else return 0;
    if (size-*pos < count) return 0;
    for (i=0; i<count; ++i) { unsigned char b=bytes[(*pos)++]; if ((b&0xc0)!=0x80) return 0; value=(value<<6)|(b&63); }
    if (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return 0;
    *scalar=value; return 1;
}
static inline int patch_wide_valid(const wchar_t *text, size_t size)
{
    size_t i;
    if (!text && size) return 0;
    for (i=0; i<size; ++i) {
        unsigned c=text[i];
        if (c>=0xd800 && c<=0xdbff) { if (++i>=size || text[i]<0xdc00 || text[i]>0xdfff) return 0; }
        else if (c>=0xdc00 && c<=0xdfff) return 0;
    }
    return 1;
}
static inline int patch_utf8_to_wide(PatchContext *context, const char *text, size_t size, wchar_t **output, PatchError *error)
{
    size_t pos=0, count=0, capacity; uint32_t scalar; void *memory=NULL; wchar_t *out;
    if (output) *output=NULL;
    if (!output || (!text && size)) goto invalid;
    while (pos<size) {
        if (!patch_utf8_scalar((const unsigned char *)text,size,&pos,&scalar)) goto invalid;
        if (!patch_size_add(count,scalar>0xffff?2:1,&count)) goto invalid;
    }
    if (!patch_size_add(count,1,&capacity) || !patch_alloc(context,capacity,sizeof(wchar_t),&memory,error)) return 0;
    out=(wchar_t *)memory; pos=0; count=0;
    while (pos<size) {
        patch_utf8_scalar((const unsigned char *)text,size,&pos,&scalar);
        if (scalar>0xffff) { scalar-=0x10000;out[count++]=(wchar_t)(0xd800+(scalar>>10));out[count++]=(wchar_t)(0xdc00+(scalar&1023)); }
        else out[count++]=(wchar_t)scalar;
    }
    *output=out; return 1;
invalid:
    patch_error_set(error,"receipt_invalid","Invalid UTF-8 string.",ERROR_NO_UNICODE_TRANSLATION); return 0;
}
static inline int patch_wide_to_utf8(PatchContext *context, const wchar_t *text, size_t size,
                                    char **output, size_t *length, PatchError *error)
{
    size_t i,n=0,capacity; void *memory=NULL; char *out;
    if(output)*output=NULL;
    if(length)*length=0;
    if(!output || !length || !patch_wide_valid(text,size)) {
        patch_error_set(error,"receipt_invalid","Invalid Unicode string.",ERROR_NO_UNICODE_TRANSLATION);return 0;
    }
    if(!patch_size_multiply(size,3,&capacity)||!patch_size_add(capacity,1,&capacity)) {
        patch_error_set(error,"allocation_overflow","Unicode output size overflow.",ERROR_ARITHMETIC_OVERFLOW);return 0;
    }
    if(!patch_alloc(context,capacity,1,&memory,error))return 0;
    out=(char *)memory;
    for(i=0;i<size;++i){uint32_t c=text[i];
        if(c>=0xd800&&c<=0xdbff){c=0x10000+((c-0xd800)<<10)+(text[++i]-0xdc00);}
        if(c<0x80)out[n++]=(char)c;
        else if(c<0x800){out[n++]=(char)(0xc0|(c>>6));out[n++]=(char)(0x80|(c&63));}
        else if(c<0x10000){out[n++]=(char)(0xe0|(c>>12));out[n++]=(char)(0x80|((c>>6)&63));out[n++]=(char)(0x80|(c&63));}
        else {out[n++]=(char)(0xf0|(c>>18));out[n++]=(char)(0x80|((c>>12)&63));out[n++]=(char)(0x80|((c>>6)&63));out[n++]=(char)(0x80|(c&63));}
    }
    out[n]=0;*output=out;*length=n;return 1;
}
static inline int patch_wide_copy(PatchContext *context, const wchar_t *text, wchar_t **output, PatchError *error)
{
    size_t n; void *memory=NULL;
    if(output)*output=NULL;
    if(!text||!output){patch_error_set(error,"invalid_argument","Unicode input and output are required.",ERROR_INVALID_PARAMETER);return 0;}
    n=wcslen(text);
    if(!patch_alloc(context,n+1,sizeof(wchar_t),&memory,error))return 0;
    memcpy(memory,text,(n+1)*sizeof(wchar_t));*output=(wchar_t *)memory;return 1;
}
static inline int patch_path_equal_n(const PatchPlatform *platform, const wchar_t *a, size_t an,
                                     const wchar_t *b, size_t bn, int *equal, PatchError *error)
{
    size_t i;
    if(equal)*equal=0;
    if(!platform||!a||!b||!equal||an>INT_MAX||bn>INT_MAX){patch_error_set(error,"invalid_argument","Invalid ordinal comparison.",ERROR_INVALID_PARAMETER);return 0;}
    if(platform->compare_ordinal){int result=platform->compare_ordinal(a,(int)an,b,(int)bn,TRUE);
        if(!result){patch_error_set(error,"io_error","Ordinal comparison failed.",GetLastError());return 0;}
        *equal=result==CSTR_EQUAL;return 1;}
    if(!platform->upcase){patch_error_set(error,"platform_unsupported","Ordinal Unicode identity is unavailable.",ERROR_CALL_NOT_IMPLEMENTED);return 0;}
    if(an!=bn)return 1;
    for(i=0;i<an;++i)if(a[i]!=b[i]&&platform->upcase(a[i])!=platform->upcase(b[i]))return 1;
    *equal=1;return 1;
}
static inline int patch_path_equal(const PatchPlatform *platform, const wchar_t *a, const wchar_t *b,
                                   int *equal, PatchError *error)
{
    if(!a||!b){if(equal)*equal=0;patch_error_set(error,"invalid_argument","Paths are required.",ERROR_INVALID_PARAMETER);return 0;}
    return patch_path_equal_n(platform,a,wcslen(a),b,wcslen(b),equal,error);
}
static inline int patch_path_under(const PatchPlatform *platform, const wchar_t *path, const wchar_t *parent,
                                   int *under, PatchError *error)
{
    size_t n=wcslen(parent),length=wcslen(path);int same=0;*under=0;
    if(!patch_path_equal_n(platform,path,length<n?length:n,parent,n,&same,error))return 0;
    if(same&&length>n&&(parent[n-1]==L'\\'||path[n]==L'\\'))*under=1;
    return 1;
}
static inline int patch_path_normalize(PatchContext *context, const wchar_t *text, wchar_t **output, PatchError *error)
{
    wchar_t *out=NULL; size_t n,i,start; int ok=0;
    if(output)*output=NULL;
    if(!text||!output)goto invalid;
    n=wcslen(text);
    if(n<3||n>32700||!patch_wide_valid(text,n)||!((text[0]>=L'A'&&text[0]<=L'Z')||(text[0]>=L'a'&&text[0]<=L'z'))||text[1]!=L':'||(text[2]!=L'\\'&&text[2]!=L'/'))goto invalid;
    if(!patch_wide_copy(context,text,&out,error))return 0;
    for(i=0;i<n;++i)if(out[i]==L'/')out[i]=L'\\';
    if(out[0]>=L'a'&&out[0]<=L'z')out[0]-=32;
    if(n>3&&out[n-1]==L'\\'){out[--n]=0;if(out[n-1]==L'\\')goto invalid;}
    for(start=3;start<n;start=i+1){size_t len,stem;wchar_t device[8]={0};
        for(i=start;i<n&&out[i]!=L'\\';++i)if(out[i]<32||wcschr(L"<>:\"|?*",out[i]))goto invalid;
        len=i-start;if(!len||out[i-1]==L'.'||out[i-1]==L' ')goto invalid;
        for(stem=0;stem<len&&out[start+stem]!=L'.';++stem){}
        if(stem<8){size_t j;for(j=0;j<stem;++j){wchar_t c=out[start+j];device[j]=c>=L'a'&&c<=L'z'?c-32:c;}
            if(!wcscmp(device,L"CON")||!wcscmp(device,L"PRN")||!wcscmp(device,L"AUX")||!wcscmp(device,L"NUL")||!wcscmp(device,L"CONIN$")||!wcscmp(device,L"CONOUT$")||
               (stem==4&&(!wcsncmp(device,L"COM",3)||!wcsncmp(device,L"LPT",3))&&wcschr(L"123456789\u00b9\u00b2\u00b3",device[3])))goto invalid;}
    }
    *output=out;ok=1;
invalid:
    if(!ok){if(out)patch_context_free(context,out);patch_error_set(error,"unsafe_path","An unambiguous absolute local path is required.",ERROR_INVALID_NAME);}
    return ok;
}
static inline int patch_path_join(PatchContext *context, const wchar_t *root, const char *relative,
                                  wchar_t **output, PatchError *error)
{
    PatchContext local={0};wchar_t *r=NULL,*p=NULL,*joined=NULL;size_t count;void *memory=NULL;int ok=0;
    if(output)*output=NULL;
    local.allocator=context->allocator;local.memory_limit=context->memory_limit;
    if(!relative||!patch_utf8_to_wide(&local,relative,strlen(relative),&r,error))goto done;
    if(!r[0]||r[0]==L'\\'||r[0]==L'/'||wcschr(r,L':')){patch_error_set(error,"unsafe_path","A relative path is required.",ERROR_INVALID_NAME);goto done;}
    if(!patch_path_normalize(&local,root,&p,error))goto done;
    if(!patch_size_add(wcslen(p),wcslen(r),&count)||!patch_size_add(count,2,&count)||!patch_alloc(&local,count,sizeof(wchar_t),&memory,error))goto done;
    joined=(wchar_t *)memory;wcscpy(joined,p);if(joined[wcslen(joined)-1]!=L'\\')wcscat(joined,L"\\");wcscat(joined,r);
    ok=patch_path_normalize(context,joined,output,error);
done:patch_context_close(&local);return ok;
}
static inline int patch_invariant_lower(PatchContext *context, const wchar_t *text, wchar_t **output, PatchError *error)
{
    size_t length;int n,got;void *memory=NULL;
    if(output)*output=NULL;
    if(!text||!output||(length=wcslen(text))>INT_MAX||!patch_wide_valid(text,length))goto invalid;
    if(!length)return patch_wide_copy(context,L"",output,error);
    n=LCMapStringW(LOCALE_INVARIANT,LCMAP_LOWERCASE,text,(int)length,NULL,0);
    if(!n)goto invalid;
    if(!patch_alloc(context,(size_t)n+1,sizeof(wchar_t),&memory,error))return 0;
    got=LCMapStringW(LOCALE_INVARIANT,LCMAP_LOWERCASE,text,(int)length,(wchar_t *)memory,n);
    if(got!=n){patch_context_free(context,memory);goto invalid;}
    *output=(wchar_t *)memory;return 1;
invalid:patch_error_set(error,"unsafe_path","Could not canonicalize the installation identity.",GetLastError());return 0;
}
/* Unknown/inaccessible process identity remains potentially the selected game.
   Callers must treat failure as busy; *matches is true until identity is proven. */
static inline int patch_process_is_target(const PatchPlatform *platform, DWORD pid, const wchar_t *target,
                                          int *matches, PatchError *error)
{
    HANDLE process=NULL;wchar_t path[32768],device[1024],drive[3];DWORD n=32768,access;int ok=0;
    PatchContext context={0};PatchError attempt={0};wchar_t *normalized=NULL;
    if(matches)*matches=1;
    if(!platform||!target||!matches)goto unknown;
    access=platform->query_image?0x1000:(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ);
    process=OpenProcess(access,FALSE,pid);if(!process)goto unknown;
    if(platform->query_image){if(!platform->query_image(process,0,path,&n)||!n||n>=32768)goto unknown;path[n]=0;}
    else {if(!platform->module_image)goto unknown;n=platform->module_image(process,NULL,path,32768);if(!n||n>=32768)goto unknown;path[n]=0;}
    /* Image APIs can return the spelling used to start a process, including an
       extended prefix or 8.3 alias. Resolve that spelling before distinguishing
       two same-named games. An image which cannot be resolved stays busy. */
    if(!patch_path_normalize(&context,!wcsncmp(path,L"\\\\?\\",4)?path+4:path,&normalized,&attempt))goto unknown;
    drive[0]=normalized[0];drive[1]=L':';drive[2]=0;
    if(!QueryDosDeviceW(drive,device,1024)||wcsncmp(device,L"\\Device\\",8))goto unknown;
    wcscpy(path,L"\\\\?\\");wcscat(path,normalized);
    patch_context_free(&context,normalized);normalized=NULL;
    if(!patch_wide_copy(&context,path,&normalized,&attempt))goto unknown;
    n=GetLongPathNameW(normalized,path,32768);
    if(!n||n>=32768||wcsncmp(path,L"\\\\?\\",4))goto unknown;
    ok=patch_path_equal(platform,path+4,target,matches,&attempt);
    if(!ok)goto unknown;
    CloseHandle(process);patch_context_close(&context);return 1;
unknown:
    {DWORD saved=GetLastError();if(matches)*matches=1;if(process)CloseHandle(process);patch_context_close(&context);
     patch_error_set(error,"game_running","Could not establish the running game's identity.",saved?saved:ERROR_CALL_NOT_IMPLEMENTED);}
    return 0;
}

#endif
