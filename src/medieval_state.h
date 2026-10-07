#ifndef MTW_MEDIEVAL_STATE_H
#define MTW_MEDIEVAL_STATE_H
/* Validated Medieval wire records. Parsed JSON is not mutation authority.
   A typed receipt/journal borrows its document and identity. Keep all alive.
   Original/staged-file verification is separate and mandatory before mutation. */
#include "patch_files.h"
#include "patch_registry.h"

#define MEDIEVAL_STATE_NAME ".unofficial-medieval-total-war-patch"
#define MEDIEVAL_RECEIPT_NAME "install-manifest.json"
#define MEDIEVAL_RECEIPT_SCHEMA "unofficial-medieval-total-war-patch-install-v2"
#define MEDIEVAL_LEGACY_SCHEMA "unofficial-medieval-total-war-patch-install-v1"
#define MEDIEVAL_JOURNAL_SCHEMA "unofficial-medieval-patch-transaction-v2"
#define MEDIEVAL_EXECUTABLE_HASH "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
static const char *const medieval_payload_names[] = {"dgVoodoo_D3D9.dll", "ddraw.dll", "D3DImm.dll",
                                                     "dgVoodoo.conf", "D3D9.dll"};

typedef struct {
    PatchContext context;
    PatchGuard *guard; /* borrowed, opened canonical directory */
    PatchRegistry registry;
    char *target, *owner_sid;
    size_t target_length;
    char registration_key[57], mutex_name[64];
} MedievalIdentity;
typedef struct {
    const char *name, *snapshot_relative, *sidecar_relative, *legacy_original_sha256;
    int sidecar_created;
    PatchFileRecord original, installed; /* wire decoding always clears file IDs */
    const JsonValue *json;
} MedievalFileState;
typedef struct {
    int legacy;
    const JsonValue *json;
    const char *installation_id, *status, *target_directory, *installer_version, *preinstall_mode,
        *uninstaller_sha256;
    MedievalFileState files[5];
} MedievalReceipt;
typedef struct {
    int is_registry;
    const JsonValue *json;
    const char *relative, *hive, *view, *name;
    PatchFileRecord before_file, after_file;
    PatchRegistrySnapshot before_registry, after_registry;
} MedievalAction;
typedef struct {
    const JsonValue *json;
    const char *operation, *phase, *installation_id;
    size_t count, started;
    int state_existed;
    MedievalReceipt receipt;
    MedievalAction actions[48];
} MedievalJournal;

