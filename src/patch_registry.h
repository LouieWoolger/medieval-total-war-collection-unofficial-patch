#ifndef MTW_PATCH_REGISTRY_H
#define MTW_PATCH_REGISTRY_H
/* Medieval typed registry snapshots. Windows identities intentionally use the
   invariant-lower UTF-8 path hash used by the prior C++ and PowerShell engines. */
#include "patch_json.h"
#include <sddl.h>

#define MEDIEVAL_PRODUCT_ID "unofficial-medieval-total-war-collection-patch"
#define MEDIEVAL_PRODUCT_NAME "Unofficial Medieval: Total War Collection Patch"
#define MEDIEVAL_UNINSTALL_NAME "Uninstall Unofficial Medieval Patch.exe"
#define PATCH_REGISTRY_PREFIX L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
static const char *const patch_owned_registry_names[] = {
    "DisplayName",     "DisplayVersion",       "Publisher", "InstallLocation", "DisplayIcon",
    "UninstallString", "QuietUninstallString", "NoModify",  "NoRepair",        "URLInfoAbout",
    "ProductId",       "InstallationId",       "OwnerSid"};
static const char *const patch_legacy_registry_names[] = {"Unofficial Medieval Total War Collection Patch",
                                                          "Unofficial Medieval Total War Patch"};
typedef LSTATUS(WINAPI *PatchRegOpenFn)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LSTATUS(WINAPI *PatchRegCreateFn)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM,
                                          const LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
typedef LSTATUS(WINAPI *PatchRegDeleteExFn)(HKEY, LPCWSTR, REGSAM, DWORD);
typedef LSTATUS(WINAPI *PatchRegDeleteFn)(HKEY, LPCWSTR);
typedef void(WINAPI *PatchNativeSystemFn)(LPSYSTEM_INFO);
typedef BOOL(WINAPI *PatchWow64Fn)(HANDLE, PBOOL);
typedef struct PatchRegistry {
    PatchPlatform platform;
    /* Configure after init, before use. Temporary allocations inherit these. */
    PatchAllocator allocator;
    size_t memory_limit;
    int initialized, os64;
    PatchRegOpenFn open;
    PatchRegCreateFn create;
    PatchRegDeleteExFn delete_ex;
    PatchRegDeleteFn delete_default;
} PatchRegistry;
typedef struct {
    int exists;
    DWORD subkeys;
    const JsonValue *json, *values;
} PatchRegistrySnapshot;

static inline void patch_registry_context(const PatchRegistry *r, PatchContext *context) {
    context->allocator = r->allocator;
    context->memory_limit = r->memory_limit;
}

static inline int patch_registry_init(PatchRegistry *r, PatchError *e) {
    HMODULE advapi = GetModuleHandleW(L"advapi32.dll"), kernel = GetModuleHandleW(L"kernel32.dll");
    FARPROC p;
    PatchNativeSystemFn native_system = NULL;
    PatchWow64Fn wow64 = NULL;
    SYSTEM_INFO info;
    BOOL wow = FALSE;
    if (!r) {
        patch_error_set(e, "invalid_argument", "Registry context required.", ERROR_INVALID_PARAMETER);
        return 0;
    }
    memset(r, 0, sizeof(*r));
    if (sizeof(void *) != 4)
        goto unsupported; /* Fallback deletion below is bound to this PE32 process's default view. */
    patch_platform_init(&r->platform);
    r->open = RegOpenKeyExW;
    r->create = RegCreateKeyExW;
    r->delete_default = RegDeleteKeyW;
    p = advapi ? GetProcAddress(advapi, "RegDeleteKeyExW") : NULL;
    if (!p && kernel)
        p = GetProcAddress(kernel, "RegDeleteKeyExW");
    memcpy(&r->delete_ex, &p, sizeof(r->delete_ex));
    p = kernel ? GetProcAddress(kernel, "GetNativeSystemInfo") : NULL;
    memcpy(&native_system, &p, sizeof(native_system));
    if (native_system) {
        memset(&info, 0, sizeof(info));
        native_system(&info);
        if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
            r->os64 = 0;
        else if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
                 info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64 ||
                 info.wProcessorArchitecture == 12)
            r->os64 = 1;
        else
            goto unsupported;
    } else {
        p = kernel ? GetProcAddress(kernel, "IsWow64Process") : NULL;
        memcpy(&wow64, &p, sizeof(wow64));
        if (!wow64 || !wow64(GetCurrentProcess(), &wow))
            goto unsupported;
        r->os64 = !!wow;
    }
    r->initialized = 1;
#ifdef PATCH_REGISTRY_CUSTOMIZE
    PATCH_REGISTRY_CUSTOMIZE(r);
#endif
    return 1;
