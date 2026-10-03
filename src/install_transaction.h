#ifndef MTW_INSTALL_TRANSACTION_H
#define MTW_INSTALL_TRANSACTION_H
/* Medieval's durable v2 transaction protocol, coordinated in C99.
   Copyright Louie Woolger. GPL-3.0; see LICENSE. No path-based mutation fallback. */
#include "medieval_state.h"
#include "patch_io.h"
#include <rpc.h>
#include <tlhelp32.h>
#include <winver.h>
#include <stdarg.h>

#define MTW_TRANSACTION_NAME ".unofficial-medieval-total-war-patch-transaction"
#define MTW_RECEIPT_RELATIVE MEDIEVAL_STATE_NAME "/" MEDIEVAL_RECEIPT_NAME

/* Options borrow their strings through medieval_run. Outcome owns its JSON and
   printable detail until medieval_outcome_close. Status is 1/0; exit_code is the
   compatible 0/2/3/4 process result. Never copy a live outcome/document. */
typedef struct {
    const char *operation, *version, *output, *fault;
    const wchar_t *target, *payload, *installer, *uninstaller, *log, *request;
} MedievalOptions;
typedef struct {
    JsonDocument document;
    JsonValue *result;
    int exit_code, removal_completed;
    const char *rollback, *restoration;
    char detail[2048];
} MedievalOutcome;
typedef struct {
    const wchar_t *directory;
    JsonValue *json;
    MedievalJournal typed;
    PatchFileRecord journal_record;
    int published;
} MedievalTransaction;
typedef struct { wchar_t *path; PatchFileRecord record; } MedievalObservation;
typedef struct { char name[160], relative[192]; wchar_t *source; } MedievalArchiveSource;
typedef struct {
    MedievalOptions options;
    MedievalOutcome *outcome;
    PatchContext memory;
    JsonDocument document;
    PatchGuard guard, payload_guard, log_guard;
    MedievalIdentity identity;
    HANDLE executable_pin, mutex, log_handle;
    int mutex_owned, have_receipt_record;
    wchar_t *state, *transaction_path;
    JsonValue *payload, *warnings;
    PatchFileRecord payload_records[5], receipt_record;
    MedievalObservation observations[18];
    size_t observation_count;
    char *recovery_archive;
} MedievalEngine;