static inline int medieval_hash_valid(const char *s) {
    size_t i;
    if (!s || strlen(s) != 64)
        return 0;
    for (i = 0; i < 64; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F')))
            return 0;
    return 1;
}
static inline int medieval_guid_valid(const char *s) {
    size_t i;
    int nonzero = 0;
    if (!s || strlen(s) != 36)
        return 0;
    for (i = 0; i < 36; ++i) {
        unsigned c = (unsigned char)s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return 0;
            continue;
        }
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return 0;
        if (c != '0')
            nonzero = 1;
    }
    return nonzero;
}
static inline const char *medieval_string(const JsonValue *object, const char *key, int optional,
                                          PatchError *e) {
    const JsonValue *v = json_get(object, key);
    if (optional && (!v || v->type == JSON_NULL))
        return "";
    return json_cstring(v, e);
}
static inline int medieval_integer(const JsonValue *object, const char *key, int optional, int64_t *out,
                                   PatchError *e) {
    const JsonValue *v = json_get(object, key);
    *out = 0;
    if (optional && (!v || v->type == JSON_NULL))
        return 1;
    if (!v || v->type != JSON_NUMBER)
        return json_invalid(e, "Expected integer state field.");
    *out = v->number;
    return 1;
}
static inline int medieval_boolean(const JsonValue *object, const char *key, int *out, PatchError *e) {
    const JsonValue *v = json_get(object, key);
    *out = 0;
    if (!v || v->type != JSON_BOOL)
        return json_invalid(e, "Expected boolean state field.");
    *out = v->boolean;
    return 1;
}
static inline void medieval_identity_close(MedievalIdentity *identity) {
    if (!identity)
        return;
    patch_context_close(&identity->context);
    identity->guard = NULL;
    identity->target = NULL;
    identity->owner_sid = NULL;
}
static inline int medieval_identity_open(MedievalIdentity *identity, PatchGuard *guard, PatchError *e) {
    if (!identity || identity->guard || !guard || !guard->canonical) {
        patch_error_set(e, "invalid_argument", "An opened canonical guard and empty identity are required.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_registry_init(&identity->registry, e) ||
        !patch_wide_to_utf8(&identity->context, guard->canonical, wcslen(guard->canonical), &identity->target,
                            &identity->target_length, e) ||
        !patch_owner_sid(&identity->context, &identity->owner_sid, e) ||
        !patch_registry_identity(&identity->context, guard->canonical, identity->registration_key,
                                 identity->mutex_name, e)) {
        medieval_identity_close(identity);
        return 0;
    }
    identity->guard = guard;
    identity->registry.allocator = identity->context.allocator;
    identity->registry.memory_limit = identity->context.memory_limit;
    return 1;
}
static inline int medieval_integrity_valid(JsonDocument *owner, const JsonValue *value, PatchError *e) {
    char digest[65];
    const char *stored = medieval_string(value, "integrity_sha256", 0, e);
    if (!stored || !medieval_hash_valid(stored))
        return json_invalid(e, "Invalid metadata integrity value.");
    if (!json_integrity(owner, value, digest, e))
        return 0;
    return !strcmp(digest, stored) ? 1 : json_invalid(e, "Metadata integrity check failed.");
}
static inline int medieval_file_record(const JsonValue *value, PatchFileRecord *out, PatchError *e) {
    const char *hash;
    int exists;
    int64_t length;
    PatchFileRecord record = {0};
    memset(out, 0, sizeof(*out));
    if (!medieval_boolean(value, "exists", &exists, e) || !medieval_integer(value, "length", 0, &length, e) ||
        (hash = medieval_string(value, "sha256", 1, e)) == NULL)
        return 0;
    if (length < 0 || (exists && !medieval_hash_valid(hash)) || (!exists && (*hash || length)))
        return json_invalid(e, "Invalid recovery file identity.");
    record.exists = exists;
    record.length = (uint64_t)length;
    if (exists)
        memcpy(record.sha256, hash, 65);
    *out = record;
    return 1;
}
static inline JsonValue *medieval_record_json(JsonDocument *doc, const PatchFileRecord *record,
                                              PatchError *e) {
    JsonValue *out = json_new(doc, JSON_OBJECT, e);
    if (record->length > INT64_MAX || (record->exists && !medieval_hash_valid(record->sha256)) ||
        (!record->exists && record->length)) {
        json_invalid(e, "Invalid file record for serialization.");
        return NULL;
    }
    if (!out || !json_set(doc, out, "exists", json_bool(doc, record->exists, e), e) ||
        !json_set(doc, out, "sha256",
                  record->exists ? json_text(doc, record->sha256, e) : json_new(doc, JSON_NULL, e), e) ||
        !json_set(doc, out, "length", json_number(doc, (int64_t)record->length, e), e))
        return NULL;
    return out;
}
static inline int medieval_receipt_validate(JsonDocument *owner, const JsonValue *value,
                                            const MedievalIdentity *identity, int allow_legacy,
                                            MedievalReceipt *out, PatchError *e) {
    MedievalReceipt r = {0};
    const char *schema, *target, *id, *sid, *key, *hash, *original, *snapshot, *sidecar, *legacy_hash;
    const JsonValue *files, *rec;
    size_t i;
    int existed, created;
    int64_t length;
    char expected[96];
    memset(out, 0, sizeof(*out));
    schema = medieval_string(value, "schema", 0, e);
    if (!schema)
        return 0;
    r.legacy = !strcmp(schema, MEDIEVAL_LEGACY_SCHEMA);
    if (strcmp(schema, MEDIEVAL_RECEIPT_SCHEMA) && !(allow_legacy && r.legacy))
        return json_invalid(e, "Unsupported installation receipt schema.");
    if (!r.legacy && !medieval_integrity_valid(owner, value, e))
        return 0;
    target = medieval_string(value, "target_directory", 0, e);
    if (!target)
        return 0;
    if (!r.legacy) {
        id = medieval_string(value, "directory_identity", 0, e);
        sid = medieval_string(value, "owner_sid", 0, e);
        key = medieval_string(value, "registration_key", 0, e);
        hash = medieval_string(value, "uninstaller_sha256", 0, e);
        if (!id || !sid || !key || !hash)
            return 0;
        if (strcmp(target, identity->target) || strcmp(id, identity->guard->identity)) {
            patch_error_set(e, "installation_moved",
                            "Patch state belongs to a different directory or filesystem object.",
                            ERROR_INVALID_DATA);
            return 0;
        }
        if (strcmp(sid, identity->owner_sid)) {
            patch_error_set(e, "wrong_account", "Run the patch with the Windows account that installed it.",
                            ERROR_ACCESS_DENIED);
            return 0;
        }
        if (strcmp(key, identity->registration_key) || !medieval_hash_valid(hash))
            return json_invalid(e, "Invalid removal identity.");
        r.uninstaller_sha256 = hash;
    } else
        r.uninstaller_sha256 = "";
    r.installation_id = medieval_string(value, "installation_id", 0, e);
    r.status = medieval_string(value, "status", 0, e);
    hash = medieval_string(value, "target_executable_sha256", 0, e);
    files = json_get(value, "files");
    if (!r.installation_id || !r.status || !hash)
        return 0;
    if (!medieval_guid_valid(r.installation_id) || strcmp(hash, MEDIEVAL_EXECUTABLE_HASH) ||
        (strcmp(r.status, "installed") && strcmp(r.status, "installing") && strcmp(r.status, "restored")) ||
        !files || files->type != JSON_OBJECT || files->count != 5)
        return json_invalid(e, "Invalid installation identity or file table.");
    r.installer_version = medieval_string(value, "installer_version", 1, e);
    r.preinstall_mode = medieval_string(value, "preinstall_mode", 1, e);
    if (!r.installer_version || !r.preinstall_mode)
        return 0;
    for (i = 0; i < 5; ++i) {
        MedievalFileState *f = &r.files[i];
        f->name = medieval_payload_names[i];
        rec = json_get(files, f->name);
        if (!medieval_boolean(rec, "existed", &existed, e) ||
            !medieval_boolean(rec, "sidecar_created", &created, e))
            return 0;
        hash = medieval_string(rec, "installed_sha256", 0, e);
        if (!hash || !medieval_hash_valid(hash) ||
            !medieval_integer(rec, "installed_length", 0, &length, e) || length <= 0)
            return json_invalid(e, "Invalid installed file record.");
        f->installed.exists = 1;
        memcpy(f->installed.sha256, hash, 65);
        f->installed.length = (uint64_t)length;
        original = medieval_string(rec, "original_sha256", 1, e);
        snapshot = medieval_string(rec, "snapshot_relative", 1, e);
        sidecar = medieval_string(rec, "sidecar_relative", 1, e);
        legacy_hash = medieval_string(rec, "legacy_original_sha256", 1, e);
        if (!original || !snapshot || !sidecar || !legacy_hash ||
            !medieval_integer(rec, "original_length", 1, &length, e))
            return 0;
        snprintf(expected, sizeof(expected), "originals/%s", f->name);
        if ((existed && (strcmp(snapshot, expected) || !medieval_hash_valid(original) || length < 0)) ||
            (!existed && (*snapshot || *original || length)))
            return json_invalid(e, "Unsafe or invalid original record.");
        snprintf(expected, sizeof(expected), "%s.unofficial-patch.bak", f->name);
        if ((*sidecar && strcmp(sidecar, expected)) || (created && (!existed || !*sidecar)) ||
            (*legacy_hash && !medieval_hash_valid(legacy_hash)))
            return json_invalid(e, "Unsafe or inconsistent sidecar record.");
        f->original.exists = existed;
        f->original.length = (uint64_t)length;
        if (existed)
            memcpy(f->original.sha256, original, 65);
        f->snapshot_relative = snapshot;
        f->sidecar_relative = sidecar;
        f->sidecar_created = created;
        f->legacy_original_sha256 = legacy_hash;
        f->json = rec;
    }
    r.json = value;
    r.target_directory = target;
    *out = r;
    return 1;
}
static inline int medieval_receipt_verify_originals(const MedievalIdentity *identity,
                                                    const MedievalReceipt *receipt, PatchError *e) {
    PatchContext local = {0};
    wchar_t *state = NULL, *path = NULL;
    PatchFileRecord actual;
    size_t i;
    int ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    if (!patch_path_join(&local, identity->guard->canonical, MEDIEVAL_STATE_NAME, &state, e))
        goto done;
    for (i = 0; i < 5; ++i)
        if (receipt->files[i].original.exists) {
            if (!patch_path_join(&local, state, receipt->files[i].snapshot_relative, &path, e) ||
                !patch_file_record(identity->guard, path, &actual, e))
                goto done;
            if (!patch_record_equal(&actual, &receipt->files[i].original)) {
                json_invalid(e, "A private original is missing or damaged. Preserve the recovery state.");
                goto done;
            }
            patch_context_free(&local, path);
            path = NULL;
        }
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
static inline int medieval_text_path_equal(const MedievalIdentity *identity, const char *a, const char *b,
                                           int *equal, PatchError *e) {
    PatchContext local = {0};
    wchar_t *x = NULL, *y = NULL;
    int ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    if (patch_utf8_to_wide(&local, a, strlen(a), &x, e) && patch_utf8_to_wide(&local, b, strlen(b), &y, e))
        ok = patch_path_equal(&identity->registry.platform, x, y, equal, e);
    patch_context_close(&local);
    return ok;
}
static inline int medieval_registry_string(const PatchRegistry *registry,
                                           const PatchRegistrySnapshot *snapshot, const char *name,
                                           const char **out, PatchError *e) {
    const JsonValue *entry, *v;
    *out = "";
    if (!patch_registry_find(registry, snapshot, name, &entry, e))
        return 0;
    if (!entry)
        return 1;
    v = json_get(entry, "value");
    if (v->type != JSON_STRING)
        return 1;
    *out = json_cstring(v, e);
    return *out != NULL;
}
static inline int medieval_uninstall_command(PatchContext *context, const MedievalIdentity *identity,
                                             int legacy, char **out, PatchError *e) {
    const char *suffix =
        legacy ? "\\" MEDIEVAL_STATE_NAME "\\Uninstall.exe\"" : "\\" MEDIEVAL_UNINSTALL_NAME "\"";
    size_t suffix_length = strlen(suffix), size;
    void *memory = NULL;
    if (!patch_size_add(identity->target_length, suffix_length + 2, &size) ||
        !patch_alloc(context, size, 1, &memory, e))
        return 0;
    *out = (char *)memory;
    (*out)[0] = '"';
    memcpy(*out + 1, identity->target, identity->target_length);
    memcpy(*out + 1 + identity->target_length, suffix, suffix_length + 1);
    return 1;
}
/* An installed after-state must carry the complete identity binding, even when
   a pre-existing empty or personal-only key would be safe to adopt. */
static inline int medieval_registry_installed_identity(const MedievalIdentity *identity,
                                                        const PatchRegistrySnapshot *snapshot,
                                                        const char *installation_id, PatchError *e) {
    const char *product, *owner, *id, *location, *command;
    char *expected = NULL;
    PatchContext local = {0};
    int equal, ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    if (!snapshot->exists)
        goto conflict;
    if (!medieval_registry_string(&identity->registry, snapshot, "ProductId", &product, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "OwnerSid", &owner, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "InstallationId", &id, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "InstallLocation", &location, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "UninstallString", &command, e))
        goto done;
    if (strcmp(product, MEDIEVAL_PRODUCT_ID) || strcmp(owner, identity->owner_sid) ||
        strcmp(id, installation_id))
        goto conflict;
    if (!medieval_text_path_equal(identity, location, identity->target, &equal, e))
        goto done;
    if (!equal)
        goto conflict;
    if (!medieval_uninstall_command(&local, identity, 0, &expected, e) ||
        !medieval_text_path_equal(identity, command, expected, &equal, e))
        goto done;
    if (!equal)
        goto conflict;
    ok = 1;
    goto done;
conflict:
    patch_error_set(e, "registry_identity_conflict",
                    "Windows uninstall entry belongs to another installation or account.",
                    ERROR_INVALID_DATA);
done:
    patch_context_close(&local);
    return ok;
}
static inline int medieval_registry_ownership(const MedievalIdentity *identity,
                                              const PatchRegistrySnapshot *snapshot,
                                              const char *installation_id, PatchError *e) {
    const JsonValue *v;
    int owned;
    if (!snapshot->exists)
        return 1;
    for (v = snapshot->values->child; v; v = v->next) {
        if (!patch_registry_owned_name(&identity->registry, json_get(v, "name")->string, &owned, e))
            return 0;
        if (owned)
            return medieval_registry_installed_identity(identity, snapshot, installation_id, e);
    }
    return 1;
}
static inline int medieval_legacy_owned(const MedievalIdentity *identity,
                                        const PatchRegistrySnapshot *snapshot, int *owned,
                                        PatchError *e) {
    const char *location, *command, *publisher, *display;
    char *expected = NULL, *trimmed = NULL;
    size_t n;
    PatchContext local = {0};
    void *memory = NULL;
    int equal, ok = 0;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    *owned = 0;
    if (!snapshot->exists)
        return 1;
    if (!medieval_registry_string(&identity->registry, snapshot, "InstallLocation", &location, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "UninstallString", &command, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "Publisher", &publisher, e) ||
        !medieval_registry_string(&identity->registry, snapshot, "DisplayName", &display, e))
        goto done;
    if (strcmp(publisher, "Louie Woolger") ||
        (strcmp(display, MEDIEVAL_PRODUCT_NAME) && strcmp(display, "Unofficial Medieval: Total War Patch"))) {
        ok = 1;
        goto done;
    }
    n = strlen(location);
    /* Discovery and recovery share this bounded trailing-separator policy.
       Keep the captured value intact; no child, sibling or prefix normalization. */
    while (n && location[n - 1] == '\\')
        --n;
    if (!patch_alloc(&local, n + 1, 1, &memory, e))
        goto done;
    trimmed = (char *)memory;
    memcpy(trimmed, location, n);
    if (!medieval_text_path_equal(identity, trimmed, identity->target, &equal, e))
        goto done;
    if (!equal) {
        ok = 1;
        goto done;
    }
    if (!medieval_uninstall_command(&local, identity, 1, &expected, e) ||
        !medieval_text_path_equal(identity, command, expected, &equal, e))
        goto done;
    *owned = equal;
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
/* Role values: runtime, sidecar, private original, receipt, legacy uninstaller,
   current uninstaller. These are the complete lifecycle path allowlist. */
static inline int medieval_path_role(const char *relative, size_t *file_index) {
    size_t i;
    char path[160];
    for (i = 0; i < 5; ++i) {
        *file_index = i;
        if (!strcmp(relative, medieval_payload_names[i]))
            return 1;
        snprintf(path, sizeof(path), "%s.unofficial-patch.bak", medieval_payload_names[i]);
        if (!strcmp(relative, path))
            return 2;
        snprintf(path, sizeof(path), MEDIEVAL_STATE_NAME "/originals/%s", medieval_payload_names[i]);
        if (!strcmp(relative, path))
            return 3;
    }
    *file_index = 0;
    if (!strcmp(relative, MEDIEVAL_STATE_NAME "/" MEDIEVAL_RECEIPT_NAME))
        return 4;
    if (!strcmp(relative, MEDIEVAL_STATE_NAME "/Uninstall.exe"))
        return 5;
    return !strcmp(relative, MEDIEVAL_UNINSTALL_NAME) ? 6 : 0;
}
static inline int medieval_registry_unknown_preserved(const PatchRegistry *registry,
                                                      const PatchRegistrySnapshot *before,
                                                      const PatchRegistrySnapshot *after, PatchError *e) {
    const JsonValue *v, *other;
    unsigned side;
    int owned;
    if (before->subkeys != after->subkeys)
        return json_invalid(e, "Registry subkeys would not be preserved.");
    for (side = 0; side < 2; ++side) {
        const PatchRegistrySnapshot *a = side ? after : before, *b = side ? before : after;
        for (v = a->values->child; v; v = v->next) {
            if (!patch_registry_owned_name(registry, json_get(v, "name")->string, &owned, e))
                return 0;
            if (!owned) {
                if (!patch_registry_find(registry, b, json_get(v, "name")->string, &other, e))
                    return 0;
                if (!patch_registry_same_data(v, other))
                    return json_invalid(e, "Unknown registry values would not be preserved.");
            }
        }
    }
    return 1;
}
static inline int medieval_journal_validate(JsonDocument *owner, const JsonValue *value,
                                            const MedievalIdentity *identity, MedievalJournal *out,
                                            PatchError *e) {
    MedievalJournal j = {0};
    const char *schema, *target, *directory, *sid, *kind;
    const JsonValue *actions, *a, *retained;
    size_t i = 0, k, index;
    int64_t started;
    unsigned required = 0;
    int role, restore, own, legacy, owned;
    REGSAM view, previous_view;
    PatchError nested = {0};
    memset(out, 0, sizeof(*out));
    schema = medieval_string(value, "schema", 0, e);
    if (!schema || strcmp(schema, MEDIEVAL_JOURNAL_SCHEMA) || !medieval_integrity_valid(owner, value, e))
        return json_invalid(e, "Invalid transaction schema or integrity.");
    target = medieval_string(value, "target", 0, e);
    directory = medieval_string(value, "directory_identity", 0, e);
    sid = medieval_string(value, "owner_sid", 0, e);
    j.operation = medieval_string(value, "operation", 0, e);
    j.phase = medieval_string(value, "phase", 0, e);
    j.installation_id = medieval_string(value, "installation_id", 0, e);
    actions = json_get(value, "actions");
    if (!target || !directory || !sid || !j.operation || !j.phase || !j.installation_id ||
        !medieval_integer(value, "started", 0, &started, e))
        return 0;
    restore = !strcmp(j.operation, "restore");
    if (strcmp(target, identity->target) || strcmp(directory, identity->guard->identity) ||
        strcmp(sid, identity->owner_sid) || (!restore && strcmp(j.operation, "install")) ||
        (strcmp(j.phase, "prepared") && strcmp(j.phase, "applying") && strcmp(j.phase, "committed") &&
         strcmp(j.phase, "rolled-back")) ||
        !actions || actions->type != JSON_ARRAY || actions->count < 8 || actions->count > 48 || started < 0 ||
        (uint64_t)started > actions->count || (!strcmp(j.phase, "prepared") && started) ||
        (!strcmp(j.phase, "committed") && (uint64_t)started != actions->count))
        return json_invalid(e, "Transaction belongs to another directory/account or has invalid progress.");
    retained = json_get(value, restore ? "removal_receipt" : "install_receipt");
    if (!medieval_receipt_validate(owner, retained, identity, 0, &j.receipt, &nested)) {
        if (nested.code)
            patch_error_set(e, nested.code, nested.message, nested.win32);
        return 0;
    }
    if (strcmp(j.installation_id, j.receipt.installation_id) || strcmp(j.receipt.status, "installed"))
        return json_invalid(e, "Transaction and retained receipt identify different installations.");
    /* PS v2 journals contain state_existed; old records missing it mean false,
       matching the original script/default-field semantics. */
    a = json_get(value, "state_existed");
    if (a && a->type != JSON_NULL) {
        if (a->type != JSON_BOOL)
            return json_invalid(e, "Invalid state existence flag.");
        j.state_existed = a->boolean;
    }
    if (!medieval_string(value, "recovery_archive", 1, e))
        return 0;
    for (a = actions->child; a; a = a->next, ++i) {
        MedievalAction *action = &j.actions[i];
        action->json = a;
        kind = medieval_string(a, "kind", 0, e);
        if (!kind)
            return 0;
        if (!strcmp(kind, "file")) {
            action->relative = medieval_string(a, "relative", 0, e);
            if (!action->relative)
                return 0;
            role = medieval_path_role(action->relative, &index);
            if (!role)
                return json_invalid(e, "Recovery file is outside the patch allowlist.");
            for (k = 0; k < i; ++k)
                if (!j.actions[k].is_registry && !strcmp(j.actions[k].relative, action->relative))
                    return json_invalid(e, "Duplicate transaction file.");
            if (!medieval_file_record(json_get(a, "before"), &action->before_file, e) ||
                !medieval_file_record(json_get(a, "after"), &action->after_file, e))
                return 0;
            if (role == 1) {
                const PatchFileRecord *expected =
                    restore ? &j.receipt.files[index].original : &j.receipt.files[index].installed;
                if (!patch_record_equal(&action->after_file, expected))
                    return json_invalid(e, "Runtime recovery differs from retained receipt.");
                required |= 1U << index;
            } else if (role == 6) {
                if (!action->after_file.exists ||
                    strcmp(action->after_file.sha256, j.receipt.uninstaller_sha256) ||
                    (restore && (!action->before_file.exists ||
                                 strcmp(action->before_file.sha256, j.receipt.uninstaller_sha256))))
                    return json_invalid(e, "Recovery uninstaller is not bound to receipt.");
                required |= 32;
            } else if (role == 4) {
                if (!action->after_file.exists || !action->after_file.length ||
                    (restore && !action->before_file.exists))
                    return json_invalid(e, "Incomplete recovery receipt action.");
                required |= 64;
            } else if (restore) {
                const MedievalFileState *f = &j.receipt.files[index];
                if (role != 2 || !f->sidecar_created ||
                    !patch_record_equal(&action->before_file, &f->original) || action->after_file.exists)
                    return json_invalid(e, "Unowned removal sidecar action.");
            } else if (role == 2) {
                const MedievalFileState *f = &j.receipt.files[index];
                if (!f->sidecar_created || action->before_file.exists ||
                    !patch_record_equal(&action->after_file, &f->original))
                    return json_invalid(e, "Unowned installation sidecar action.");
            } else if (role == 3) {
                const MedievalFileState *f = &j.receipt.files[index];
                if (action->after_file.exists) {
                    if (!patch_record_equal(&action->after_file, &f->original))
                        return json_invalid(e, "Private original differs from retained receipt.");
                } else if (!*f->legacy_original_sha256 || !action->before_file.exists ||
                           strcmp(action->before_file.sha256, f->legacy_original_sha256))
                    return json_invalid(e, "Unowned private original retirement.");
            } else if (role == 5 && action->after_file.exists)
                return json_invalid(e, "Legacy uninstaller cannot be introduced.");
        } else if (!strcmp(kind, "registry")) {
            action->is_registry = 1;
            action->hive = medieval_string(a, "hive", 0, e);
            action->view = medieval_string(a, "view", 0, e);
            action->name = medieval_string(a, "name", 0, e);
            if (!action->hive || !action->view || !action->name ||
                !patch_registry_view(&identity->registry, action->view, &view, e))
                return 0;
            own = !strcmp(action->hive, "CurrentUser") && !strcmp(action->view, "Registry32") &&
                  !strcmp(action->name, identity->registration_key);
            legacy = (!strcmp(action->hive, "CurrentUser") || !strcmp(action->hive, "LocalMachine")) &&
                     (!strcmp(action->name, patch_legacy_registry_names[0]) ||
                      !strcmp(action->name, patch_legacy_registry_names[1]));
            if (!own && !legacy)
                return json_invalid(e, "Recovery names an unrelated registry key.");
            for (k = 0; k < i; ++k)
                if (j.actions[k].is_registry && !strcmp(j.actions[k].hive, action->hive) &&
                    !strcmp(j.actions[k].name, action->name)) {
                    if (!patch_registry_view(&identity->registry, j.actions[k].view, &previous_view, e))
                        return 0;
                    if (view == previous_view || !strcmp(action->hive, "CurrentUser"))
                        return json_invalid(e, "Duplicate physical registry action.");
                }
            if (!patch_registry_validate(&identity->registry, json_get(a, "before"), &action->before_registry,
                                         e) ||
                !patch_registry_validate(&identity->registry, json_get(a, "after"), &action->after_registry,
                                         e))
                return 0;
            if (own && !medieval_registry_ownership(identity, &action->before_registry, j.installation_id, e))
                return 0;
            if (!medieval_registry_unknown_preserved(&identity->registry, &action->before_registry,
                                                     &action->after_registry, e))
                return 0;
            if (restore || legacy) {
                const JsonValue *v;
                if (legacy && restore)
                    return json_invalid(e, "Legacy removal only belongs to migration.");
                for (v = action->after_registry.values->child; v; v = v->next) {
                    if (!patch_registry_owned_name(&identity->registry, json_get(v, "name")->string, &owned,
                                                   e))
                        return 0;
                    if (owned)
                        return json_invalid(e, "Registry removal retains owned values.");
                }
                if (action->after_registry.exists !=
                    (action->after_registry.values->count > 0 || action->after_registry.subkeys > 0))
                    return json_invalid(e, "Invalid removed registry existence.");
            } else if (!medieval_registry_installed_identity(identity, &action->after_registry,
                                                              j.installation_id, e))
                return json_invalid(e, "Invalid installed registration identity.");
            if (legacy && action->before_registry.exists) {
                if (!medieval_legacy_owned(identity, &action->before_registry, &owned, e))
                    return 0;
                if (!owned)
                    return json_invalid(e, "Legacy registration does not belong to this folder.");
            }
            if (own)
                required |= 128;
        } else
            return json_invalid(e, "Unknown transaction action.");
    }
    if (required != 255)
        return json_invalid(e, "Recovery omits a required runtime, receipt, uninstaller or registration.");
    j.json = value;
    j.count = actions->count;
    j.started = (size_t)started;
    *out = j;
    return 1;
}
static inline int medieval_journal_verify_copies(const MedievalIdentity *identity,
                                                 const MedievalJournal *journal, const wchar_t *transaction,
                                                 PatchError *e) {
    PatchContext local = {0};
    PatchFileRecord actual;
    size_t i;
    unsigned side;
    char relative[32];
    wchar_t *path = NULL;
    int ok = 0;
    if (!strcmp(journal->phase, "committed") || !strcmp(journal->phase, "rolled-back"))
        return 1;
    local.allocator = identity->context.allocator;
    local.memory_limit = identity->context.memory_limit;
    for (i = 0; i < journal->count; ++i)
        if (!journal->actions[i].is_registry)
            for (side = 0; side < 2; ++side) {
                const PatchFileRecord *expected =
                    side ? &journal->actions[i].after_file : &journal->actions[i].before_file;
                if (!expected->exists)
                    continue;
                snprintf(relative, sizeof(relative), "%s/%03u.bin", side ? "after" : "before", (unsigned)i);
                if (!patch_path_join(&local, transaction, relative, &path, e) ||
                    !patch_file_record(identity->guard, path, &actual, e))
                    goto done;
                if (!patch_record_equal(&actual, expected)) {
                    json_invalid(e, "Missing or damaged transaction copy.");
                    goto done;
                }
                patch_context_free(&local, path);
                path = NULL;
            }
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
static inline int medieval_old_patch(const char *mode) {
    return !strcmp(mode, "r186") || !strcmp(mode, "r185") ||
           !strcmp(mode, "dust-only") || !strcmp(mode, "r6f160");
}
static inline int medieval_old_configuration(const char *hash) {
    return !strcmp(hash, "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA") ||
           !strcmp(hash, "BD21E07D4B9282A8CA0F53613CCB852D419E5D54967A96C8E34A60D2F96E476A");
}
static inline const char *medieval_known_d3d9_mode(const char *hash) {
    static const struct {
        const char *hash, *mode;
    } known[] = {{"E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6", "stock-dgvoodoo"},
                 {"34F855A17C10B6BBCEFD844B99A5B5A7F442DECDDFFDB0D898645FA2FF0B0F0C", "dust-only"},
                 {"E421B9A1FF5927A8B93FD7B2A83E0C965EB1EA596D924561713C16F323924892", "r6f160"},
                 {"A3FFCC0BCDD74044448BF0418F0FA732594F23D025DE667172AFE118CD82F9FA", "r185"},
                 {"3EE7EE33946F9F73A61559C23505AFCC27D45E61067644AF611B09F627297AD8", "r185"},
                 {"060136A2BE50A0F209FF74F242FFC264C6709C48135C8EB362771DCBF25931F1", "r185"},
                 {"C0D597364734EAEA26ABA83F1FA6B8875B830E4D626D5CF41F42DAEB92106CB4", "r185"},
                 {"9D1C8B4E5C0CF0A224FFEB8C20F762C52B1D8C2B306162A639D81295B103D04E", "r185"},
                 {"E3D5D6D5E214D79592B7D6F7F26D52CDFF5A59EB028CCD9DE499900BCFF18D74", "r185"},
                 {"F5F7EEDB312D251ECE1CCC726A08E866020C89A3C00212216B6A70FC0F5D6BE0", "r185"},
                 {"CC3537E286863FA75200DFB80839F07E07D8AD91F2ABBB2A4C5629677CE965FD", "r185"},
                 {"8FD9B9CE5809C52E2C815754ECD391D6ACDA14D33518A70BA9B4BE1FFD5DC7DF", "r185"},
                 {"D61A5DB23EE091CAE0D97AB5E385D42BD301E97D7F9A4FB6F3A3CA1484E7B932", "r185"},
                 {"E2B6F73CCA21467E14505AFF8DA31B8454D4D7546C499285670ED6A2FF4E0DF5", "r185"},
                 {"3A11DF858C9B1339297B5E78684C07722DB6A2A3C378173FEE8E640B19F53F67", "r185"},
                 {"B7F1FEA6588BDED9270E6A51E1CA80FE20F1F32A87112101212A8D476287384F", "r185"},
                 {"E5703EA5E8B5F33F8CD6F41B50125D2187F73C50DA3DC8F621F4E160D28B4BC9", "r185"},
                 {"FD1271790E739C86D29E4EE776A4621A03236192AB436AE24B0836B26B423E4C", "r185"},
                 {"EDF4B6CB4F2DEF7563E15AF46AFE3A8708F240B6FE1EC174D14DC2FAA61D49A3", "r185"},
                 {"B07D861994FBDEC15956BFF5FB79882A9D2E630CF72ECDA07BC3EBE7E5248875", "r185"},
                 {"516D61823F0629856199C0E6F888F4CF803B711CC569058D0D88D0D72F0C18FA", "r185"},
                 {"48ED6EF8DB1197A0E1240CB73B2217F66CA863F7A4E4568DB00ADD4E4288F6CF", "r185"},
                 {"CBB6A16CE535640B4FDB6526F42E575EF882E4CFE232BA8CF8BAAF8735E8596A", "r185"},
                 {"9791F095DB66817A4CB6A6B23DF4983254950F7F1266D1CDC3040E1E9FEFDE4C", "r186"},
                 {"AD7E922E1F160C045325E75107E507E54807F426BFD8102A1808E969AD67CFCA", "r186"},
                 {"24E0C23B0C1424F77201A83D449D22165694D6D3535D187BC9FD3247DC2A8F0E", "r186"}};
    size_t i;
    for (i = 0; i < sizeof(known) / sizeof(*known); ++i)
        if (!strcmp(hash, known[i].hash))
            return known[i].mode;
    return "";
}
static inline int medieval_known_old_runtime(const char *name, const char *hash, const char *payload_hash) {
    if (!strcmp(name, "D3D9.dll"))
        return medieval_old_patch(medieval_known_d3d9_mode(hash));
    return !strcmp(hash, payload_hash) ||
           (!strcmp(name, "dgVoodoo.conf") && medieval_old_configuration(hash));
}
typedef struct {
    int preserve_current, archive_current;
    const char *baseline_kind;
} MedievalBaselineDecision;
static inline MedievalBaselineDecision medieval_baseline_decision(const char *name,
                                                                  const PatchFileRecord *current,
                                                                  const char *mode,
                                                                  const char *payload_hash) {
    MedievalBaselineDecision result = {current->exists, 0, "verified-preinstall"};
    if (medieval_old_patch(mode) && current->exists) {
        result.archive_current = 1;
        if (strcmp(name, "dgVoodoo.conf") || !strcmp(current->sha256, payload_hash) ||
            medieval_old_configuration(current->sha256)) {
            result.preserve_current = 0;
            result.baseline_kind = "older-patch-archived";
        }
    }
    return result;
}
/* Draft creation preserves ordered members and the earliest baseline; it is
   deliberately unsealed until Task 4 fills files/completed_utc/archive fields.
   new_guid and utc are supplied by the lifecycle clock/UUID owner. */
static inline int medieval_receipt_draft(JsonDocument *doc, const MedievalIdentity *identity,
                                         const MedievalReceipt *previous, const char *mode,
                                         const char *uninstaller_hash, const char *version,
                                         const char *new_guid, const char *utc,
                                         const JsonValue *locked_settings, const char *installer_hash,
                                         JsonValue **out, PatchError *e) {
    JsonValue *r = NULL, *locked;
    const char *restoration;
    int equal;
    int64_t count;
    *out = NULL;
    if (!medieval_hash_valid(uninstaller_hash) || !medieval_guid_valid(new_guid) ||
        (installer_hash && !medieval_hash_valid(installer_hash)))
        return json_invalid(e, "Invalid receipt draft identity.");
#define MS_TEXT(k, s)                                                                                        \
    do {                                                                                                     \
        if (!json_set(doc, r, (k), json_text(doc, (s), e), e))                                               \
            return 0;                                                                                        \
    } while (0)
#define MS_VALUE(k, v)                                                                                       \
    do {                                                                                                     \
        if (!json_set(doc, r, (k), (v), e))                                                                  \
            return 0;                                                                                        \
    } while (0)
    if (previous) {
        r = json_clone(doc, previous->json, e);
        if (!r)
            return 0;
        if (!medieval_integer(previous->json, "repair_count", 1, &count, e))
            return 0;
        if (count < 0 || count == INT64_MAX)
            return json_invalid(e, "Repair count exceeds supported range.");
        MS_VALUE("repair_count", json_number(doc, count + 1, e));
        MS_TEXT("last_repaired_utc", utc);
        if (previous->legacy) {
            if (!medieval_text_path_equal(identity, previous->target_directory, identity->target, &equal, e))
                return 0;
            if (!equal) {
                MS_TEXT("legacy_installation_id", previous->installation_id);
                MS_TEXT("legacy_target_directory", previous->target_directory);
                MS_TEXT("installation_id", new_guid);
            }
        }
    } else {
        r = json_new(doc, JSON_OBJECT, e);
        if (!r)
            return 0;
        MS_TEXT("schema", MEDIEVAL_RECEIPT_SCHEMA);
        MS_TEXT("status", "installed");
        MS_TEXT("installation_id", new_guid);
        MS_TEXT("installer_version", version);
        MS_VALUE("installer_sha256", json_new(doc, JSON_NULL, e));
        MS_TEXT("installed_utc", utc);
        MS_TEXT("target_directory", identity->target);
        MS_TEXT("target_executable_sha256", MEDIEVAL_EXECUTABLE_HASH);
        MS_TEXT("preinstall_mode", mode);
        MS_VALUE("repair_count", json_number(doc, 0, e));
        MS_VALUE("files", json_new(doc, JSON_OBJECT, e));
        locked = locked_settings ? json_clone(doc, locked_settings, e) : json_new(doc, JSON_NULL, e);
        MS_VALUE("locked_settings", locked);
    }
    MS_TEXT("schema", MEDIEVAL_RECEIPT_SCHEMA);
    MS_TEXT("status", "installed");
    MS_TEXT("target_directory", identity->target);
    MS_TEXT("directory_identity", identity->guard->identity);
    MS_TEXT("owner_sid", identity->owner_sid);
    MS_TEXT("registration_key", identity->registration_key);
    MS_TEXT("uninstaller_sha256", uninstaller_hash);
    MS_TEXT("installer_version", version);
    locked = locked_settings ? json_clone(doc, locked_settings, e) : json_new(doc, JSON_NULL, e);
    MS_VALUE("locked_settings", locked);
    restoration = medieval_string(r, "restoration_kind", 1, e);
    if (!restoration)
        return 0;
    MS_TEXT("restoration_kind", *restoration ? restoration : "verified-preinstall");
    if (installer_hash)
        MS_TEXT("installer_sha256", installer_hash);
#undef MS_TEXT
#undef MS_VALUE
    *out = r;
    return 1;
}
static inline int medieval_registry_add(JsonDocument *doc, JsonValue *values, const char *name,
                                        JsonValue *value, PatchError *e) {
    JsonValue *entry = json_new(doc, JSON_OBJECT, e);
    if (!entry || !value)
        return 0;
    return json_set(doc, entry, "name", json_text(doc, name, e), e) &&
           json_set(doc, entry, "kind", json_text(doc, value->type == JSON_NUMBER ? "DWord" : "String", e),
                    e) &&
           json_set(doc, entry, "value", value, e) && json_append(values, entry, e);
}
static inline int medieval_registry_installed(JsonDocument *doc, const MedievalIdentity *identity,
                                              const MedievalReceipt *receipt,
                                              const PatchRegistrySnapshot *before, const char *edition,
                                              JsonValue **out, PatchError *e) {
    JsonValue *removed = NULL, *values;
    char *command = NULL, *display = NULL, *suffix = NULL;
    void *memory = NULL;
    size_t n;
    PatchContext local = {0};
    int ok = 0;
    *out = NULL;
    local.allocator = doc->context.allocator;
    local.memory_limit = doc->context.memory_limit;
    if (strcmp(edition, "GOG") && strcmp(edition, "Steam") && strcmp(edition, "Game copy"))
        return json_invalid(e, "Unknown edition label.");
    if (!medieval_registry_ownership(identity, before, receipt->installation_id, e) ||
        !patch_registry_removed(&identity->registry, doc, before, &removed, e))
        goto done;
    values = json_clone(doc, json_get(removed, "values"), e);
    if (!values)
        goto done;
    if (!medieval_uninstall_command(&local, identity, 0, &command, e))
        goto done;
    n = identity->target_length + strlen(edition) + 64;
    if (!patch_alloc(&local, n, 1, &memory, e))
        goto done;
    display = (char *)memory;
    snprintf(display, n, "Unofficial Medieval Patch [%.8s] (%s - %s)", identity->registration_key + 24,
             edition, identity->target);
#define MR_TEXT(k, s)                                                                                        \
    do {                                                                                                     \
        if (!medieval_registry_add(doc, values, (k), json_text(doc, (s), e), e))                             \
            goto done;                                                                                       \
    } while (0)
    MR_TEXT("DisplayName", display);
    MR_TEXT("DisplayVersion", receipt->installer_version);
    MR_TEXT("Publisher", "Louie Woolger");
    MR_TEXT("InstallLocation", identity->target);
    n = strlen(command);
    if (!patch_alloc(&local, n + 4, 1, &memory, e))
        goto done;
    suffix = (char *)memory;
    memcpy(suffix, command, n);
    memcpy(suffix + n, ",0", 3);
    MR_TEXT("DisplayIcon", suffix);
    MR_TEXT("UninstallString", command);
    memcpy(suffix + n, " /S", 4);
    MR_TEXT("QuietUninstallString", suffix);
    if (!medieval_registry_add(doc, values, "NoModify", json_number(doc, 1, e), e) ||
        !medieval_registry_add(doc, values, "NoRepair", json_number(doc, 1, e), e))
        goto done;
    MR_TEXT("URLInfoAbout", "https://github.com/LouieWoolger/medieval-total-war-collection-unofficial-patch");
    MR_TEXT("ProductId", MEDIEVAL_PRODUCT_ID);
    MR_TEXT("InstallationId", receipt->installation_id);
    MR_TEXT("OwnerSid", identity->owner_sid);
#undef MR_TEXT
    if (!patch_registry_sort(&identity->registry, values, e))
        goto done;
    ok = patch_registry_object(doc, 1, before->subkeys, values, out, e);
done:
    patch_context_close(&local);
    return ok;
}
/* Enumeration never duplicates HKLM view aliases on native 32-bit Windows. */
static inline int medieval_legacy_registrations(JsonDocument *doc, const MedievalIdentity *identity,
                                                JsonValue **out, PatchError *e) {
    unsigned h, v, n;
    JsonValue *found = json_new(doc, JSON_ARRAY, e), *before, *after, *entry;
    PatchRegistrySnapshot snapshot;
    int owned;
    *out = NULL;
    if (!found)
        return 0;
    for (h = 0; h < 2; ++h)
        for (v = 0; v < patch_registry_view_count(&identity->registry, h); ++v)
            for (n = 0; n < 2; ++n) {
                const char *hive = h ? "LocalMachine" : "CurrentUser",
                           *view = v ? "Registry64" : "Registry32", *name = patch_legacy_registry_names[n];
                if (!patch_registry_snapshot(&identity->registry, doc, hive, view, name, &before, e) ||
                    !patch_registry_validate(&identity->registry, before, &snapshot, e) ||
                    !medieval_legacy_owned(identity, &snapshot, &owned, e))
                    return 0;
                if (!owned)
                    continue;
                if (!patch_registry_removed(&identity->registry, doc, &snapshot, &after, e))
                    return 0;
                entry = json_new(doc, JSON_OBJECT, e);
                if (!entry || !json_set(doc, entry, "hive", json_text(doc, hive, e), e) ||
                    !json_set(doc, entry, "view", json_text(doc, view, e), e) ||
                    !json_set(doc, entry, "name", json_text(doc, name, e), e) ||
                    !json_set(doc, entry, "before", before, e) || !json_set(doc, entry, "after", after, e) ||
                    !json_append(found, entry, e))
                    return 0;
            }
    *out = found;
    return 1;
}
typedef struct {
    const char *file, *source_relative;
    PatchFileRecord record;
} MedievalArchiveRecord;
typedef struct {
    const JsonValue *json;
    const char *reason, *created_utc;
    size_t count;
    MedievalArchiveRecord files[48];
} MedievalArchive;
static inline int medieval_archive_validate(JsonDocument *doc, const JsonValue *value,
                                            const MedievalIdentity *identity, MedievalArchive *out,
                                            PatchError *e) {
    MedievalArchive archive = {0};
    const JsonValue *files, *v;
    const char *schema, *target, *hash;
    size_t i = 0, k, index;
    int64_t length;
    PatchContext local = {0};
    wchar_t *path = NULL;
    int ok = 0;
    memset(out, 0, sizeof(*out));
    schema = medieval_string(value, "schema", 0, e);
    target = medieval_string(value, "target", 0, e);
    if (!schema || !target || strcmp(schema, "unofficial-medieval-patch-recovery-v1") ||
        strcmp(target, identity->target) || !medieval_integrity_valid(doc, value, e))
        return json_invalid(e, "Invalid recovery archive binding.");
    archive.reason = medieval_string(value, "reason", 0, e);
    archive.created_utc = medieval_string(value, "created_utc", 0, e);
    files = json_get(value, "files");
    if (!archive.reason || !archive.created_utc || !files || files->type != JSON_ARRAY || files->count > 48)
        return json_invalid(e, "Invalid recovery archive records.");
    for (v = files->child; v; v = v->next, ++i) {
        MedievalArchiveRecord *r = &archive.files[i];
        r->file = medieval_string(v, "file", 0, e);
        r->source_relative = medieval_string(v, "source_relative", 0, e);
        hash = medieval_string(v, "sha256", 0, e);
        if (!r->file || !r->source_relative || !hash || !medieval_integer(v, "length", 0, &length, e))
            goto done;
        if (!*r->file || strpbrk(r->file, "\\/:") || !strcmp(r->file, "manifest.json") ||
            !strcmp(r->file, "README.txt") || !medieval_hash_valid(hash) || length < 0 ||
            !medieval_path_role(r->source_relative, &index))
            goto bad;
        for (k = 0; k < i; ++k)
            if (!strcmp(archive.files[k].file, r->file))
                goto bad;
        if (!patch_path_join(&local, identity->guard->canonical, r->file, &path, e))
            goto done;
        patch_context_free(&local, path);
        path = NULL;
        r->record.exists = 1;
        r->record.length = (uint64_t)length;
        memcpy(r->record.sha256, hash, 65);
    }
    archive.json = value;
    archive.count = files->count;
    *out = archive;
    ok = 1;
    goto done;
bad:
    json_invalid(e, "Unsafe recovery archive record.");
done:
    patch_context_close(&local);
    return ok;
}
#endif
