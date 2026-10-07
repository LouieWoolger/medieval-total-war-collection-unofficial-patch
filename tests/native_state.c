/* Behavioural adapter for real C JSON/state/registry modules. No game mutations. */
// clang-format off: install the isolated registry routing before state headers.
#include "native_registry_override.c"
#include "medieval_state.h"
// clang-format on
#include "patch_json.h"
#include <stdio.h>

static int passed, failed;
static void check(int ok, const char *name) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok)
        ++passed;
    else
        ++failed;
}
typedef struct {
    size_t calls, fail_at, live;
} AllocatorTest;
static void *test_allocate(void *user, size_t n) {
    AllocatorTest *a = (AllocatorTest *)user;
    void *p;
    if (++a->calls == a->fail_at)
        return NULL;
    p = malloc(n);
    if (p)
        ++a->live;
    return p;
}
static void test_deallocate(void *user, void *p) {
    AllocatorTest *a = (AllocatorTest *)user;
    if (p)
        --a->live;
    free(p);
}
static int json_suite(void) {
    const char *bad[] = {"",
                         "{",
                         "[",
                         "{\"a\":1,\"a\":2}",
                         "{\"a\":1,\"\\u0061\":2}",
                         "[1,]",
                         "{\"x\":1,}",
                         "01",
                         "-01",
                         "1.0",
                         "1e2",
                         "9223372036854775808",
                         "-9223372036854775809",
                         "\"\\ud800\"",
                         "\"\\udc00\"",
                         "true false",
                         "\"\\q\"",
                         "{1:2}",
                         "[+1]",
                         "NaN",
                         "\"\xc0\xaf\"",
                         "\"\x01\"",
                         "\"\xed\xa0\x80\"",
                         "\"\xf4\x90\x80\x80\""};
    const char *sample = "{\"z\":null,\"a\":[true,false,-9223372036854775808,9223372036854775807,"
                         "\"\\ud83d\\ude00\"],\"nul\":\"a\\u0000b\"}";
    const char *expected = "{\"z\":null,\"a\":[true,false,-9223372036854775808,9223372036854775807,"
                           "\"\xf0\x9f\x98\x80\"],\"nul\":\"a\\u0000b\"}";
    JsonDocument doc = {0};
    PatchError error = {0};
    char *out = NULL;
    size_t n = 0, i, calls = 0;
    check(json_parse(sample, strlen(sample), &doc, &error), "ordered signed JSON parsed");
    check(json_write_canonical(&doc, &out, &n, &error) && n == strlen(expected) && !memcmp(out, expected, n),
          "canonical order numbers surrogate and embedded NUL");
    check(json_get(doc.root, "nul")->length == 3, "decoded NUL explicit length");
    check(!json_cstring(json_get(doc.root, "nul"), &error), "C-string consumer rejects embedded NUL");
    {
        int64_t integer = 0;
        memset(&error, 0, sizeof(error));
        check(!medieval_integer(doc.root, "a", 0, &integer, &error), "JSON wrong typed accessor refused");
        memset(&error, 0, sizeof(error));
        check(!medieval_integer(doc.root, "missing", 0, &integer, &error), "JSON missing required member refused");
    }
    json_document_close(&doc);
    {
        JsonDocument other = {0};
        JsonValue *array, *item, *foreign;
        memset(&doc, 0, sizeof(doc));
        memset(&error, 0, sizeof(error));
        array = json_new(&doc, JSON_ARRAY, &error);
        item = json_number(&doc, 7, &error);
        foreign = json_number(&other, 9, &error);
        check(json_append(array, item, &error), "fresh JSON node attached");
        check(!json_append(array, item, &error), "already attached JSON node refused");
        check(!json_append(array, foreign, &error), "cross-document JSON node refused");
        json_document_close(&doc);
        json_document_close(&other);
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(&doc, 0, sizeof(doc));
        memset(&error, 0, sizeof(error));
        check(!json_parse(bad[i], strlen(bad[i]), &doc, &error) && error.code &&
                  !strcmp(error.code, "receipt_invalid") && !doc.root,
              "invalid JSON rejected with no tree");
        json_document_close(&doc);
    }
    {
        char depth[90];
        memset(depth, '[', 42);
        depth[42] = '0';
        memset(depth + 43, ']', 42);
        depth[85] = 0;
        memset(&doc, 0, sizeof(doc));
        memset(&error, 0, sizeof(error));
        check(!json_parse(depth, 85, &doc, &error), "depth bound");
        json_document_close(&doc);
    }
    {
        char *many = (char *)malloc(200004);
        many[0] = '[';
        for (i = 0; i < 100001; ++i) {
            many[1 + 2 * i] = '0';
            many[2 + 2 * i] = ',';
        }
        many[200002] = ']';
        memset(&doc, 0, sizeof(doc));
        memset(&error, 0, sizeof(error));
        check(!json_parse(many, 200003, &doc, &error), "node bound");
        json_document_close(&doc);
        free(many);
    }
    memset(&doc, 0, sizeof(doc));
    memset(&error, 0, sizeof(error));
    check(!json_parse("[]", JSON_MAX_BYTES + 1, &doc, &error), "byte bound before read");
    json_document_close(&doc);
    memset(&doc, 0, sizeof(doc));
    memset(&error, 0, sizeof(error));
    check(json_parse("\xef\xbb\xbf{}", 5, &doc, &error), "UTF8 BOM accepted");
    json_document_close(&doc);
    for (i = 0; i < 200; ++i) {
        AllocatorTest a = {0};
        int ok;
        memset(&doc, 0, sizeof(doc));
        memset(&error, 0, sizeof(error));
        a.fail_at = i;
        doc.context.allocator.user = &a;
        doc.context.allocator.allocate = test_allocate;
        doc.context.allocator.deallocate = test_deallocate;
        ok = json_parse(sample, strlen(sample), &doc, &error);
        if (ok)
            ok = json_write_canonical(&doc, &out, &n, &error);
        calls = a.calls;
        if (i && i <= calls)
            check(!ok && error.code && !strcmp(error.code, "allocation_failed"),
                  "allocation failure reported");
        json_document_close(&doc);
        check(a.live == 0, "allocation failure cleanup");
        if (i > calls)
            break;
    }
    printf("RESULT passed=%d failed=%d\n", passed, failed);
    return failed ? 1 : 0;
}
static int read_input(const wchar_t *path, char **out, size_t *n) {
    FILE *f = _wfopen(path, L"rb");
    long size;
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || size > 2100000 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return 0;
    }
    *out = (char *)malloc((size_t)size + 1);
    if (!*out) {
        fclose(f);
        return 0;
    }
    *n = fread(*out, 1, (size_t)size, f);
    fclose(f);
    (*out)[*n] = 0;
    return *n == (size_t)size;
}
int wmain(int argc, wchar_t **argv) {
    JsonDocument doc = {0};
    PatchError error = {0};
    char *input = NULL, *out = NULL;
    size_t n = 0, length = 0;
    int ok = 0;
    if (argc == 4 && !wcscmp(argv[1], L"--oom-state")) {
        PatchGuard guard = {0};
        size_t failure, total = 0;
        DWORD handles_before = 0, handles_after = 0;
        if (!read_input(argv[3], &input, &n) || !patch_guard_open(&guard, argv[2], &error))
            return 90;
        GetProcessHandleCount(GetCurrentProcess(), &handles_before);
        for (failure = 0; failure < 2500; ++failure) {
            AllocatorTest allocator = {0};
            MedievalIdentity identity = {0};
            MedievalReceipt receipt = {0};
            JsonValue *draft = NULL, *empty = NULL, *installed = NULL;
            PatchRegistrySnapshot snapshot = {0};
            memset(&doc, 0, sizeof(doc));
            memset(&error, 0, sizeof(error));
            allocator.fail_at = failure;
            doc.context.allocator.user = &allocator;
            doc.context.allocator.allocate = test_allocate;
            doc.context.allocator.deallocate = test_deallocate;
            identity.context.allocator = doc.context.allocator;
            ok = medieval_identity_open(&identity, &guard, &error) && json_parse(input, n, &doc, &error) &&
                 medieval_receipt_validate(&doc, doc.root, &identity, 1, &receipt, &error) &&
                 medieval_receipt_draft(&doc, &identity, &receipt, "clean", receipt.uninstaller_sha256,
                                        "1.0.0", "01234567-89ab-cdef-0123-456789abcdef",
                                        "2026-10-02T01:00:00Z", json_get(receipt.json, "locked_settings"),
                                        NULL, &draft, &error) &&
                 patch_registry_object(&doc, 0, 0, json_new(&doc, JSON_ARRAY, &error), &empty, &error) &&
                 patch_registry_validate(&identity.registry, empty, &snapshot, &error) &&
                 medieval_registry_installed(&doc, &identity, &receipt, &snapshot, "GOG", &installed, &error);
            if (ok) {
                doc.root = installed;
                ok = json_write_canonical(&doc, &out, &length, &error);
            }
            total = allocator.calls;
            if (failure && failure <= total &&
                (ok || !error.code || strcmp(error.code, "allocation_failed"))) {
                printf("FAIL allocation %u of %u error %s\n", (unsigned)failure, (unsigned)total,
                       error.code ? error.code : "none");
                ++failed;
            }
            json_document_close(&doc);
            medieval_identity_close(&identity);
            if (allocator.live) {
                printf("FAIL leaked allocation %u\n", (unsigned)failure);
                ++failed;
            }
            if (!ok && !failure) {
                printf("FAIL baseline %s\n", error.code ? error.code : "none");
                ++failed;
                break;
            }
            if (failure > total)
                break;
        }
        GetProcessHandleCount(GetCurrentProcess(), &handles_after);
        check(handles_before == handles_after, "state OOM handle cleanup");
        check(failure > total && total > 300, "all state allocation boundaries exercised");
        printf("OOM attempts=%u RESULT passed=%d failed=%d\n", (unsigned)failure, passed, failed);
        free(input);
        patch_guard_close(&guard);
        return failed ? 1 : 0;
    }
    if (argc == 2 && !wcscmp(argv[1], L"--json-suite"))
        return json_suite();
    if (argc == 2 && !wcscmp(argv[1], L"--migration-suite")) {
        PatchFileRecord current = {0};
        MedievalBaselineDecision decision;
        JsonValue *wire;
        check(!strcmp(medieval_known_d3d9_mode(
                          "E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6"),
                      "stock-dgvoodoo"),
              "stock wrapper classification");
        check(medieval_old_patch(medieval_known_d3d9_mode(
                  "A3FFCC0BCDD74044448BF0418F0FA732594F23D025DE667172AFE118CD82F9FA")),
              "older wrapper classification");
        check(medieval_old_patch(medieval_known_d3d9_mode(
                  "AD7E922E1F160C045325E75107E507E54807F426BFD8102A1808E969AD67CFCA")),
              "v2.87.5 scroll-enabled proxy classification");
        check(medieval_old_patch(medieval_known_d3d9_mode(
                  "24E0C23B0C1424F77201A83D449D22165694D6D3535D187BC9FD3247DC2A8F0E")),
              "v2.87.5 scroll-disabled proxy classification");
        check(medieval_old_patch(medieval_known_d3d9_mode(
                  "5E0E398BB5D10F2855928844A374E966FCDC71D28EAAA0D70BA17FA7ADE8838C")),
              "v2.87.5 sprite-disabled proxy classification");
        check(medieval_old_patch(medieval_known_d3d9_mode(
                  "300373700D0868CF2B1BA94762132A781E70918A01DB873DA3E8666FCD73B8F1")),
              "v2.87.5 scroll-and-sprite-disabled proxy classification");
        check(!*medieval_known_d3d9_mode("unknown"), "unknown wrapper refused");
        current.exists = 1;
        current.length = 42;
        memset(current.sha256, 'A', 64);
        strcpy(current.identity, "00000000:0000000000000001");
        decision = medieval_baseline_decision("D3D9.dll", &current, "r185", current.sha256);
        check(!decision.preserve_current && decision.archive_current,
              "older patched runtime cannot become clean original");
        decision = medieval_baseline_decision("dgVoodoo.conf", &current, "r185", "BBBB");
        check(decision.preserve_current && decision.archive_current,
              "personal configuration preserved and archived");
        decision = medieval_baseline_decision("dgVoodoo.conf", &current, "clean", current.sha256);
        check(decision.preserve_current && !decision.archive_current, "clean preinstall file retained");
        wire = medieval_record_json(&doc, &current, &error);
        doc.root = wire;
        check(wire && wire->count == 3 && !json_get(wire, "identity"),
              "transient file identity excluded from v2");
        json_document_close(&doc);
        printf("RESULT passed=%d failed=%d\n", passed, failed);
        return failed ? 1 : 0;
    }
    if (argc >= 3 && (!wcscmp(argv[1], L"--identity") || !wcscmp(argv[1], L"--receipt") ||
                      !wcscmp(argv[1], L"--journal") || !wcscmp(argv[1], L"--draft") ||
                      !wcscmp(argv[1], L"--installed-registry") || !wcscmp(argv[1], L"--archive"))) {
        PatchGuard guard = {0};
        MedievalIdentity identity = {0};
        MedievalReceipt receipt = {0};
        MedievalJournal journal = {0};
        ok = patch_guard_open(&guard, argv[2], &error) && medieval_identity_open(&identity, &guard, &error);
        if (ok && !wcscmp(argv[1], L"--identity")) {
            doc.root = json_new(&doc, JSON_OBJECT, &error);
            ok = json_set(&doc, doc.root, "target",
                          json_string(&doc, identity.target, identity.target_length, &error), &error) &&
                 json_set(&doc, doc.root, "directory_identity", json_text(&doc, guard.identity, &error),
                          &error) &&
                 json_set(&doc, doc.root, "owner_sid", json_text(&doc, identity.owner_sid, &error), &error) &&
                 json_set(&doc, doc.root, "registration_key",
                          json_text(&doc, identity.registration_key, &error), &error);
        } else if (ok) {
            ok = argc >= 4 && read_input(argv[3], &input, &n) && json_parse(input, n, &doc, &error);
            if (ok && (!wcscmp(argv[1], L"--receipt") || !wcscmp(argv[1], L"--draft") ||
                       !wcscmp(argv[1], L"--installed-registry"))) {
                ok = medieval_receipt_validate(&doc, doc.root, &identity, 1, &receipt, &error);
                if (ok && argc == 5 && !wcscmp(argv[4], L"originals"))
                    ok = medieval_receipt_verify_originals(&identity, &receipt, &error);
                if (ok && !wcscmp(argv[1], L"--draft")) {
                    JsonValue *draft = NULL;
                    ok = medieval_receipt_draft(
                        &doc, &identity, &receipt, "clean", receipt.uninstaller_sha256, "1.0.0",
                        "01234567-89ab-cdef-0123-456789abcdef", "2026-10-02T01:00:00Z",
                        json_get(receipt.json, "locked_settings"), NULL, &draft, &error);
                    if (ok) {
                        doc.root = draft;
                        ok = json_seal(&doc, draft, &error);
                    }
                } else if (ok && !wcscmp(argv[1], L"--installed-registry")) {
                    JsonValue *values = json_new(&doc, JSON_ARRAY, &error), *before = NULL, *installed = NULL;
                    PatchRegistrySnapshot snapshot;
                    ok = patch_registry_object(&doc, 0, 0, values, &before, &error) &&
                         patch_registry_validate(&identity.registry, before, &snapshot, &error) &&
                         medieval_registry_installed(&doc, &identity, &receipt, &snapshot, "GOG", &installed,
                                                     &error);
                    if (ok)
                        doc.root = installed;
                }
            } else if (ok && !wcscmp(argv[1], L"--archive")) {
                MedievalArchive archive = {0};
                ok = medieval_archive_validate(&doc, doc.root, &identity, &archive, &error);
            } else if (ok) {
                if (argc == 5 && !wcscmp(argv[4], L"native32-schema"))
                    identity.registry.os64 = 0;
                ok = medieval_journal_validate(&doc, doc.root, &identity, &journal, &error);
                if (ok && argc == 5 && wcscmp(argv[4], L"native32-schema"))
                    ok = medieval_journal_verify_copies(&identity, &journal, argv[4], &error);
            }
        }
        if (ok)
            ok = json_write_canonical(&doc, &out, &length, &error);
        if (ok)
            fwrite(out, 1, length, stdout);
        else
            printf("ERROR %s\n", error.code ? error.code : "unknown");
        free(input);
        json_document_close(&doc);
        medieval_identity_close(&identity);
        patch_guard_close(&guard);
        return ok ? 0 : 2;
    }
    if (argc == 5 && !wcscmp(argv[1], L"--legacy-enumerate")) {
        PatchGuard guard = {0};
        MedievalIdentity identity = {0};
        JsonValue *found = NULL;
        if (!isolated_registry_begin(argv[2]))
            return 93;
        ok = patch_guard_open(&guard, argv[3], &error) && medieval_identity_open(&identity, &guard, &error);
        if (!wcscmp(argv[4], L"native32"))
            identity.registry.os64 = 0;
        if (ok)
            ok = medieval_legacy_registrations(&doc, &identity, &found, &error);
        if (ok) {
            doc.root = found;
            ok = json_write_canonical(&doc, &out, &length, &error);
        }
        if (ok)
            fwrite(out, 1, length, stdout);
        else
            printf("ERROR %s\n", error.code ? error.code : "unknown");
        json_document_close(&doc);
        medieval_identity_close(&identity);
        patch_guard_close(&guard);
        isolated_registry_close();
        return ok ? 0 : 2;
    }
    if (argc == 4 && !wcscmp(argv[1], L"--oom-registry")) {
        PatchRegistry registry = {0};
        JsonDocument desired = {0};
        DWORD before = 0, after = 0;
        size_t failure, total = 0;
        if (!isolated_registry_begin(argv[2]) || !read_input(argv[3], &input, &n) ||
            !json_parse(input, n, &desired, &error))
            return 93;
        if (!patch_registry_init(&registry, &error))
            return 94;
        GetProcessHandleCount(GetCurrentProcess(), &before);
        for (failure = 0; failure < 2500; ++failure) {
            AllocatorTest allocator = {0};
            allocator.fail_at = failure;
            memset(&error, 0, sizeof(error));
            registry.allocator.user = &allocator;
            registry.allocator.allocate = test_allocate;
            registry.allocator.deallocate = test_deallocate;
            ok = patch_registry_set(&registry, "LocalMachine", "Registry32", "Task3", desired.root, &error);
            total = allocator.calls;
            if (failure && failure <= total &&
                (ok || !error.code || strcmp(error.code, "allocation_failed"))) {
                printf("FAIL registry allocation %u code %s\n", (unsigned)failure,
                       error.code ? error.code : "none");
                ++failed;
            }
            if (allocator.live) {
                printf("FAIL leaked registry allocation %u\n", (unsigned)failure);
                ++failed;
            }
            if (!failure && !ok) {
                printf("FAIL baseline registry %s\n", error.code ? error.code : "none");
                ++failed;
                break;
            }
            if (failure > total)
                break;
        }
        GetProcessHandleCount(GetCurrentProcess(), &after);
        check(before == after, "registry OOM handle cleanup");
        check(failure > total && total > 50, "all registry allocation boundaries exercised");
        printf("OOM attempts=%u RESULT passed=%d failed=%d\n", (unsigned)failure, passed, failed);
        free(input);
        json_document_close(&desired);
        isolated_registry_close();
        return failed ? 1 : 0;
    }
    if (argc >= 5 && !wcsncmp(argv[1], L"--live-", 7)) {
        PatchRegistry registry = {0};
        PatchContext context = {0};
        char *view = NULL;
        size_t vn;
        JsonValue *snapshot = NULL;
        if (!isolated_registry_begin(argv[2]))
            return 93;
        ok = patch_registry_init(&registry, &error) &&
             patch_wide_to_utf8(&context, argv[3], wcslen(argv[3]), &view, &vn, &error);
        if (!wcscmp(argv[4], L"native32")) {
            registry.os64 = 0;
            registry.delete_ex = NULL;
        } else if (!wcscmp(argv[4], L"no-delete-ex"))
            registry.delete_ex = NULL;
        if (ok && !wcscmp(argv[1], L"--live-set")) {
            ok = argc == 6 && read_input(argv[5], &input, &n) && json_parse(input, n, &doc, &error);
            if (ok)
                ok = patch_registry_set(&registry, "LocalMachine", view, "Task3", doc.root, &error);
        } else if (ok) {
            ok = patch_registry_snapshot(&registry, &doc, "LocalMachine", view, "Task3", &snapshot, &error);
            doc.root = snapshot;
            if (ok)
                ok = json_write_canonical(&doc, &out, &length, &error);
            if (ok)
                fwrite(out, 1, length, stdout);
        }
        if (!ok)
            printf("ERROR %s\n", error.code ? error.code : "unknown");
        free(input);
        json_document_close(&doc);
        patch_context_close(&context);
        isolated_registry_close();
        return ok ? 0 : 2;
    }
    if (argc == 3 && !wcscmp(argv[1], L"--key")) {
        PatchContext context = {0};
        char key[57], mutex[64];
        wchar_t *normalized = NULL;
        ok = patch_path_normalize(&context, argv[2], &normalized, &error) &&
             patch_registry_identity(&context, normalized, key, mutex, &error);
        if (ok)
            printf("%s\n%s\n", key, mutex);
        else
            printf("ERROR %s\n", error.code ? error.code : "unknown");
        patch_context_close(&context);
        return ok ? 0 : 2;
    }
    if (argc != 3 || !read_input(argv[2], &input, &n))
        return 90;
    ok = json_parse(input, n, &doc, &error);
    if (ok && !wcscmp(argv[1], L"--registry-compare")) {
        PatchRegistry registry = {0};
        PatchRegistrySnapshot a = {0}, b = {0};
        int strict = 0, expected = 0;
        ok = patch_registry_init(&registry, &error) &&
             patch_registry_validate(&registry, json_get(doc.root, "a"), &a, &error) &&
             patch_registry_validate(&registry, json_get(doc.root, "b"), &b, &error) &&
             patch_registry_equal(&registry, &a, &b, &strict, &error) &&
             patch_registry_expected_equal(&registry, &a, &b, &expected, &error) &&
             json_set(&doc, doc.root, "strict", json_bool(&doc, strict, &error), &error) &&
             json_set(&doc, doc.root, "expected", json_bool(&doc, expected, &error), &error);
    }
    if (ok && !wcscmp(argv[1], L"--registry")) {
        PatchRegistry registry = {0};
        PatchRegistrySnapshot snapshot = {0};
        ok = patch_registry_init(&registry, &error) &&
             patch_registry_validate(&registry, doc.root, &snapshot, &error);
    }
    if (ok && !wcscmp(argv[1], L"--seal"))
        ok = json_seal(&doc, doc.root, &error);
    if (ok)
        ok = json_write_canonical(&doc, &out, &length, &error);
    if (ok)
        fwrite(out, 1, length, stdout);
    else
        printf("ERROR %s\n", error.code ? error.code : "unknown");
    free(input);
    json_document_close(&doc);
    return ok ? 0 : 2;
}