unsupported:
    patch_error_set(e, "platform_unsupported", "Cannot establish the native registry view.",
                    ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}
static inline int patch_registry_identity(PatchContext *context, const wchar_t *canonical, char key[57],
                                          char mutex[64], PatchError *e) {
    PatchContext temporary = {0};
    wchar_t *lower = NULL;
    char *utf8 = NULL, digest[65];
    size_t n = 0, i;
    int ok = 0;
    if (key)
        key[0] = 0;
    if (mutex)
        mutex[0] = 0;
    if (!context || !canonical || !key || !mutex) {
        patch_error_set(e, "invalid_argument", "Installation identity inputs required.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    temporary.allocator = context->allocator;
    temporary.memory_limit = context->memory_limit;
    if (!patch_invariant_lower(&temporary, canonical, &lower, e) ||
        !patch_wide_to_utf8(&temporary, lower, wcslen(lower), &utf8, &n, e))
        goto done;
    if (!patch_sha256_bytes(&temporary, utf8, n, digest)) {
        patch_error_set(e, temporary.error.code, temporary.error.message, GetLastError());
        goto done;
    }
    memcpy(key, "UnofficialMedievalPatch-", 24);
    for (i = 0; i < 32; ++i)
        key[24 + i] = digest[i] >= 'A' && digest[i] <= 'F' ? (char)(digest[i] + 32) : digest[i];
    key[56] = 0;
    memcpy(mutex, "Global\\", 7);
    memcpy(mutex + 7, key, 57);
    ok = 1;
done:
    patch_context_close(&temporary);
    return ok;
}
static inline int patch_owner_sid(PatchContext *context, char **out, PatchError *e) {
    HANDLE token = NULL;
    DWORD n = 0, code;
    void *buffer = NULL;
    LPWSTR text = NULL;
    size_t length;
    int ok = 0;
    PatchContext local = {0};
    *out = NULL;
    local.allocator = context->allocator;
    local.memory_limit = context->memory_limit;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        goto winerror;
    GetTokenInformation(token, TokenUser, NULL, 0, &n);
    code = GetLastError();
    if (code != ERROR_INSUFFICIENT_BUFFER || n > 65536) {
        SetLastError(code);
        goto winerror;
    }
    if (!patch_alloc(&local, n, 1, &buffer, e))
        goto done;
    if (!GetTokenInformation(token, TokenUser, buffer, n, &n) ||
        !ConvertSidToStringSidW(((TOKEN_USER *)buffer)->User.Sid, &text))
        goto winerror;
    ok = patch_wide_to_utf8(context, text, wcslen(text), out, &length, e);
    goto done;
winerror:
    patch_error_set(e, "wrong_account", "Cannot identify the installing Windows account.", GetLastError());
done:
    if (text)
        LocalFree(text);
    if (token)
        CloseHandle(token);
    patch_context_close(&local);
    return ok;
}
static inline int patch_registry_view(const PatchRegistry *r, const char *view, REGSAM *out, PatchError *e) {
    if (!r || !r->initialized)
        return json_invalid(e, "Registry platform is not initialized.");
    if (!strcmp(view, "Registry32")) {
        *out = KEY_WOW64_32KEY;
        return 1;
    }
    if (!strcmp(view, "Registry64")) {
        *out = r->os64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
        return 1;
    }
    return json_invalid(e, "Invalid registry view.");
}
static inline unsigned patch_registry_view_count(const PatchRegistry *r, int local_machine) {
    return local_machine && r->os64 ? 2 : 1;
}
static inline int patch_registry_name_compare(const PatchRegistry *r, const wchar_t *a, const wchar_t *b,
                                              int *order, PatchError *e) {
    size_t i = 0;
    unsigned x, y;
    int result;
    if (r->platform.compare_ordinal) {
        result = r->platform.compare_ordinal(a, -1, b, -1, TRUE);
        if (!result) {
            patch_error_set(e, "registry_invalid", "Cannot compare registry names.", GetLastError());
            return 0;
        }
        *order = result - CSTR_EQUAL;
        return 1;
    }
    if (!r->platform.upcase) {
        patch_error_set(e, "platform_unsupported", "Ordinal registry comparison unavailable.",
                        ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    }
    do {
        x = r->platform.upcase(a[i]);
        y = r->platform.upcase(b[i]);
        if (x != y) {
            *order = x < y ? -1 : 1;
            return 1;
        }
        ++i;
    } while (x);
    *order = 0;
    return 1;
}
static inline int patch_registry_kind(const JsonValue *kind, DWORD *type, PatchError *e) {
    static const char *const names[] = {"None",  "String",      "ExpandString", "Binary",
                                        "DWord", "MultiString", "QWord"};
    static const DWORD types[] = {REG_NONE,  REG_SZ,       REG_EXPAND_SZ, REG_BINARY,
                                  REG_DWORD, REG_MULTI_SZ, REG_QWORD};
    size_t i;
    for (i = 0; i < 7; ++i)
        if (json_is_text(kind, names[i])) {
            *type = types[i];
            return 1;
        }
    return json_invalid(e, "Unsupported registry value type.");
}
static inline const char *patch_registry_kind_name(DWORD type) {
    switch (type) {
    case REG_NONE:
        return "None";
    case REG_SZ:
        return "String";
    case REG_EXPAND_SZ:
        return "ExpandString";
    case REG_BINARY:
        return "Binary";
    case REG_DWORD:
        return "DWord";
    case REG_QWORD:
        return "QWord";
    case REG_MULTI_SZ:
        return "MultiString";
    default:
        return NULL;
    }
}
static inline int patch_registry_owned_name(const PatchRegistry *r, const char *name, int *owned,
                                            PatchError *e) {
    wchar_t wide[32], candidate[32];
    size_t pos = 0, n = strlen(name), units = 0, i, j;
    uint32_t scalar;
    int order;
    *owned = 0;
    while (pos < n) {
        if (!patch_utf8_scalar((const unsigned char *)name, n, &pos, &scalar))
            return json_invalid(e, "Invalid registry value name.");
        /* No owned ASCII name has this length or a supplementary equivalent. */
        if (scalar > 0xffff || units == 31)
            return 1;
        wide[units++] = (wchar_t)scalar;
    }
    wide[units] = 0;
    for (i = 0; i < sizeof(patch_owned_registry_names) / sizeof(*patch_owned_registry_names); ++i) {
        const char *text = patch_owned_registry_names[i];
        for (j = 0; text[j]; ++j)
            candidate[j] = (wchar_t)(unsigned char)text[j];
        candidate[j] = 0;
        if (!patch_registry_name_compare(r, wide, candidate, &order, e))
            return 0;
        if (!order) {
            *owned = 1;
            return 1;
        }
    }
    return 1;
}
static inline int patch_registry_validate(const PatchRegistry *r, const JsonValue *value,
                                          PatchRegistrySnapshot *out, PatchError *e) {
    const JsonValue *exists = json_get(value, "exists"), *subkeys = json_get(value, "subkeys"),
                    *values = json_get(value, "values"), *v, *part, *data;
    PatchContext local = {0};
    void *memory = NULL;
    wchar_t **names;
    size_t i = 0, j;
    DWORD type;
    int order, ok = 0;
    const char *name;
    memset(out, 0, sizeof(*out));
    patch_registry_context(r, &local);
    if (!exists || exists->type != JSON_BOOL || !subkeys || subkeys->type != JSON_NUMBER ||
        subkeys->number < 0 || subkeys->number > UINT32_MAX || !values || values->type != JSON_ARRAY ||
        values->count > 4096 || (!exists->boolean && (values->count || subkeys->number)))
        return json_invalid(e, "Invalid registry snapshot.");
    if (!patch_alloc(&local, values->count, sizeof(wchar_t *), &memory, e))
        goto done;
    names = (wchar_t **)memory;
    for (v = values->child; v; v = v->next) {
        const JsonValue *nv = json_get(v, "name");
        name = json_cstring(nv, e);
        if (!name || !patch_utf8_to_wide(&local, name, nv->length, &names[i], e))
            goto done;
        if (wcslen(names[i]) > 32767)
            goto bad;
        for (j = 0; j < i; ++j) {
            if (!patch_registry_name_compare(r, names[i], names[j], &order, e))
                goto done;
            if (!order)
                goto bad;
        }
        ++i;
        if (!patch_registry_kind(json_get(v, "kind"), &type, e))
            goto done;
        data = json_get(v, "value");
        if (!data)
            goto bad;
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            if (data->type != JSON_STRING || !json_utf8_valid(data->string, data->length))
                goto bad;
        } else if (type == REG_DWORD || type == REG_QWORD) {
            if (data->type != JSON_NUMBER ||
                (type == REG_DWORD && (data->number < INT32_MIN || data->number > INT32_MAX)))
                goto bad;
        } else {
            if (data->type != JSON_ARRAY)
                goto bad;
            for (part = data->child; part; part = part->next) {
                if (type == REG_MULTI_SZ) {
                    if (!json_cstring(part, e) || !part->length)
                        goto bad;
                } else if (part->type != JSON_NUMBER || part->number < 0 || part->number > 255)
                    goto bad;
            }
        }
    }
    out->exists = exists->boolean;
    out->subkeys = (DWORD)subkeys->number;
    out->values = values;
    out->json = value;
    ok = 1;
    goto done;
bad:
    json_invalid(e, "Malformed or duplicate registry value.");
done:
    patch_context_close(&local);
    return ok;
}
static inline int patch_registry_find(const PatchRegistry *r, const PatchRegistrySnapshot *s,
                                      const char *name, const JsonValue **out, PatchError *e) {
    PatchContext local = {0};
    wchar_t *wanted = NULL, *candidate = NULL;
    const JsonValue *v;
    int order, ok = 0;
    *out = NULL;
    patch_registry_context(r, &local);
    if (!patch_utf8_to_wide(&local, name, strlen(name), &wanted, e))
        goto done;
    for (v = s->values->child; v; v = v->next) {
        const JsonValue *nv = json_get(v, "name");
        if (!patch_utf8_to_wide(&local, nv->string, nv->length, &candidate, e) ||
            !patch_registry_name_compare(r, wanted, candidate, &order, e))
            goto done;
        patch_context_free(&local, candidate);
        candidate = NULL;
        if (!order) {
            *out = v;
            break;
        }
    }
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
static inline int patch_registry_equal_value(const JsonValue *a, const JsonValue *b) {
    const JsonValue *x, *y;
    if (!a || !b || a->type != b->type)
        return 0;
    switch (a->type) {
    case JSON_NULL:
        return 1;
    case JSON_BOOL:
        return a->boolean == b->boolean;
    case JSON_NUMBER:
        return a->number == b->number;
    case JSON_STRING:
        return a->length == b->length && !memcmp(a->string, b->string, a->length);
    case JSON_ARRAY:
        if (a->count != b->count)
            return 0;
        for (x = a->child, y = b->child; x && y; x = x->next, y = y->next)
            if (!patch_registry_equal_value(x, y))
                return 0;
        return !x && !y;
    default:
        return 0;
    }
}
static inline int patch_registry_same_data(const JsonValue *a, const JsonValue *b) {
    return a && b && patch_registry_equal_value(json_get(a, "kind"), json_get(b, "kind")) &&
           patch_registry_equal_value(json_get(a, "value"), json_get(b, "value"));
}
static inline int patch_registry_equal(const PatchRegistry *r, const PatchRegistrySnapshot *a,
                                       const PatchRegistrySnapshot *b, int *equal, PatchError *e) {
    const JsonValue *v, *other;
    *equal = 0;
    if (a->exists != b->exists || a->subkeys != b->subkeys || a->values->count != b->values->count)
        return 1;
    for (v = a->values->child; v; v = v->next) {
        if (!patch_registry_find(r, b, json_get(v, "name")->string, &other, e))
            return 0;
        if (!patch_registry_same_data(v, other))
            return 1;
    }
    *equal = 1;
    return 1;
}
/* Expected lifecycle contents may encode absence (including historical v2/C++
   journals). Removing owned values retains the physical key: Windows has no
   conditional empty-key delete, so deleting it could destroy a late writer's
   data. Only two content-free snapshots are equivalent here. Physical snapshot
   equality above and all nonempty values/subkeys remain strict. */
static inline int patch_registry_expected_equal(const PatchRegistry *r, const PatchRegistrySnapshot *a,
                                                const PatchRegistrySnapshot *b, int *equal, PatchError *e) {
    if (!a->subkeys && !b->subkeys && !a->values->count && !b->values->count) {
        *equal = 1;
        return 1;
    }
    return patch_registry_equal(r, a, b, equal, e);
}
static inline int patch_registry_object(JsonDocument *doc, int exists, DWORD subkeys, JsonValue *values,
                                        JsonValue **out, PatchError *e) {
    JsonValue *object = json_new(doc, JSON_OBJECT, e);
    *out = NULL;
    if (!object || !json_set(doc, object, "exists", json_bool(doc, exists, e), e) ||
        !json_set(doc, object, "values", values, e) ||
        !json_set(doc, object, "subkeys", json_number(doc, subkeys, e), e))
        return 0;
    *out = object;
    return 1;
}
static inline int patch_registry_removed(const PatchRegistry *registry, JsonDocument *doc,
                                         const PatchRegistrySnapshot *before, JsonValue **out,
                                         PatchError *e) {
    JsonValue *values = json_new(doc, JSON_ARRAY, e), *copy;
    const JsonValue *v;
    int owned;
    if (!values)
        return 0;
    for (v = before->values->child; v; v = v->next) {
        if (!patch_registry_owned_name(registry, json_get(v, "name")->string, &owned, e))
            return 0;
        if (!owned) {
            copy = json_clone(doc, v, e);
            if (!copy || !json_append(values, copy, e))
                return 0;
        }
    }
    return patch_registry_object(doc, values->count || before->subkeys, before->subkeys, values, out, e);
}
static inline int patch_registry_path(PatchContext *context, const char *hive, const char *name, HKEY *root,
                                      wchar_t **out, PatchError *e) {
    wchar_t *w = NULL;
    size_t n, prefix = wcslen(PATCH_REGISTRY_PREFIX);
    void *memory = NULL;
    if (!strcmp(hive, "CurrentUser"))
        *root = HKEY_CURRENT_USER;
    else if (!strcmp(hive, "LocalMachine"))
        *root = HKEY_LOCAL_MACHINE;
    else
        return json_invalid(e, "Invalid registry hive.");
    if (!name || !*name || strchr(name, '\\') || strchr(name, '/'))
        return json_invalid(e, "Invalid registry key name.");
    if (!patch_utf8_to_wide(context, name, strlen(name), &w, e))
        return 0;
    n = wcslen(w);
    if (n > 32767 - prefix)
        return json_invalid(e, "Registry key name too long.");
    if (!patch_alloc(context, prefix + n + 1, sizeof(wchar_t), &memory, e))
        return 0;
    *out = (wchar_t *)memory;
    memcpy(*out, PATCH_REGISTRY_PREFIX, prefix * sizeof(wchar_t));
    memcpy(*out + prefix, w, (n + 1) * sizeof(wchar_t));
    return 1;
}
static inline int patch_registry_error(PatchError *e, LSTATUS status, const char *code, const char *message) {
    if (status == ERROR_SUCCESS)
        return 1;
    patch_error_set(e, code, message, (DWORD)status);
    return 0;
}
static inline int patch_registry_decode(JsonDocument *doc, DWORD type, const unsigned char *bytes, DWORD size,
                                        JsonValue **out, PatchError *e) {
    JsonValue *v = NULL, *part;
    const wchar_t *w = (const wchar_t *)bytes;
    size_t units = size / 2, pos, end, length;
    char *text = NULL;
    int64_t q;
    int32_t d;
    DWORD i;
    *out = NULL;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        if (size % 2 || units < 1 || w[units - 1] != 0)
            goto bad;
        if (!patch_wide_to_utf8(&doc->context, w, units - 1, &text, &length, e))
            return 0;
        v = json_new(doc, JSON_STRING, e);
        if (!v)
            return 0;
        v->string = text;
        v->length = length;
    } else if (type == REG_DWORD) {
        if (size != 4)
            goto bad;
        memcpy(&d, bytes, 4);
        v = json_number(doc, d, e);
    } else if (type == REG_QWORD) {
        if (size != 8)
            goto bad;
        memcpy(&q, bytes, 8);
        v = json_number(doc, q, e);
    } else if (type == REG_BINARY || type == REG_NONE) {
        v = json_new(doc, JSON_ARRAY, e);
        if (!v)
            return 0;
        for (i = 0; i < size; ++i)
            if (!json_append(v, json_number(doc, bytes[i], e), e))
                return 0;
    } else if (type == REG_MULTI_SZ) {
        if (size % 2 || units < 1 || w[units - 1])
            goto bad;
        v = json_new(doc, JSON_ARRAY, e);
        if (!v)
            return 0;
        if ((units == 1 || units == 2) && !w[0]) {
            *out = v;
            return 1;
        }
        if (units < 2 || w[units - 2])
            goto bad;
        pos = 0;
        while (pos < units - 1) {
            end = pos;
            while (end < units && w[end])
                ++end;
            if (end == pos || end >= units - 1)
                goto bad;
            if (!patch_wide_to_utf8(&doc->context, w + pos, end - pos, &text, &length, e))
                return 0;
            part = json_new(doc, JSON_STRING, e);
            if (!part)
                return 0;
            part->string = text;
            part->length = length;
            if (!json_append(v, part, e))
                return 0;
            pos = end + 1;
        }
        if (pos != units - 1)
            goto bad;
    } else
        goto bad;
    *out = v;
    return v != NULL;
bad:
    patch_error_set(e, "registry_invalid", "Malformed or unsupported registry data was preserved.",
                    ERROR_INVALID_DATA);
    return 0;
}
static inline int patch_registry_sort(const PatchRegistry *r, JsonValue *values, PatchError *e) {
    typedef struct {
        JsonValue *v;
        wchar_t *name;
    } Item;
    PatchContext local = {0};
    void *memory = NULL;
    Item *items, item;
    JsonValue *v;
    size_t i = 0, j;
    int order, ok = 0;
    patch_registry_context(r, &local);
    if (!patch_alloc(&local, values->count, sizeof(Item), &memory, e))
        goto done;
    items = (Item *)memory;
    for (v = values->child; v; v = v->next) {
        const JsonValue *name = json_get(v, "name");
        items[i].v = v;
        if (!patch_utf8_to_wide(&local, name->string, name->length, &items[i].name, e))
            goto done;
        ++i;
    }
    for (i = 1; i < values->count; ++i) {
        item = items[i];
        j = i;
        while (j) {
            if (!patch_registry_name_compare(r, items[j - 1].name, item.name, &order, e))
                goto done;
            if (order <= 0)
                break;
            items[j] = items[j - 1];
            --j;
        }
        items[j] = item;
    }
    for (i = 0; i < values->count; ++i)
        items[i].v->next = i + 1 < values->count ? items[i + 1].v : NULL;
    values->child = values->count ? items[0].v : NULL;
    values->last = values->count ? items[values->count - 1].v : NULL;
    ok = 1;
done:
    patch_context_close(&local);
    return ok;
}
static inline int patch_registry_snapshot(const PatchRegistry *r, JsonDocument *doc, const char *hive,
                                          const char *view, const char *name, JsonValue **out,
                                          PatchError *e) {
    PatchContext local = {0};
    HKEY root, key = NULL;
    wchar_t *path = NULL, *vn = NULL;
    void *memory = NULL;
    unsigned char *bytes = NULL;
    REGSAM access;
    LSTATUS status;
    DWORD subkeys = 0, count = 0, maxname = 0, maxdata = 0, i, nn, nb, type;
    JsonValue *values = NULL, *value, *entry;
    char *utf8 = NULL;
    size_t n;
    int ok = 0;
    *out = NULL;
    local.allocator = doc->context.allocator;
    local.memory_limit = doc->context.memory_limit;
    if (!patch_registry_path(&local, hive, name, &root, &path, e) ||
        !patch_registry_view(r, view, &access, e))
        goto done;
    values = json_new(doc, JSON_ARRAY, e);
    if (!values)
        goto done;
    status = r->open(root, path, 0, KEY_READ | access, &key);
    if (status == ERROR_FILE_NOT_FOUND) {
        ok = patch_registry_object(doc, 0, 0, values, out, e);
        goto done;
    }
    if (!patch_registry_error(e, status, "registry_read_failed", "Cannot inspect patch registration."))
        goto done;
    status =
        RegQueryInfoKeyW(key, NULL, NULL, NULL, &subkeys, NULL, NULL, &count, &maxname, &maxdata, NULL, NULL);
    if (!patch_registry_error(e, status, "registry_read_failed", "Cannot enumerate registry metadata."))
        goto done;
    if (count > 4096 || maxname > 32767 || maxdata > JSON_MAX_BYTES) {
        patch_error_set(e, "registry_invalid", "Registry metadata exceeds safe limits.", ERROR_INVALID_DATA);
        goto done;
    }
    if (!patch_alloc(&local, maxname + 2, sizeof(wchar_t), &memory, e))
        goto done;
    vn = (wchar_t *)memory;
    if (!patch_alloc(&local, maxdata + 4, 1, &memory, e))
        goto done;
    bytes = (unsigned char *)memory;
    for (i = 0; i < count; ++i) {
        nn = maxname + 1;
        nb = maxdata + 2;
        type = 0;
        status = RegEnumValueW(key, i, vn, &nn, NULL, &type, bytes, &nb);
        if (!patch_registry_error(e, status, "registry_read_failed",
                                  "Registry changed or value could not be read."))
            goto done;
        if (nn > maxname || nb > maxdata || !patch_registry_kind_name(type)) {
            patch_error_set(e, "registry_invalid", "Unsupported registry value was preserved.",
                            ERROR_INVALID_DATA);
            goto done;
        }
        if (wmemchr(vn, 0, nn)) {
            json_invalid(e, "Registry name contains NUL.");
            goto done;
        }
        if (!patch_registry_decode(doc, type, bytes, nb, &value, e) ||
            !patch_wide_to_utf8(&doc->context, vn, nn, &utf8, &n, e))
            goto done;
        entry = json_new(doc, JSON_OBJECT, e);
        if (!entry || !json_set(doc, entry, "name", json_string(doc, utf8, n, e), e) ||
            !json_set(doc, entry, "kind", json_text(doc, patch_registry_kind_name(type), e), e) ||
            !json_set(doc, entry, "value", value, e) || !json_append(values, entry, e))
            goto done;
    }
    if (!patch_registry_sort(r, values, e))
        goto done;
    ok = patch_registry_object(doc, 1, subkeys, values, out, e);
done:
    if (key)
        RegCloseKey(key);
    patch_context_close(&local);
    return ok;
}
static inline int patch_registry_encode(PatchContext *context, const JsonValue *entry, DWORD *type,
                                        unsigned char **bytes, DWORD *size, PatchError *e) {
    const JsonValue *v = json_get(entry, "value"), *p;
    wchar_t *w = NULL;
    size_t n = 0, total = 0, position = 0;
    void *memory = NULL;
    int32_t d;
    int64_t q;
    *bytes = NULL;
    *size = 0;
    if (!patch_registry_kind(json_get(entry, "kind"), type, e))
        return 0;
    if (*type == REG_SZ || *type == REG_EXPAND_SZ) {
        if (!patch_utf8_to_wide(context, v->string, v->length, &w, e))
            return 0;
        /* UTF-16 units are counted explicitly; wcslen would truncate REG_SZ NULs. */
        for (n = 0, position = 0; position < v->length;) {
            uint32_t scalar;
            if (!patch_utf8_scalar((const unsigned char *)v->string, v->length, &position, &scalar))
                return 0;
            n += scalar > 0xffff ? 2 : 1;
        }
        total = (n + 1) * 2;
        *bytes = (unsigned char *)w;
    } else if (*type == REG_DWORD || *type == REG_QWORD) {
        total = *type == REG_DWORD ? 4 : 8;
        if (!patch_alloc(context, total, 1, &memory, e))
            return 0;
        d = (int32_t)v->number;
        q = v->number;
        memcpy(memory, *type == REG_DWORD ? (const void *)&d : (const void *)&q, total);
        *bytes = (unsigned char *)memory;
    } else if (*type == REG_MULTI_SZ) {
        total = 2;
        for (p = v->child; p; p = p->next) {
            if (!patch_utf8_to_wide(context, p->string, p->length, &w, e))
                return 0;
            n = wcslen(w);
            if (!patch_size_add(total, (n + 1) * 2, &total))
                return json_invalid(e, "Registry value size overflow.");
            patch_context_free(context, w);
        }
        if (!v->count)
            total = 4;
        if (total > JSON_MAX_BYTES)
            return json_invalid(e, "Registry value exceeds safe limits.");
        if (!patch_alloc(context, total, 1, &memory, e))
            return 0;
        *bytes = (unsigned char *)memory;
        for (p = v->child; p; p = p->next) {
            if (!patch_utf8_to_wide(context, p->string, p->length, &w, e))
                return 0;
            n = (wcslen(w) + 1) * 2;
            memcpy(*bytes + position, w, n);
            position += n;
            patch_context_free(context, w);
        }
    } else {
        total = v->count;
        if (!patch_alloc(context, total, 1, &memory, e))
            return 0;
        *bytes = (unsigned char *)memory;
        for (p = v->child; p; p = p->next)
            (*bytes)[position++] = (unsigned char)p->number;
    }
    if (total > JSON_MAX_BYTES)
        return json_invalid(e, "Registry value exceeds safe limits.");
    *size = (DWORD)total;
    return 1;
}
/* Probe access without creating keys or changing values. Missing keys require
   create-subkey access on the nearest existing ancestor, not on an invented
   temporary key. Existing entries only need value-write access. */
static inline int patch_registry_writable(const PatchRegistry *r, const char *hive, const char *view,
                                          const char *name, PatchError *e) {
    PatchContext local = {0};
    HKEY root, key = NULL;
    wchar_t *path = NULL, *end;
    REGSAM access;
    LSTATUS status = ERROR_INVALID_PARAMETER;
    int ok = 0;
    patch_registry_context(r, &local);
    if (!patch_registry_path(&local, hive, name, &root, &path, e) ||
        !patch_registry_view(r, view, &access, e)) goto done;
    status = r->open(root, path, 0, KEY_READ | KEY_SET_VALUE | access, &key);
    while (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        end = wcsrchr(path, L'\\');
        if (!*path) break;
        if (end) *end = 0;
        else *path = 0;
        status = r->open(root, path, 0, KEY_CREATE_SUB_KEY | access, &key);
    }
    if (status == ERROR_ACCESS_DENIED && !strcmp(hive, "LocalMachine"))
        patch_error_set(e, "elevation_required",
                        "Updating an older patch entry for all Windows users needs administrator permission. Run setup as administrator using the same Windows account. No new patch changes were applied.", status);
    else ok = patch_registry_error(e, status, "registry_access_denied",
                                    "Windows denied access to the patch registration. Check this account's registry permissions before retrying.");
done:
    if (key) RegCloseKey(key);
    patch_context_close(&local);
    return ok;
}
/* Preflight all unknown values before touching owned values. Unknown values are
   never rewritten: equal explicit-length data is retained byte-for-byte; a
   changed/missing unknown value or changed subkey count is a conflict. */
static inline int patch_registry_set(const PatchRegistry *r, const char *hive, const char *view,
                                     const char *name, const JsonValue *desired, PatchError *e) {
    JsonDocument current_doc = {0}, verify_doc = {0};
    PatchContext local = {0};
    PatchRegistrySnapshot target, before, after;
    JsonValue *current = NULL, *verified = NULL;
    const JsonValue *v, *other;
    HKEY root, key = NULL;
    wchar_t *path = NULL, *vn = NULL;
    REGSAM access;
    LSTATUS status;
    unsigned char *bytes;
    DWORD type, size;
    int equal, owned, ok = 0;
    patch_registry_context(r, &local);
    patch_registry_context(r, &current_doc.context);
    patch_registry_context(r, &verify_doc.context);
    if (!patch_registry_validate(r, desired, &target, e) ||
        !patch_registry_path(&local, hive, name, &root, &path, e) ||
        !patch_registry_view(r, view, &access, e))
        goto done;
    if (!patch_registry_snapshot(r, &current_doc, hive, view, name, &current, e) ||
        !patch_registry_validate(r, current, &before, e))
        goto done;
    if (before.subkeys != target.subkeys)
        goto conflict;
    for (v = before.values->child; v; v = v->next) {
        if (!patch_registry_owned_name(r, json_get(v, "name")->string, &owned, e))
            goto done;
        if (!owned) {
            if (!patch_registry_find(r, &target, json_get(v, "name")->string, &other, e))
                goto done;
            if (!patch_registry_same_data(v, other))
                goto conflict;
        }
    }
    for (v = target.values->child; v; v = v->next) {
        if (!patch_registry_owned_name(r, json_get(v, "name")->string, &owned, e))
            goto done;
        if (!owned) {
            if (!patch_registry_find(r, &before, json_get(v, "name")->string, &other, e))
                goto done;
            if (!patch_registry_same_data(v, other))
                goto conflict;
        }
    }
    if (!patch_registry_expected_equal(r, &target, &before, &equal, e)) goto done;
    /* In particular, rollback must not request write access to an action that
       failed before modifying its registry entry. */
    if (equal) {
        ok = 1;
        goto done;
    }
    if (target.exists && !before.exists)
        status = r->create(root, path, 0, NULL, 0, KEY_READ | KEY_SET_VALUE | access, NULL, &key, NULL);
    else
        status = r->open(root, path, 0, KEY_READ | KEY_SET_VALUE | access, &key);
    if (!patch_registry_error(e, status, "registry_write_failed",
                              "Cannot open patch registration for writing."))
        goto done;
    for (v = before.values->child; v; v = v->next) {
        const char *n = json_get(v, "name")->string;
        if (!patch_registry_find(r, &target, n, &other, e))
            goto done;
        if (!other) {
            if (!patch_utf8_to_wide(&local, n, strlen(n), &vn, e))
                goto done;
            status = RegDeleteValueW(key, vn);
            patch_context_free(&local, vn);
            if (!patch_registry_error(e, status, "registry_write_failed",
                                      "Cannot delete owned registry value."))
                goto done;
        }
    }
    for (v = target.values->child; v; v = v->next) {
        const JsonValue *nv = json_get(v, "name");
        if (!patch_registry_owned_name(r, nv->string, &owned, e))
            goto done;
        if (!owned)
            continue;
        if (!patch_registry_encode(&local, v, &type, &bytes, &size, e) ||
            !patch_utf8_to_wide(&local, nv->string, nv->length, &vn, e))
            goto done;
        status = RegSetValueExW(key, vn, 0, type, bytes, size);
        patch_context_free(&local, bytes);
        patch_context_free(&local, vn);
        if (!patch_registry_error(e, status, "registry_write_failed", "Cannot write owned registry value."))
            goto done;
    }
    if (!patch_registry_error(e, RegFlushKey(key), "registry_write_failed",
                              "Cannot flush patch registration."))
        goto done;
    RegCloseKey(key);
    key = NULL;
    /* Never delete this container by name. Only the owned values above may be
       removed; a late unknown value or subkey survives and fails verification. */
    if (!patch_registry_snapshot(r, &verify_doc, hive, view, name, &verified, e) ||
        !patch_registry_validate(r, verified, &after, e) ||
        !patch_registry_expected_equal(r, &target, &after, &equal, e))
        goto done;
    if (!equal) {
        patch_error_set(e, "registry_verification_failed", "Patch registration verification failed.",
                        ERROR_INVALID_DATA);
        goto done;
    }
    ok = 1;
    goto done;
conflict:
    patch_error_set(e, "registry_cleanup_conflict", "Changed or unknown registry contents were preserved.",
                    ERROR_INVALID_DATA);
done:
    if (key)
        RegCloseKey(key);
    patch_context_close(&local);
    json_document_close(&current_doc);
    json_document_close(&verify_doc);
    return ok;
}
#endif
