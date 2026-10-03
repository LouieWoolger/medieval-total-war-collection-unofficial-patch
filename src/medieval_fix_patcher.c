/* Native Medieval lifecycle entry point. Copyright Louie Woolger.
   GPL-3.0; see LICENSE. The production package switches to this in Task 5. */
#include "lifecycle.h"

static int medieval_version_valid(const char *s) {
    unsigned part;
    if (!s) return 0;
    for (part = 0; part < 3; ++part) {
        if (*s < '0' || *s > '9') return 0;
        while (*s >= '0' && *s <= '9') ++s;
        if (part < 2 && *s++ != '.') return 0;
    }
    if (!*s) return 1;
    if (*s != '.' && *s != '-') return 0;
    ++s;
    if (!*s) return 0;
    for (; *s; ++s)
        if (!(*s >= '0' && *s <= '9') && !(*s >= 'a' && *s <= 'z') &&
            !(*s >= 'A' && *s <= 'Z') && *s != '.' && *s != '-') return 0;
    return 1;
}
static int medieval_field(const wchar_t *s, int request) {
    static const wchar_t *const command[] = {L"--operation", L"--target", L"--payload", L"--version",
        L"--installer", L"--uninstaller", L"--log", L"--request", L"--test-fault", L"--output", L"--require-owner"};
    static const wchar_t *const aliases[] = {L"-Operation", L"-Target", L"-PayloadDirectory", L"-InstallerVersion",
        L"-InstallerPath", L"-UninstallerSource", L"-LogPath", L"-RequestPath", L"-TestFault", L"-OutputMode", L"-RequireOwner"};
    static const wchar_t *const fields[] = {L"operation", L"target", L"payload", L"version", L"installer", L"uninstaller", L"log", L"require_owner"};
    size_t i;
    for (i = 0; i < (request ? 8U : 11U); ++i)
        if (request ? !wcscmp(s, fields[i]) : (!wcscmp(s, command[i]) || !wcscmp(s, aliases[i]))) return request && i == 7 ? 10 : (int)i;
    return -1;
}
static int medieval_option_set(PatchContext *c, MedievalOptions *o, int field, const wchar_t *value, PatchError *e) {
    char *text;
    size_t length = wcslen(value), bytes;
    /* Validate even values consumed later as UTF-16 paths. */
    if (!patch_wide_to_utf8(c, value, length, &text, &bytes, e)) return 0;
    switch (field) {
    case 0: o->operation = text; break;
    case 1: o->target = value; break;
    case 2: o->payload = value; break;
    case 3: o->version = text; break;
    case 4: o->installer = value; break;
    case 5: o->uninstaller = value; break;
    case 6: o->log = value; break;
    case 7: o->request = value; break;
    case 8: o->fault = text; break;
    case 9: o->output = text; break;
    case 10: o->require_owner = text; break;
    default: return 0;
    }
    return 1;
}
static int medieval_options_parse(PatchContext *c, int argc, wchar_t **argv, MedievalOptions *o, PatchError *e) {
    unsigned seen = 0;
    int i, field;
    PatchError nested = {0};
    memset(o, 0, sizeof *o); o->output = "Json";
    for (i = 1; i < argc; ++i) {
        field = medieval_field(argv[i], 0);
        if (field < 0 || i + 1 >= argc || (seen & (1U << field))) goto invalid;
        seen |= 1U << field;
        if (!medieval_option_set(c, o, field, argv[++i], &nested)) goto invalid;
    }
    if (o->request && *o->request) {
        PatchGuard guard = {0};
        wchar_t *path = NULL, *parent = NULL, *data;
        char *bytes = NULL;
        size_t size = 0, units, p, start;
        void *memory = NULL;
        int read_ok;
        if (seen & ~((1U << 7) | (1U << 9))) goto invalid;
        read_ok = patch_path_normalize(c, o->request, &path, &nested) && patch_parent_path(c, path, &parent, &nested) &&
                  patch_guard_open_internal(&guard, parent, 1, &nested) &&
                  patch_file_read(&guard, path, 65536, c, &bytes, &size, &nested);
        patch_guard_close(&guard);
        if (!read_ok || size % 2 || !patch_alloc(c, size / 2 + 1, sizeof(wchar_t), &memory, &nested)) goto invalid;
        data = memory; units = size / 2;
        for (p = 0; p < units; ++p) {
            data[p] = (wchar_t)((unsigned char)bytes[p * 2] | (unsigned char)bytes[p * 2 + 1] << 8);
            if (!data[p]) goto invalid;
        }
        {
            char *validated;
            size_t validated_length;
            if (!patch_wide_to_utf8(c, data, units, &validated, &validated_length, &nested)) goto invalid;
        }
        p = units && data[0] == 0xfeff ? 1 : 0;
        seen = 0;
        while (p < units) {
            wchar_t *key, *value, *equal;
            start = p;
            while (p < units && data[p] != L'\n') ++p;
            if (p < units) data[p++] = 0;
            key = data + start;
            {
                size_t length = wcslen(key);
                if (length && key[length - 1] == L'\r') key[length - 1] = 0;
            }
            equal = wcschr(key, L'=');
            if (!equal || equal == key) goto invalid;
            *equal = 0; value = equal + 1;
            field = medieval_field(key, 1);
            if (field < 0 || (seen & (1U << field))) goto invalid;
            seen |= 1U << field;
            if (!medieval_option_set(c, o, field, value, &nested)) goto invalid;
        }
    }
    if ((!mtw_is(o->operation, "Inspect") && !mtw_is(o->operation, "Install") && !mtw_is(o->operation, "Verify") && !mtw_is(o->operation, "Restore")) ||
        !o->target || !*o->target || !o->payload || !*o->payload || !medieval_version_valid(o->version) ||
        (!mtw_is(o->output, "Json") && !mtw_is(o->output, "Human"))) goto invalid;
    return 1;
invalid:
    patch_error_set(e, "invalid_request", "A valid operation, target, payload and patch version are required. Request fields must be unique, recognized and valid UTF-16 without embedded NUL.", nested.win32);
    return 0;
}
int wmain(int argc, wchar_t **argv) {
    MedievalOptions options = {0};
    MedievalOutcome outcome = {0};
    PatchContext arguments = {0}, output_context = {0};
    PatchError error = {0}, formatting = {0};
    PatchDiagnostics output;
    HANDLE output_handle;
    char *text = NULL;
    size_t length = 0;
    int exit_code, emitted = 0;
    SetDllDirectoryW(L"");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (medieval_options_parse(&arguments, argc, argv, &options, &error)) medieval_run(&options, &outcome, &error);
    else medieval_failure_result(&outcome, &options, &error, "", &formatting);
    exit_code = outcome.exit_code;
    output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    patch_diagnostics_init(&output, &output_context, output_handle, NULL);
    if (output_handle && output_handle != INVALID_HANDLE_VALUE && mtw_is(options.output, "Human")) {
        const char *message;
        if (exit_code) message = outcome.detail[0] ? outcome.detail : error.message ? error.message : "The operation could not complete.";
        else if (mtw_is(options.operation, "Install")) message = "Installed and verified the Terrain Movement Fix, recovery state and uninstaller.";
        else if (mtw_is(options.operation, "Restore")) message = "Unofficial patch removal completed. Your game and personal content are kept.";
        else if (mtw_is(mtw_s(outcome.result, "mode"), "recovery-pending")) message = "Recovery metadata validated. Setup will recover the interrupted operation before continuing.";
        else message = "Supported Medieval: Total War folder. Runtime and installation checks completed.";
        emitted = patch_diagnostics_printf(&output, "%s\n", message);
        if (emitted && *mtw_s(outcome.result, "recovery_archive"))
            emitted = patch_diagnostics_printf(&output, "Preserved files: %s\n", mtw_s(outcome.result, "recovery_archive"));
        if (emitted && *mtw_s(outcome.result, "recovery_file"))
            emitted = patch_diagnostics_printf(&output, "Preserved original: %s\n", mtw_s(outcome.result, "recovery_file"));
    } else if (output_handle && output_handle != INVALID_HANDLE_VALUE && outcome.result &&
               json_write_canonical(&outcome.document, &text, &length, &formatting)) {
        emitted = patch_diagnostics_emit(&output, text, length) && patch_diagnostics_emit(&output, "\n", 1);
    }
    if (!emitted) {
        PatchDiagnostics fallback;
        const char *message = "Diagnostic output failed. Preserve any recovery state and inspect the persistent log.\n";
        exit_code = outcome.removal_completed ? 4 : mtw_is(outcome.restoration, "verified") ? 3 : 2;
        patch_diagnostics_init(&fallback, &output_context, GetStdHandle(STD_ERROR_HANDLE), NULL);
        patch_diagnostics_emit(&fallback, message, strlen(message));
    }
    medieval_outcome_close(&outcome);
    patch_context_close(&output_context);
    patch_context_close(&arguments);
    return exit_code;
}