#define MTW_TRY(x) do { if (!(x)) return 0; } while (0)
static inline int mtw_fail(MedievalEngine *m, PatchError *e, const char *code, const char *message, ...) {
    if (!e || !e->code) {
        va_list args;
        va_start(args, message);
        vsnprintf(m->outcome->detail, sizeof(m->outcome->detail), message, args);
        va_end(args);
    }
    patch_error_set(e, code, "The lifecycle operation could not complete; see its detailed result.", 0);
    return 0;
}
static inline int mtw_text(JsonDocument *d, JsonValue *v, const char *key, const char *text, PatchError *e) {
    return json_set(d, v, key, json_text(d, text, e), e);
}
static inline int mtw_number(JsonDocument *d, JsonValue *v, const char *key, int64_t n, PatchError *e) {
    return json_set(d, v, key, json_number(d, n, e), e);
}
static inline int mtw_boolean(JsonDocument *d, JsonValue *v, const char *key, int n, PatchError *e) {
    return json_set(d, v, key, json_bool(d, n, e), e);
}
static inline const char *mtw_s(const JsonValue *v, const char *key) {
    const JsonValue *p = json_get(v, key);
    return p && p->type == JSON_STRING ? p->string : "";
}
static inline int mtw_is(const char *a, const char *b) { return a && !strcmp(a, b); }
static inline int mtw_guid(char out[37], PatchError *e) {
    UUID id;
    RPC_STATUS status = UuidCreate(&id);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY) {
        patch_error_set(e, "io_error", "Could not create an installation identifier.", status);
        return 0;
    }
    snprintf(out, 37, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned long)id.Data1, id.Data2, id.Data3, id.Data4[0], id.Data4[1], id.Data4[2],
             id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
    return 1;
}
static inline void mtw_utc(char out[32]) {
    SYSTEMTIME t;
    GetSystemTime(&t);
    snprintf(out, 32, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", (unsigned)t.wYear % 10000,
             (unsigned)t.wMonth % 100, (unsigned)t.wDay % 100, (unsigned)t.wHour % 100,
             (unsigned)t.wMinute % 100, (unsigned)t.wSecond % 100, (unsigned)t.wMilliseconds % 1000);
}
static inline wchar_t *mtw_path(MedievalEngine *m, const wchar_t *root, const char *relative, PatchError *e) {
    wchar_t *p = NULL;
    if (!root || !patch_path_join(&m->memory, root, relative, &p, e)) return NULL;
    return p;
}
static inline wchar_t *mtw_live(MedievalEngine *m, const char *relative, PatchError *e) {
    size_t index;
    if (!medieval_path_role(relative, &index)) {
        mtw_fail(m, e, "unsafe_path", "Recovery metadata names a file outside the patch allowlist.");
        return NULL;
    }
    return mtw_path(m, m->guard.canonical, relative, e);
}
static inline int mtw_record(MedievalEngine *m, const wchar_t *path, PatchFileRecord *r, PatchError *e) {
    return path && patch_file_record(&m->guard, path, r, e);
}
static inline int mtw_relative_record(MedievalEngine *m, const char *name, PatchFileRecord *r, PatchError *e) {
    return mtw_record(m, mtw_path(m, m->guard.canonical, name, e), r, e);
}
static inline int mtw_directory_exists(const wchar_t *p) {
    DWORD a = p ? GetFileAttributesW(p) : INVALID_FILE_ATTRIBUTES;
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static inline int mtw_parent_create(MedievalEngine *m, const wchar_t *p, PatchError *e) {
    wchar_t *parent = NULL;
    return p && patch_parent_path(&m->memory, p, &parent, e) &&
           patch_guard_directory(&m->guard, parent, 1, e);
}
static inline int mtw_external_record(const wchar_t *p, PatchFileRecord *r, PatchError *e) {
    PatchGuard g = {0};
    PatchContext c = {0};
    wchar_t *path = NULL, *parent = NULL;
    int ok = p && *p && patch_path_normalize(&c, p, &path, e) &&
             patch_parent_path(&c, path, &parent, e) && patch_guard_open_internal(&g, parent, 1, e) &&
             patch_file_record(&g, path, r, e);
    patch_guard_close(&g);
    patch_context_close(&c);
    return ok;
}
static inline int mtw_read_json(MedievalEngine *m, PatchGuard *guard, const wchar_t *p,
                                JsonValue **out, PatchFileRecord *record, PatchError *e) {
    JsonDocument parsed = {0};
    PatchContext c = {0};
    PatchFileRecord before, after;
    char *bytes = NULL;
    size_t size;
    int ok = 0;
    *out = NULL;
    if (!p || !patch_file_record(guard, p, &before, e) ||
        !patch_file_read(guard, p, 2097152, &c, &bytes, &size, e) ||
        !json_parse(bytes, size, &parsed, e) || !patch_file_record(guard, p, &after, e)) goto done;
    if (!patch_record_matches(&after, &before)) {
        mtw_fail(m, e, "concurrent_change", "Recovery metadata changed while it was being read; it was preserved.");
        goto done;
    }
    *out = json_clone(&m->document, parsed.root, e);
    if (!*out) goto done;
    if (record) *record = before;
    ok = 1;
done:
    json_document_close(&parsed);
    patch_context_close(&c);
    return ok;
}
static inline int mtw_log(MedievalEngine *m, const char *text, PatchError *e) {
    PatchContext c = {0};
    PatchDiagnostics d;
    char utc[32];
    int ok;
    if (m->log_handle == INVALID_HANDLE_VALUE) return 1;
    mtw_utc(utc);
    patch_diagnostics_init(&d, &c, NULL, m->log_handle);
    ok = patch_diagnostics_printf(&d, "%s %s\r\n", utc, text);
    if (!ok) patch_error_set(e, d.error.code, d.error.message, d.error.win32);
    patch_context_close(&c);
    return ok;
}
static inline int mtw_fault(MedievalEngine *m, const char *point, PatchError *e) {
    const char *p = m->options.fault ? m->options.fault : "";
    while (*p) {
        const char *end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p), k = strlen(point);
        if (n == k + 6 && !strncmp(p, "throw:", 6) && !memcmp(p + 6, point, k))
            return mtw_fail(m, e, "injected_failure", "Disposable-fixture failure at %s.", point);
        if (n == k + 6 && !strncmp(p, "crash:", 6) && !memcmp(p + 6, point, k)) {
            MTW_TRY(mtw_log(m, point, e));
            TerminateProcess(GetCurrentProcess(), 97);
            return 0;
        }
        if (n == k + 5 && !strncmp(p, "wait:", 5) && !memcmp(p + 5, point, k)) {
            wchar_t *path = mtw_path(m, m->guard.canonical, ".umtwp-fault-waiting", e);
            DWORD start = GetTickCount();
            MTW_TRY(path && patch_file_write_new(&m->guard, path, point, k, e));
            while (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES && GetTickCount() - start < 30000)
                Sleep(100);
        }
        p = end ? end + 1 : p + n;
    }
    return 1;
}
static inline int mtw_observed(MedievalEngine *m, const wchar_t *path, const PatchFileRecord *r, PatchError *e) {
    size_t i;
    int equal;
    for (i = 0; i < m->observation_count; ++i) {
        MTW_TRY(patch_path_equal(&m->guard.platform, path, m->observations[i].path, &equal, e));
        if (equal && !patch_record_matches(r, &m->observations[i].record))
            return mtw_fail(m, e, "concurrent_change", "A file changed after inspection or preservation. Its newest contents were retained; close programs editing this folder and retry.");
    }
    return 1;
}
static inline int mtw_observe(MedievalEngine *m, PatchError *e) {
    size_t i, kind;
    char relative[192];
    m->observation_count = 0;
    for (kind = 0; kind < 3; ++kind) for (i = 0; i < 5; ++i) {
        MedievalObservation *o = &m->observations[m->observation_count++];
        if (!kind) snprintf(relative, sizeof relative, "%s", medieval_payload_names[i]);
        else if (kind == 1) snprintf(relative, sizeof relative, "%s.unofficial-patch.bak", medieval_payload_names[i]);
        else snprintf(relative, sizeof relative, MEDIEVAL_STATE_NAME "/originals/%s", medieval_payload_names[i]);
        o->path = mtw_live(m, relative, e);
        MTW_TRY(mtw_record(m, o->path, &o->record, e));
    }
    for (i = 0; i < 3; ++i) {
        MedievalObservation *o = &m->observations[m->observation_count++];
        o->path = mtw_live(m, i == 0 ? MTW_RECEIPT_RELATIVE : i == 1 ? MEDIEVAL_STATE_NAME "/Uninstall.exe" : MEDIEVAL_UNINSTALL_NAME, e);
        MTW_TRY(mtw_record(m, o->path, &o->record, e));
    }
    if (m->have_receipt_record)
        MTW_TRY(mtw_observed(m, mtw_live(m, MTW_RECEIPT_RELATIVE, e), &m->receipt_record, e));
    return 1;
}
static inline int mtw_checked_write(MedievalEngine *m, JsonValue *value, const wchar_t *path,
                                    const PatchFileRecord *before, PatchError *e) {
    char *bytes = NULL;
    size_t length;
    int ok;
    MTW_TRY(path && json_seal(&m->document, value, e) &&
            json_write_value(&m->document, value, NULL, &bytes, &length, e));
    ok = mtw_parent_create(m, path, e) &&
         patch_file_write_atomic_owned(&m->guard, path, bytes, length, before, e);
    patch_context_free(&m->document.context, bytes);
    return ok;
}
static inline int mtw_registry_snapshot(MedievalEngine *m, const char *hive, const char *view,
                                       const char *name, PatchRegistrySnapshot *out, PatchError *e) {
    JsonValue *raw;
    return patch_registry_snapshot(&m->identity.registry, &m->document, hive, view, name, &raw, e) &&
           patch_registry_validate(&m->identity.registry, raw, out, e);
}
static inline int mtw_registry_compare(MedievalEngine *m, const PatchRegistrySnapshot *a,
                                      const PatchRegistrySnapshot *b, int *equal, PatchError *e) {
    return patch_registry_expected_equal(&m->identity.registry, a, b, equal, e);
}
static inline int mtw_json_equal(MedievalEngine *m, const JsonValue *a, const JsonValue *b, int *same, PatchError *e) {
    char *left = NULL, *right = NULL;
    size_t ln, rn;
    int ok = json_write_value(&m->document, a, NULL, &left, &ln, e) &&
             json_write_value(&m->document, b, NULL, &right, &rn, e);
    *same = ok && ln == rn && !memcmp(left, right, ln);
    patch_context_free(&m->document.context, left);
    patch_context_free(&m->document.context, right);
    return ok;
}
static inline int mtw_warning(MedievalEngine *m, const char *message, PatchError *e) {
    return json_append(m->warnings, json_text(&m->document, message, e), e);
}
static inline int mtw_remove_empty(MedievalEngine *m, const wchar_t *path, PatchError *e) {
    int removed;
    return path && patch_guard_remove_empty(&m->guard, path, &removed, e);
}
/* Enumerate only while the directory chain is pinned. Unknown children are
   retained; no recursion or enumeration-derived deletion is used. */
