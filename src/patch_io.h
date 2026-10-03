#ifndef MTW_PATCH_IO_H
#define MTW_PATCH_IO_H

/* UTF-8 diagnostic emission follows Shogun's patch_io.h at
   013d5ed285d0eadcbf22803b17a663fa8231c570 (GPL-3.0; see LICENSE).
   Adapted here for explicit per-operation ownership, checked output/log writes,
   durable flushes and independent diagnostic error reporting. No stdio macro
   replacement or global log state is used. Handles are borrowed from the caller. */
#include "patch_platform.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    PatchContext *context;
    HANDLE output;
    HANDLE log;
    PatchError error;
    int failed;
} PatchDiagnostics;

static inline void patch_diagnostics_init(PatchDiagnostics *diagnostics, PatchContext *context,
                                          HANDLE output, HANDLE log)
{
    memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->context = context;
    diagnostics->output = output;
    diagnostics->log = log;
}

static inline int patch_diagnostics_failure(PatchDiagnostics *diagnostics, DWORD native,
                                           const char *message)
{
    diagnostics->failed = 1;
    patch_error_set(&diagnostics->error, "diagnostic_write_failed", message, native);
    if (diagnostics->context)
        patch_error_set(&diagnostics->context->error, "diagnostic_write_failed", message, native);
    return 0;
}

static inline int patch_diagnostics_write(PatchDiagnostics *diagnostics, HANDLE file,
                                          const char *text, size_t length)
{
    while (length) {
        DWORD written = 0;
        DWORD chunk = length > 65536 ? 65536 : (DWORD)length;
        if (!WriteFile(file, text, chunk, &written, NULL)) {
            DWORD native = GetLastError();
            return patch_diagnostics_failure(diagnostics, native, "Diagnostic write failed.");
        }
        if (!written || written > chunk)
            return patch_diagnostics_failure(diagnostics, ERROR_WRITE_FAULT,
                                             "Diagnostic write made invalid progress.");
        text += written;
        length -= written;
    }
    return 1;
}

/* NULL means an absent sink; INVALID_HANDLE_VALUE is an invalid requested sink.
   Log handles must refer to writable ordinary files validated by the guard layer.
   This layer never opens a path, deletes a file, closes a handle or retries a
   failed diagnostic operation. Callers use failed to retain recovery evidence. */
static inline int patch_diagnostics_emit(PatchDiagnostics *diagnostics,
                                         const char *text, size_t length)
{
    if (!diagnostics || !diagnostics->context) return 0;
    if (diagnostics->failed) return 0;
    if (!text && length)
        return patch_diagnostics_failure(diagnostics, ERROR_INVALID_PARAMETER,
                                         "Diagnostic text is missing.");
    if (diagnostics->output && !patch_diagnostics_write(diagnostics, diagnostics->output, text, length))
        return 0;
    if (diagnostics->log) {
        if (diagnostics->log != diagnostics->output &&
            !patch_diagnostics_write(diagnostics, diagnostics->log, text, length)) return 0;
        if (!FlushFileBuffers(diagnostics->log)) {
            DWORD native = GetLastError();
            return patch_diagnostics_failure(diagnostics, native, "Persistent diagnostic flush failed.");
        }
    }
    return 1;
}

/* The format and %s arguments are UTF-8. For wide paths the guard/conversion
   layer must validate and convert explicitly before formatting. */
static inline int patch_diagnostics_printf(PatchDiagnostics *diagnostics, const char *format, ...)
{
    va_list arguments, measure;
    int length, rendered, ok = 0;
    size_t capacity;
    char *text = NULL;
    void *allocation = NULL;
    if (!diagnostics || !diagnostics->context || diagnostics->failed) return 0;
    if (!format) return patch_diagnostics_failure(diagnostics, ERROR_INVALID_PARAMETER,
                                                 "Diagnostic format is missing.");
    va_start(arguments, format);
    va_copy(measure, arguments);
    length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (length < 0 || !patch_size_add((size_t)length, 1, &capacity)) {
        patch_diagnostics_failure(diagnostics, ERROR_INVALID_DATA, "Diagnostic format failed.");
        goto cleanup;
    }
    if (!patch_context_alloc(diagnostics->context, capacity, 1, &allocation)) {
        DWORD native = GetLastError();
        patch_diagnostics_failure(diagnostics, native,
                                  "Diagnostic buffer allocation failed.");
        goto cleanup;
    }
    text = (char *)allocation;
    rendered = vsnprintf(text, capacity, format, arguments);
    if (rendered != length) {
        patch_diagnostics_failure(diagnostics, ERROR_INVALID_DATA, "Diagnostic format size changed.");
        goto cleanup;
    }
    ok = patch_diagnostics_emit(diagnostics, text, (size_t)length);
cleanup:
    va_end(arguments);
    patch_context_free(diagnostics->context, allocation);
    return ok;
}

#endif