static inline int mtw_unknown_children(MedievalEngine *m, const wchar_t *path, int state, int *unknown, PatchError *e) {
    WIN32_FIND_DATAW data;
    HANDLE search;
    wchar_t *pattern;
    size_t n;
    void *memory = NULL;
    DWORD last;
    *unknown = 0;
    if (!path) return 0;
    if (!mtw_directory_exists(path)) return 1;
    MTW_TRY(patch_guard_directory(&m->guard, path, 0, e));
    n = wcslen(path);
    MTW_TRY(patch_alloc(&m->memory, n + 3, sizeof(wchar_t), &memory, e));
    pattern = memory;
    wcscpy(pattern, path); wcscat(pattern, L"\\*");
    search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return 1;
        return patch_file_fail(e, "Cannot enumerate patch metadata.");
    }
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
        if (state == 1 && (!wcscmp(data.cFileName, L"install-manifest.json") || !wcscmp(data.cFileName, L"originals"))) continue;
        if (state == 2 && !wcscmp(data.cFileName, L"journal.json")) continue;
        *unknown = 1;
    } while (FindNextFileW(search, &data));
    last = GetLastError();
    FindClose(search);
    if (last != ERROR_NO_MORE_FILES) {
        patch_error_set(e, "unsafe_path", "Patch metadata enumeration failed.", last);
        return 0;
    }
    return 1;
}

/* Implementations that also need lifecycle state are below lifecycle.h. */
static inline int mtw_complete_removal(MedievalEngine *, MedievalTransaction *, JsonValue **, PatchError *);
static inline int mtw_verify_installed(MedievalEngine *, const MedievalReceipt *, PatchError *);

static inline wchar_t *mtw_staged(MedievalEngine *m, MedievalTransaction *t, const char *side,
                                  size_t index, PatchError *e) {
    char relative[32];
    snprintf(relative, sizeof relative, "%s/%03u.bin", side, (unsigned)index);
    return mtw_path(m, t->directory, relative, e);
}
static inline int mtw_transaction_prepare(MedievalEngine *m, MedievalTransaction *t, const char *operation,
                                        const MedievalReceipt *receipt, PatchError *e) {
    char id[37], name[100], utc[32];
    JsonDocument *d = &m->document;
    JsonValue *j;
    if (mtw_directory_exists(m->transaction_path))
        return mtw_fail(m, e, "recovery_required", "An earlier transaction must be recovered before continuing.");
    MTW_TRY(mtw_guid(id, e));
    snprintf(name, sizeof name, ".unofficial-medieval-patch-preparing-%s", id);
    t->directory = mtw_path(m, m->guard.canonical, name, e);
    MTW_TRY(t->directory && patch_guard_directory(&m->guard, t->directory, 1, e));
    MTW_TRY(patch_guard_directory(&m->guard, mtw_path(m, t->directory, "before", e), 1, e));
    MTW_TRY(patch_guard_directory(&m->guard, mtw_path(m, t->directory, "after", e), 1, e));
    t->json = j = json_new(d, JSON_OBJECT, e);
    mtw_utc(utc);
    MTW_TRY(j && mtw_text(d, j, "schema", MEDIEVAL_JOURNAL_SCHEMA, e) &&
            mtw_text(d, j, "target", m->identity.target, e) &&
            mtw_text(d, j, "directory_identity", m->guard.identity, e) &&
            mtw_text(d, j, "owner_sid", m->identity.owner_sid, e) &&
            mtw_text(d, j, "installation_id", receipt->installation_id, e) &&
            mtw_text(d, j, "operation", operation, e) && mtw_text(d, j, "phase", "prepared", e) &&
            mtw_number(d, j, "started", 0, e) && mtw_text(d, j, "created_utc", utc, e) &&
            json_set(d, j, "actions", json_new(d, JSON_ARRAY, e), e) &&
            json_set(d, j, "removal_receipt", mtw_is(operation, "restore") ? json_clone(d, receipt->json, e) : json_new(d, JSON_NULL, e), e) &&
            json_set(d, j, "install_receipt", mtw_is(operation, "install") ? json_clone(d, receipt->json, e) : json_new(d, JSON_NULL, e), e) &&
            mtw_text(d, j, "recovery_archive", m->recovery_archive ? m->recovery_archive : "", e) &&
            mtw_boolean(d, j, "state_existed", mtw_directory_exists(m->state), e));
    return 1;
}
static inline int mtw_transaction_add_file(MedievalEngine *m, MedievalTransaction *t, const char *relative,
                                         const wchar_t *source, PatchError *e) {
    JsonDocument *d = &m->document;
    JsonValue *actions = json_get(t->json, "actions"), *a;
    wchar_t *destination = mtw_live(m, relative, e), *stage;
    PatchFileRecord before, after = {0};
    size_t index = actions->count;
    MTW_TRY(mtw_record(m, destination, &before, e) && mtw_observed(m, destination, &before, e));
    if (before.exists) {
        stage = mtw_staged(m, t, "before", index, e);
        MTW_TRY(stage && patch_file_copy_new(&m->guard, destination, stage, &before, e));
    }
    if (source) {
        MTW_TRY(mtw_external_record(source, &after, e));
        if (!after.exists) return mtw_fail(m, e, "invalid_payload", "A staged installation source is missing.");
        MTW_TRY(mtw_observed(m, source, &after, e));
        stage = mtw_staged(m, t, "after", index, e);
        MTW_TRY(stage && patch_file_copy_new(&m->guard, source, stage, &after, e));
    }
    a = json_new(d, JSON_OBJECT, e);
    MTW_TRY(a && mtw_text(d, a, "kind", "file", e) && mtw_text(d, a, "relative", relative, e) &&
            json_set(d, a, "before", medieval_record_json(d, &before, e), e) &&
            json_set(d, a, "after", medieval_record_json(d, &after, e), e) && json_append(actions, a, e));
    return 1;
}
static inline int mtw_transaction_add_registry(MedievalEngine *m, MedievalTransaction *t, const char *hive,
                                              const char *view, const char *name, const JsonValue *before,
                                              const JsonValue *after, PatchError *e) {
    JsonDocument *d = &m->document;
    JsonValue *a = json_new(d, JSON_OBJECT, e);
    return a && mtw_text(d, a, "kind", "registry", e) && mtw_text(d, a, "hive", hive, e) &&
           mtw_text(d, a, "view", view, e) && mtw_text(d, a, "name", name, e) &&
           json_set(d, a, "before", json_clone(d, before, e), e) &&
           json_set(d, a, "after", json_clone(d, after, e), e) && json_append(json_get(t->json, "actions"), a, e);
}
static inline int mtw_receipt_bound(MedievalEngine *m, const MedievalReceipt *actual,
                                   const MedievalReceipt *retained, const char *status, PatchError *e) {
    size_t k;
    if (strcmp(actual->installation_id, retained->installation_id) || strcmp(actual->status, status) ||
        strcmp(actual->uninstaller_sha256, retained->uninstaller_sha256) ||
        strcmp(actual->preinstall_mode, retained->preinstall_mode) ||
        strcmp(actual->installer_version, retained->installer_version))
        return mtw_fail(m, e, "receipt_invalid", "The receipt does not describe this transaction's installation.");
    for (k = 0; k < 5; ++k) {
        const MedievalFileState *x = &actual->files[k], *y = &retained->files[k];
        if (!patch_record_equal(&x->original, &y->original) || !patch_record_equal(&x->installed, &y->installed) ||
            x->sidecar_created != y->sidecar_created || strcmp(x->snapshot_relative, y->snapshot_relative) ||
            strcmp(x->sidecar_relative, y->sidecar_relative) || strcmp(x->legacy_original_sha256, y->legacy_original_sha256))
            return mtw_fail(m, e, "receipt_invalid", "The receipt changes the retained original baseline or file ownership.");
    }
    return 1;
}
static inline int mtw_staged_receipt(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    size_t i;
    int terminal = mtw_is(t->typed.phase, "committed") || mtw_is(t->typed.phase, "rolled-back");
    for (i = 0; i < t->typed.count; ++i) {
        const MedievalAction *a = &t->typed.actions[i];
        JsonValue *raw;
        MedievalReceipt staged;
        PatchFileRecord actual;
        wchar_t *p;
        if (a->is_registry || strcmp(a->relative, MTW_RECEIPT_RELATIVE)) continue;
        p = mtw_staged(m, t, "after", i, e);
        MTW_TRY(mtw_record(m, p, &actual, e));
        if (!actual.exists && terminal) {
            /* A completed install still requires its live receipt even if a
               cleanup already retired the copy. Removal separately allows a
               retired live receipt and verifies any surviving one. */
            if (!mtw_is(t->typed.phase, "committed") || !mtw_is(t->typed.operation, "install")) return 1;
            p = mtw_live(m, MTW_RECEIPT_RELATIVE, e);
            MTW_TRY(mtw_record(m, p, &actual, e));
        }
        if (!patch_record_equal(&actual, &a->after_file))
            return mtw_fail(m, e, "receipt_invalid", "The staged receipt is missing or damaged; all recovery files were retained.");
        MTW_TRY(mtw_read_json(m, &m->guard, p, &raw, NULL, e) &&
                medieval_receipt_validate(&m->document, raw, &m->identity, 0, &staged, e));
        return mtw_receipt_bound(m, &staged, &t->typed.receipt, mtw_is(t->typed.operation, "restore") ? "restored" : "installed", e);
    }
    return mtw_fail(m, e, "receipt_invalid", "Recovery metadata has no receipt action.");
}
static inline int mtw_transaction_validate(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    return medieval_journal_validate(&m->document, t->json, &m->identity, &t->typed, e) &&
           medieval_journal_verify_copies(&m->identity, &t->typed, t->directory, e) && mtw_staged_receipt(m, t, e);
}
static inline int mtw_transaction_save(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    wchar_t *p = mtw_path(m, t->directory, "journal.json", e);
    int initial = !t->journal_record.exists;
    PatchFileRecord expected = {0}, written;
    char *bytes = NULL;
    size_t length;
    MTW_TRY(p && mtw_fault(m, "before-journal-write", e) &&
            mtw_checked_write(m, t->json, p, &t->journal_record, e));
    MTW_TRY(json_write_value(&m->document, t->json, NULL, &bytes, &length, e));
    expected.exists = 1; expected.length = length;
    if (!patch_sha256_bytes(&m->memory, bytes, length, expected.sha256)) {
        patch_context_free(&m->document.context, bytes);
        patch_error_set(e, m->memory.error.code, m->memory.error.message, m->memory.error.win32);
        return 0;
    }
    patch_context_free(&m->document.context, bytes);
    if (initial) MTW_TRY(mtw_fault(m, "after-journal-write", e));
    /* Atomic replacement creates a different object. Bind the next write to
       the new live ID, never to the retired journal or its previous copy. */
    MTW_TRY(mtw_record(m, p, &written, e));
    if (!patch_record_equal(&written, &expected))
        return mtw_fail(m, e, "file_changed", "The journal changed after it was written. Foreign contents were preserved; no authority was adopted from them.");
    t->journal_record = written;
    return medieval_journal_validate(&m->document, t->json, &m->identity, &t->typed, e);
}
static inline int mtw_transaction_publish(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    MTW_TRY(mtw_transaction_save(m, t, e) && mtw_transaction_validate(m, t, e));
    MTW_TRY(patch_guard_rename_directory(&m->guard, t->directory, m->transaction_path, e));
    t->directory = m->transaction_path;
    t->published = 1;
    return patch_guard_directory(&m->guard, t->directory, 0, e);
}
static inline int mtw_transaction_file(MedievalEngine *m, MedievalTransaction *t, size_t index,
                                     int after, PatchError *e) {
    const MedievalAction *a = &t->typed.actions[index];
    const PatchFileRecord *expected = after ? &a->after_file : &a->before_file;
    const PatchFileRecord *other = after ? &a->before_file : &a->after_file;
    PatchFileRecord current, source_record, verified;
    wchar_t *destination = mtw_live(m, a->relative, e), *source;
    MTW_TRY(mtw_record(m, destination, &current, e));
    if (patch_record_equal(&current, expected)) return 1;
    if (!patch_record_equal(&current, other) && (after || current.exists))
        return mtw_fail(m, e, "recovery_conflict", "A file changed before its transaction action: %s. Its newest contents were preserved.", a->relative);
    MTW_TRY(mtw_parent_create(m, destination, e));
    if (expected->exists) {
        source = mtw_staged(m, t, after ? "after" : "before", index, e);
        MTW_TRY(mtw_record(m, source, &source_record, e));
        if (!patch_record_equal(&source_record, expected))
            return mtw_fail(m, e, "receipt_invalid", "A required transaction copy is missing or corrupt: %s.", a->relative);
        MTW_TRY(patch_file_replace(&m->guard, source, destination, &current, &source_record, e));
    } else if (current.exists) MTW_TRY(patch_file_remove(&m->guard, destination, &current, e));
    MTW_TRY(mtw_record(m, destination, &verified, e));
    if (!patch_record_equal(&verified, expected))
        return mtw_fail(m, e, "verification_failed", "Transaction file verification failed: %s.", a->relative);
    return 1;
}
/* Interrupted registry setters may leave a mix of the two validated states.
   Every present value must be unchanged from one of them, and unknown values
   and subkeys must still match. No arbitrary partial owned entry is adopted. */
static inline int mtw_registry_recoverable(MedievalEngine *m, const MedievalAction *a,
                                         const PatchRegistrySnapshot *current, PatchError *e) {
    const JsonValue *v, *candidate;
    int equal, owned;
    if (current->subkeys != a->before_registry.subkeys)
        return mtw_fail(m, e, "recovery_conflict", "Registry subkeys changed outside this operation.");
    MTW_TRY(medieval_registry_unknown_preserved(&m->identity.registry, &a->before_registry, current, e));
    for (v = current->values->child; v; v = v->next) {
        const char *name = json_get(v, "name")->string;
        MTW_TRY(patch_registry_owned_name(&m->identity.registry, name, &owned, e));
        if (!owned) continue;
        MTW_TRY(patch_registry_find(&m->identity.registry, &a->before_registry, name, &candidate, e));
        equal = 0;
        if (candidate) {
            MTW_TRY(mtw_json_equal(m, json_get(v, "value"), json_get(candidate, "value"), &equal, e));
            equal = equal && !strcmp(mtw_s(v, "kind"), mtw_s(candidate, "kind"));
        }
        if (!equal) {
            MTW_TRY(patch_registry_find(&m->identity.registry, &a->after_registry, name, &candidate, e));
            if (candidate) {
                MTW_TRY(mtw_json_equal(m, json_get(v, "value"), json_get(candidate, "value"), &equal, e));
                equal = equal && !strcmp(mtw_s(v, "kind"), mtw_s(candidate, "kind"));
            }
        }
        if (!equal) return mtw_fail(m, e, "recovery_conflict", "A registry value changed outside this operation; it was preserved.");
    }
    return 1;
}
static inline int mtw_registry_write_checked(MedievalEngine *m, const char *hive, const char *view,
                                           const char *name, const PatchRegistrySnapshot *expected,
                                           const JsonValue *desired, PatchError *e) {
    PatchRegistrySnapshot now;
    int equal;
    MTW_TRY(mtw_registry_snapshot(m, hive, view, name, &now, e) &&
            mtw_registry_compare(m, &now, expected, &equal, e));
    if (!equal) return mtw_fail(m, e, "concurrent_change", "Windows registration changed before publication; it was preserved.");
    return patch_registry_set(&m->identity.registry, hive, view, name, desired, e);
}
static inline int mtw_transaction_commit(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    size_t index, file_index = 0;
    char point[48], line[384];
    MTW_TRY(mtw_text(&m->document, t->json, "phase", "applying", e) && mtw_transaction_save(m, t, e));
    for (index = 0; index < t->typed.count; ++index) {
        const MedievalAction *a = &t->typed.actions[index];
        PatchFileRecord current;
        PatchRegistrySnapshot registry;
        int equal;
        if (!a->is_registry) {
            MTW_TRY(mtw_record(m, mtw_live(m, a->relative, e), &current, e));
            if (!patch_record_equal(&current, &a->before_file))
                return mtw_fail(m, e, "concurrent_change", "A file changed after transaction preparation: %s. It was preserved.", a->relative);
        } else {
            MTW_TRY(mtw_registry_snapshot(m, a->hive, a->view, a->name, &registry, e) &&
                    mtw_registry_compare(m, &registry, &a->before_registry, &equal, e));
            if (!equal) return mtw_fail(m, e, "concurrent_change", "Windows registration changed after preparation; it was preserved.");
        }
        MTW_TRY(mtw_number(&m->document, t->json, "started", (int64_t)index + 1, e) && mtw_transaction_save(m, t, e));
        a = &t->typed.actions[index];
        if (!a->is_registry) {
            MTW_TRY(mtw_transaction_file(m, t, index, 1, e));
            snprintf(line, sizeof line, "committed_file=%s sha256=%s", a->relative, a->after_file.sha256);
            MTW_TRY(mtw_log(m, line, e));
            snprintf(point, sizeof point, "after-file-%u", (unsigned)file_index++);
            MTW_TRY(mtw_fault(m, point, e));
        } else {
            MTW_TRY(mtw_fault(m, "before-registry-write", e));
            MTW_TRY(mtw_registry_write_checked(m, a->hive, a->view, a->name, &registry, a->after_registry.json, e));
            snprintf(line, sizeof line, "committed_registry=%s/%s/%s", a->hive, a->view, a->name);
            MTW_TRY(mtw_log(m, line, e) && mtw_fault(m, "after-registry", e));
        }
    }
    MTW_TRY(mtw_fault(m, "before-commit", e) && mtw_text(&m->document, t->json, "phase", "committed", e) &&
            mtw_transaction_save(m, t, e));
    return mtw_log(m, "transaction=committed", e);
}
static inline int mtw_transaction_final(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    size_t i;
    for (i = 0; i < t->typed.count; ++i) {
        const MedievalAction *a = &t->typed.actions[i];
        if (!a->is_registry) {
            PatchFileRecord current;
            MTW_TRY(mtw_record(m, mtw_live(m, a->relative, e), &current, e));
            if (!patch_record_equal(&current, &a->after_file))
                return mtw_fail(m, e, "recovery_conflict", "A committed file changed before final verification: %s. Its contents and the recovery journal were preserved.", a->relative);
        } else {
            PatchRegistrySnapshot current;
            int equal;
            MTW_TRY(mtw_registry_snapshot(m, a->hive, a->view, a->name, &current, e) &&
                    mtw_registry_compare(m, &current, &a->after_registry, &equal, e));
            if (!equal) return mtw_fail(m, e, "recovery_conflict", "A committed registry entry changed before final verification; it and the recovery journal were preserved.");
        }
    }
    return 1;
}
static inline int mtw_transaction_clear(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    size_t i;
    int equal, side, removed = 0, unknown;
    char id[37], relative[128];
    wchar_t *path;
    PatchFileRecord current;
    JsonValue *raw;
    MTW_TRY(patch_path_equal(&m->guard.platform, t->directory, m->transaction_path, &equal, e));
    if (equal) {
        MTW_TRY(mtw_guid(id, e));
        snprintf(relative, sizeof relative, ".medieval-cleanup-%.12s", id);
        path = mtw_path(m, m->guard.canonical, relative, e);
        MTW_TRY(path && patch_guard_rename_directory(&m->guard, t->directory, path, e));
        t->directory = path;
        MTW_TRY(patch_guard_directory(&m->guard, path, 0, e) && mtw_log(m, "transaction_cleanup=retired", e));
    }
    for (i = 0; i < t->typed.count; ++i) {
        const MedievalAction *a = &t->typed.actions[i];
        if (a->is_registry) continue;
        for (side = 0; side < 2; ++side) {
            path = mtw_staged(m, t, side ? "after" : "before", i, e);
            MTW_TRY(mtw_record(m, path, &current, e));
            if (!current.exists) continue;
            if (!patch_record_equal(&current, side ? &a->after_file : &a->before_file))
                return mtw_fail(m, e, "cleanup_pending", "A transaction copy changed; it was preserved for review.");
            MTW_TRY(patch_file_remove(&m->guard, path, &current, e));
            if (!removed) { removed = 1; MTW_TRY(mtw_fault(m, "cleanup-after-first-backup", e)); }
        }
    }
    MTW_TRY(mtw_remove_empty(m, mtw_path(m, t->directory, "before", e), e) &&
            mtw_remove_empty(m, mtw_path(m, t->directory, "after", e), e) &&
            mtw_unknown_children(m, t->directory, 2, &unknown, e));
    if (unknown) {
        MTW_TRY(mtw_guid(id, e));
        snprintf(relative, sizeof relative, "Unofficial Medieval Patch Recovery - transaction - %s", id);
        path = mtw_path(m, m->guard.canonical, relative, e);
        MTW_TRY(path && patch_guard_rename_directory(&m->guard, t->directory, path, e));
        MTW_TRY(mtw_warning(m, "Unrecognized transaction content was retained in a recovery directory.", e));
    } else {
        path = mtw_path(m, t->directory, "journal.json", e);
        MTW_TRY(mtw_read_json(m, &m->guard, path, &raw, &current, e) &&
                mtw_json_equal(m, raw, t->json, &equal, e));
        if (!equal || !patch_record_matches(&current, &t->journal_record))
            return mtw_fail(m, e, "cleanup_pending", "The transaction journal changed; it was preserved.");
        MTW_TRY(patch_file_remove(&m->guard, path, &current, e) && mtw_fault(m, "cleanup-after-journal-delete", e) &&
                mtw_remove_empty(m, t->directory, e));
    }
    return mtw_remove_empty(m, mtw_path(m, m->state, "originals", e), e) && mtw_remove_empty(m, m->state, e);
}
static inline int mtw_transaction_rollback(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    size_t index, started = t->typed.started;
    for (index = 0; index < started; ++index) {
        const MedievalAction *a = &t->typed.actions[index];
        if (!a->is_registry) {
            PatchFileRecord current;
            MTW_TRY(mtw_record(m, mtw_live(m, a->relative, e), &current, e));
            if (current.exists && !patch_record_equal(&current, &a->before_file) && !patch_record_equal(&current, &a->after_file))
                return mtw_fail(m, e, "recovery_conflict", "Recovery preserved a file changed outside this operation: %s. Keep the recovery directory and resolve the conflict before retrying.", a->relative);
        } else {
            PatchRegistrySnapshot current;
            MTW_TRY(mtw_registry_snapshot(m, a->hive, a->view, a->name, &current, e) &&
                    mtw_registry_recoverable(m, a, &current, e));
        }
    }
    for (index = started; index-- > 0;) {
        const MedievalAction *a = &t->typed.actions[index];
        if (!a->is_registry) MTW_TRY(mtw_transaction_file(m, t, index, 0, e));
        else {
            PatchRegistrySnapshot current;
            MTW_TRY(mtw_registry_snapshot(m, a->hive, a->view, a->name, &current, e) &&
                    mtw_registry_recoverable(m, a, &current, e) &&
                    mtw_registry_write_checked(m, a->hive, a->view, a->name, &current, a->before_registry.json, e));
        }
    }
    m->outcome->rollback = "verified";
    MTW_TRY(mtw_text(&m->document, t->json, "phase", "rolled-back", e) && mtw_transaction_save(m, t, e) &&
            mtw_log(m, "rollback=verified", e) && mtw_fault(m, "rollback-after-restoration", e));
    return mtw_transaction_clear(m, t, e);
}
static inline int mtw_transaction_failure(MedievalEngine *m, MedievalTransaction *t, PatchError *e) {
    /* A file-layer recovery_required means publication/retirement itself is
       uncertain. Stop here: retain the journal and the guard's named original
       for an explicit next-run recovery, rather than losing that diagnostic
       path in a subsequent replacement during automatic rollback. */
    if (t->published && e && mtw_is(e->code, "recovery_required")) {
        m->outcome->rollback = "incomplete";
        return 0;
    }
    if (t->published && !mtw_is(mtw_s(t->json, "phase"), "committed")) {
        PatchError recovery = {0};
        char first[2048];
        memcpy(first, m->outcome->detail, sizeof first);
        if (!mtw_transaction_rollback(m, t, &recovery)) {
            m->outcome->rollback = "incomplete";
            /* Keep the initiating code/native error; report incomplete recovery
               separately instead of overwriting the first useful diagnostic. */
            snprintf(m->outcome->detail, sizeof m->outcome->detail,
                     "%.1200s Recovery is incomplete: %.500s Keep the journal, uninstaller and originals, then retry.",
                     first[0] ? first : (e && e->message ? e->message : "Operation stopped."),
                     recovery.message ? recovery.message : "Recovery failed.");
        } else memcpy(m->outcome->detail, first, sizeof first);
    }
    return 0;
}
static inline int mtw_transaction_recover(MedievalEngine *m, JsonValue **result, int *pending, PatchError *e) {
    MedievalTransaction t = {0}, previous = {0};
    wchar_t *p, *fallback;
    PatchFileRecord primary, backup;
    size_t index;
    int equal;
    *result = NULL; *pending = 0;
    if (!mtw_directory_exists(m->transaction_path)) return 1;
    MTW_TRY(patch_guard_directory(&m->guard, m->transaction_path, 0, e));
    p = mtw_path(m, m->transaction_path, "journal.json", e);
    fallback = mtw_path(m, m->transaction_path, "journal.json.mtw-previous", e);
    MTW_TRY(mtw_record(m, p, &primary, e) && mtw_record(m, fallback, &backup, e));
    if (!primary.exists && !backup.exists)
        return mtw_fail(m, e, "receipt_invalid", "The recovery directory has no valid journal. Preserve it for manual recovery; no game files were changed by this run.");
    t.directory = m->transaction_path; t.published = 1;
    MTW_TRY(mtw_read_json(m, &m->guard, primary.exists ? p : fallback, &t.json, &t.journal_record, e) &&
            mtw_transaction_validate(m, &t, e));
    *pending = 1;
    if (mtw_is(m->options.operation, "Inspect")) return 1;
    if (mtw_is(m->options.operation, "Verify"))
        return mtw_fail(m, e, "recovery_required", "An interrupted operation must be recovered by setup or the patch uninstaller before verification.");
    if (backup.exists) {
        previous.directory = m->transaction_path;
        MTW_TRY(mtw_read_json(m, &m->guard, fallback, &previous.json, &previous.journal_record, e) &&
                mtw_transaction_validate(m, &previous, e));
        if (strcmp(previous.typed.installation_id, t.typed.installation_id))
            return mtw_fail(m, e, "receipt_invalid", "The previous journal belongs to another installation and was preserved.");
    }
    if (!primary.exists) {
        MTW_TRY(patch_file_copy_new(&m->guard, fallback, p, &backup, e) && mtw_record(m, p, &t.journal_record, e));
        if (!patch_record_equal(&t.journal_record, &backup))
            return mtw_fail(m, e, "recovery_required", "The recovered primary journal changed; both slots were preserved.");
    }
    if (backup.exists) MTW_TRY(patch_file_remove(&m->guard, fallback, &backup, e));
    MTW_TRY(mtw_log(m, "recovering=validated-transaction", e));
    if (mtw_is(t.typed.phase, "committed") && mtw_is(t.typed.operation, "restore"))
        return mtw_complete_removal(m, &t, result, e);
    if (mtw_is(t.typed.phase, "committed") || mtw_is(t.typed.phase, "rolled-back")) {
        int after = mtw_is(t.typed.phase, "committed");
        for (index = 0; index < t.typed.count; ++index) {
            const MedievalAction *a = &t.typed.actions[index];
            if (!a->is_registry) {
                PatchFileRecord current;
                MTW_TRY(mtw_record(m, mtw_live(m, a->relative, e), &current, e));
                if (!patch_record_equal(&current, after ? &a->after_file : &a->before_file))
                    return mtw_fail(m, e, "recovery_conflict", "A completed transaction file changed before cleanup: %s. Keep the recovery directory and resolve the change before retrying.", a->relative);
            } else {
                PatchRegistrySnapshot current;
                MTW_TRY(mtw_registry_snapshot(m, a->hive, a->view, a->name, &current, e) &&
                        mtw_registry_compare(m, &current, after ? &a->after_registry : &a->before_registry, &equal, e));
                if (!equal) return mtw_fail(m, e, "recovery_conflict", "A completed transaction registration changed before cleanup; it was preserved.");
            }
        }
        if (after) MTW_TRY(mtw_verify_installed(m, &t.typed.receipt, e));
        MTW_TRY(mtw_transaction_clear(m, &t, e));
    } else MTW_TRY(mtw_transaction_rollback(m, &t, e));
    *pending = 0;
    return 1;
}

#endif
