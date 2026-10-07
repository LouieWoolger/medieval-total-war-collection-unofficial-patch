#ifndef MTW_LIFECYCLE_H
#define MTW_LIFECYCLE_H
/* One C99 coordinator for Medieval install, repair, migration and removal.
   Runtime bytes, identities and durable wire records stay Medieval-specific. */
#include "install_transaction.h"

static inline int mtw_config_check(MedievalEngine *m, PatchGuard *g, const wchar_t *path, PatchError *e) {
    static const char *const names[] = {"Resampling", "ScalingMode", "FullscreenAttributes", "FastVideoMemoryAccess", "FPSLimit"};
    static const char *const values[] = {"lanczos-3", "stretched_ar", "fake", "true", "0"};
    PatchContext c = {0};
    char *text = NULL;
    size_t length, i;
    int ok = 0;
    if (!path || !patch_file_read(g, path, 2097152, &c, &text, &length, e)) goto done;
    for (i = 0; i < 5; ++i) {
        char *line = text, *end = text + length;
        int found = 0;
        while (line < end) {
            char *next = memchr(line, '\n', (size_t)(end - line)), *p = line, *q;
            size_t k = strlen(names[i]), v = strlen(values[i]);
            q = next ? next : end;
            while (p < q && (*p == ' ' || *p == '\t')) ++p;
            if ((size_t)(q - p) >= k && !_strnicmp(p, names[i], k)) {
                p += k;
                while (p < q && (*p == ' ' || *p == '\t')) ++p;
                if (p < q && *p++ == '=') {
                    while (p < q && (*p == ' ' || *p == '\t')) ++p;
                    if ((size_t)(q - p) >= v && !_strnicmp(p, values[i], v)) {
                        p += v;
                        while (p < q && (*p == ' ' || *p == '\t')) ++p;
                        if (p < q && *p == '\r') ++p;
                        if (p == q || *p == ';') found = 1;
                    }
                }
            }
            line = next ? next + 1 : end;
        }
        if (!found) {
            mtw_fail(m, e, "verification_failed", "Required dgVoodoo setting is missing or changed: %s = %s", names[i], values[i]);
            goto done;
        }
    }
    ok = 1;
done:
    patch_context_close(&c);
    return ok;
}
static inline int mtw_verify_runtime(MedievalEngine *m, PatchError *e) {
    size_t i;
    for (i = 0; i < MEDIEVAL_FILE_COUNT; ++i) {
        PatchFileRecord actual;
        MTW_TRY(mtw_relative_record(m, medieval_payload_names[i], &actual, e));
        if (!patch_record_equal(&actual, &m->payload_records[i]))
            return mtw_fail(m, e, "verification_failed", "Installed runtime verification failed for %s.", medieval_payload_names[i]);
    }
    return !m->options.terrain_fix ||
           mtw_config_check(m, &m->guard, mtw_live(m, "dgVoodoo.conf", e), e);
}
static inline int mtw_preinstall_mode(MedievalEngine *m, const char **mode, PatchError *e) {
    PatchFileRecord record;
    size_t i;
    *mode = "clean";
    MTW_TRY(mtw_relative_record(m, "D3D8.dll", &record, e));
    if (record.exists) return mtw_fail(m, e, "d3d8_conflict", "D3D8.dll is present. Remove or isolate that wrapper before installing this patch.");
    MTW_TRY(mtw_relative_record(m, "D3D9.dll", &record, e));
    if (record.exists) {
        *mode = medieval_known_d3d9_mode(record.sha256);
        if (!**mode) return mtw_fail(m, e, "wrapper_conflict", "D3D9.dll is not a supported stock, dust-only, R6F160, R185, or R186 build (SHA-256 %s). No files were changed.", record.sha256);
    }
    for (i = 0; i < 3; ++i) {
        MTW_TRY(mtw_relative_record(m, medieval_payload_names[i], &record, e));
        if (record.exists && strcmp(record.sha256, m->payload_records[i].sha256))
            return mtw_fail(m, e, "wrapper_conflict", "An unrecognized wrapper was preserved: %s. Remove or isolate it before installing this patch.", medieval_payload_names[i]);
    }
    if (!m->options.terrain_fix) {
        for (i = 0; i < MEDIEVAL_RUNTIME_COUNT; ++i) {
            MTW_TRY(mtw_relative_record(m, medieval_payload_names[i], &record, e));
            if (record.exists)
                return mtw_fail(m, e, "wrapper_conflict",
                                "Scrolling-only setup found a pre-existing wrapper. Preserve or isolate it before installing; no runtime file was changed.");
        }
    }
    return 1;
}
static inline int mtw_originals(MedievalEngine *m, const MedievalReceipt *r, PatchError *e) {
    size_t i;
    for (i = 0; i < medieval_receipt_file_count(r); ++i) {
        PatchFileRecord current;
        if (!r->files[i].original.exists) continue;
        MTW_TRY(mtw_record(m, mtw_path(m, m->state, r->files[i].snapshot_relative, e), &current, e));
        if (!patch_record_equal(&current, &r->files[i].original))
            return mtw_fail(m, e, "receipt_invalid", "The private original for %s is missing or damaged. Keep the patch state and backups; nothing was invented or overwritten.", medieval_payload_names[i]);
    }
    return 1;
}
static inline int mtw_read_receipt(MedievalEngine *m, MedievalReceipt *receipt, int *present, PatchError *e) {
    JsonValue *raw;
    PatchFileRecord actual, after;
    wchar_t *path;
    *present = 0;
    if (!mtw_directory_exists(m->state)) return 1;
    MTW_TRY(patch_guard_directory(&m->guard, m->state, 0, e));
    path = mtw_live(m, MTW_RECEIPT_RELATIVE, e);
    MTW_TRY(mtw_record(m, path, &actual, e));
    if (!actual.exists) return mtw_fail(m, e, "receipt_invalid", "The patch state directory exists without usable installation metadata. Preserve it and its originals for recovery.");
    MTW_TRY(mtw_read_json(m, &m->guard, path, &raw, &m->receipt_record, e) &&
            medieval_receipt_validate(&m->document, raw, &m->identity, 1, receipt, e));
    m->have_receipt_record = 1;
    if (!receipt->legacy && mtw_is(receipt->status, "restored")) {
        PatchRegistrySnapshot registry;
        const JsonValue *v;
        int owned;
        MTW_TRY(mtw_relative_record(m, MEDIEVAL_UNINSTALL_NAME, &after, e));
        if (after.exists) return mtw_fail(m, e, "recovery_required", "Restored metadata still has a removal program. Recover the interrupted transaction before reinstalling.");
        MTW_TRY(mtw_registry_snapshot(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, e));
        for (v = registry.values->child; v; v = v->next) {
            MTW_TRY(patch_registry_owned_name(&m->identity.registry, mtw_s(v, "name"), &owned, e));
            if (owned) return mtw_fail(m, e, "recovery_required", "Restored metadata still has an active Windows patch entry. Preserve the entry and recovery files for repair.");
        }
        return 1;
    }
    MTW_TRY(mtw_originals(m, receipt, e));
    *present = 1;
    return 1;
}
static inline int mtw_installed_registry(MedievalEngine *m, const MedievalReceipt *r,
                                      const PatchRegistrySnapshot *before, JsonValue **out, PatchError *e) {
    const char *edition = "Game copy";
    PatchFileRecord file;
    MTW_TRY(mtw_relative_record(m, "goggame-1397939414.info", &file, e));
    if (file.exists) edition = "GOG";
    else {
        MTW_TRY(mtw_relative_record(m, "steam_api.dll", &file, e));
        if (!file.exists) MTW_TRY(mtw_relative_record(m, "steam_appid.txt", &file, e));
        if (file.exists) edition = "Steam";
    }
    return medieval_registry_installed(&m->document, &m->identity, r, before, edition, out, e);
}
static inline int mtw_verify_installed(MedievalEngine *m, const MedievalReceipt *r, PatchError *e) {
    PatchFileRecord file;
    PatchRegistrySnapshot registry, wanted;
    JsonValue *raw;
    int equal;
    MTW_TRY(mtw_relative_record(m, MEDIEVAL_UNINSTALL_NAME, &file, e));
    if (!file.exists || strcmp(file.sha256, r->uninstaller_sha256))
        return mtw_fail(m, e, "verification_failed", "The installed patch uninstaller is missing or changed. Run setup to repair this installation.");
    MTW_TRY(mtw_registry_snapshot(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, e));
    if (!registry.exists) return mtw_fail(m, e, "verification_failed", "The patch Windows entry is missing. Run setup to restore it.");
    MTW_TRY(medieval_registry_installed_identity(&m->identity, &registry, r->installation_id, e) &&
            mtw_installed_registry(m, r, &registry, &raw, e) &&
            patch_registry_validate(&m->identity.registry, raw, &wanted, e) &&
            mtw_registry_compare(m, &registry, &wanted, &equal, e));
    if (!equal) return mtw_fail(m, e, "verification_failed", "The Windows patch entry is incomplete. Run setup to repair its display and removal information.");
    return 1;
}
static inline int mtw_writable(MedievalEngine *m, uint64_t bytes, PatchError *e) {
    char id[37], relative[80];
    wchar_t *path;
    PatchFileRecord r;
    PatchError local = {0};
    ULARGE_INTEGER available;
    MTW_TRY(mtw_guid(id, e));
    snprintf(relative, sizeof relative, ".umtwp-write-%s.tmp", id);
    path = mtw_path(m, m->guard.canonical, relative, e);
    MTW_TRY(path);
    if (!patch_file_write_new(&m->guard, path, "x", 1, &local) || !mtw_record(m, path, &r, &local) ||
        !patch_file_remove(&m->guard, path, &r, &local))
        return mtw_fail(m, e, "target_not_writable", "The selected game folder is not writable. Grant your account write access or choose a writable game installation.");
    if (!bytes) return 1;
    if (!GetDiskFreeSpaceExW(m->guard.canonical, &available, NULL, NULL)) {
        patch_error_set(e, "insufficient_space", "Cannot determine available disk space.", GetLastError());
        return 0;
    }
    if (bytes > (UINT64_MAX - 5 * 1024 * 1024) / 3 || available.QuadPart < bytes * 3 + 5 * 1024 * 1024)
        return mtw_fail(m, e, "insufficient_space", "There is insufficient free space for a transactional installation.");
    return 1;
}
static inline int mtw_archive_source(MedievalEngine *m, MedievalArchiveSource *sources, size_t *count,
                                    const char *name, const char *relative, PatchError *e) {
    MedievalArchiveSource *s;
    if (*count >= 48 || strlen(name) >= sizeof sources[0].name || strlen(relative) >= sizeof sources[0].relative)
        return mtw_fail(m, e, "receipt_invalid", "Too many or oversized recovery archive members.");
    s = &sources[(*count)++];
    strcpy(s->name, name); strcpy(s->relative, relative);
    s->source = mtw_live(m, relative, e);
    return s->source != NULL;
}
static inline int mtw_archive(MedievalEngine *m, const MedievalArchiveSource *sources, size_t count,
                             const char *reason, PatchError *e) {
    JsonDocument *d = &m->document;
    JsonValue *manifest, *files, *raw;
    MedievalArchive archive;
    PatchFileRecord rec, written, empty = {0};
    char id[37], relative[80], utc[32];
    wchar_t *directory, *path;
    size_t i, archive_length;
    int equal;
    static const char readme[] = "Unofficial Medieval Patch recovery archive\r\n\r\nThese verified copies were preserved before active files were replaced or removed.\r\nThey may include personal modifications or an older patch, and are not a claim of clean game originals.\r\nThe archive is not an active patch. Keep it until you have checked the game; it may be copied elsewhere.\r\nSee manifest.json for original locations, sizes and SHA-256 hashes.\r\n";
    if (!count) return 1;
    MTW_TRY(mtw_guid(id, e));
    snprintf(relative, sizeof relative, ".medieval-recovery-%.12s", id);
    directory = mtw_path(m, m->guard.canonical, relative, e);
    MTW_TRY(directory && patch_guard_directory(&m->guard, directory, 1, e));
    files = json_new(d, JSON_ARRAY, e);
    MTW_TRY(files);
    for (i = 0; i < count; ++i) {
        JsonValue *entry;
        size_t k;
        if (strpbrk(sources[i].name, "\\/:") || !strcmp(sources[i].name, "README.txt") || !strcmp(sources[i].name, "manifest.json"))
            return mtw_fail(m, e, "unsafe_path", "Invalid archive member name.");
        for (k = 0; k < i; ++k) if (!strcmp(sources[k].name, sources[i].name))
            return mtw_fail(m, e, "unsafe_path", "Duplicate archive member name.");
        MTW_TRY(mtw_record(m, sources[i].source, &rec, e) && mtw_observed(m, sources[i].source, &rec, e));
        if (!rec.exists) continue;
        path = mtw_path(m, directory, sources[i].name, e);
        MTW_TRY(path && mtw_fault(m, "archive-write", e) && patch_file_copy_new(&m->guard, sources[i].source, path, &rec, e));
        entry = json_new(d, JSON_OBJECT, e);
        MTW_TRY(entry && mtw_text(d, entry, "file", sources[i].name, e) &&
                mtw_text(d, entry, "source_relative", sources[i].relative, e) &&
                mtw_number(d, entry, "length", (int64_t)rec.length, e) && mtw_text(d, entry, "sha256", rec.sha256, e) &&
                json_append(files, entry, e));
    }
    mtw_utc(utc);
    manifest = json_new(d, JSON_OBJECT, e);
    MTW_TRY(manifest && mtw_text(d, manifest, "schema", "unofficial-medieval-patch-recovery-v1", e) &&
            mtw_text(d, manifest, "reason", reason, e) && mtw_text(d, manifest, "created_utc", utc, e) &&
            mtw_text(d, manifest, "target", m->identity.target, e) && json_set(d, manifest, "files", files, e) &&
            json_seal(d, manifest, e) && medieval_archive_validate(d, manifest, &m->identity, &archive, e));
    path = mtw_path(m, directory, "manifest.json", e);
    MTW_TRY(mtw_checked_write(m, manifest, path, &empty, e) && mtw_read_json(m, &m->guard, path, &raw, NULL, e) &&
            mtw_json_equal(m, raw, manifest, &equal, e));
    if (!equal) return mtw_fail(m, e, "verification_failed", "Recovery archive manifest verification failed; active files were retained.");
    path = mtw_path(m, directory, "README.txt", e);
    MTW_TRY(path && patch_file_write_new(&m->guard, path, readme, sizeof readme - 1, e));
    for (i = 0; i < archive.count; ++i) {
        MTW_TRY(mtw_record(m, mtw_path(m, directory, archive.files[i].file, e), &written, e));
        if (!patch_record_equal(&written, &archive.files[i].record))
            return mtw_fail(m, e, "verification_failed", "Recovery archive verification failed; active files were retained.");
    }
    MTW_TRY(patch_wide_to_utf8(&m->memory, directory, wcslen(directory), &m->recovery_archive, &archive_length, e));
    return mtw_log(m, m->recovery_archive, e);
}
static inline int mtw_version_string(MedievalEngine *m, const wchar_t *path, const wchar_t *name,
                                    char **out, PatchError *e) {
    typedef struct { WORD language, codepage; } Translation;
    DWORD unused, size = GetFileVersionInfoSizeW(path, &unused);
    void *bytes = NULL, *table = NULL, *value = NULL;
    UINT length = 0, n = 0;
    size_t i, output_length;
    wchar_t query[128];
    *out = NULL;
    if (!size || size > 2097152) return 1;
    MTW_TRY(patch_alloc(&m->memory, size, 1, &bytes, e));
    if (!GetFileVersionInfoW(path, 0, size, bytes) ||
        !VerQueryValueW(bytes, L"\\VarFileInfo\\Translation", &table, &length)) return 1;
    for (i = 0; i < length / sizeof(Translation); ++i) {
        Translation *languages = table;
        _snwprintf(query, 128, L"\\StringFileInfo\\%04x%04x\\%ls", languages[i].language, languages[i].codepage, name);
        if (VerQueryValueW(bytes, query, &value, &n) && n)
            return patch_wide_to_utf8(&m->memory, value, n - 1, out, &output_length, e);
    }
    return 1;
}

static inline int mtw_install(MedievalEngine *m, const MedievalReceipt *previous, JsonValue **result, PatchError *e) {
    JsonDocument *d = &m->document;
    JsonValue *receipt, *files, *legacy = NULL, *registration, *raw;
    MedievalReceipt typed;
    MedievalTransaction t = {0};
    PatchRegistrySnapshot registry;
    PatchFileRecord remover, current_remover, installer = {0}, empty = {0};
    MedievalArchiveSource sources[48];
    size_t count = 0, i, k;
    wchar_t *original_sources[MEDIEVAL_FILE_COUNT] = {0}, *old_executable, *receipt_source, *payload_source;
    int retire_originals[MEDIEVAL_FILE_COUNT] = {0}, retire_legacy = 0, repair = previous != NULL;
    const char *mode;
    char id[37], utc[32], relative[192], name[160];
    static const size_t original_order[MEDIEVAL_FILE_COUNT] = {5, 4, 2, 1, 3, 0};
    MTW_TRY(mtw_observe(m, e));
    if (repair && !m->options.terrain_fix)
        for (i = 0; i < MEDIEVAL_RUNTIME_COUNT; ++i)
            m->payload_records[i] = previous->files[i].original;
    if (!m->options.uninstaller || !*m->options.uninstaller)
        return mtw_fail(m, e, "invalid_payload", "The installer did not supply its generated removal program. No runtime files were changed.");
    MTW_TRY(mtw_external_record(m->options.uninstaller, &remover, e));
    if (!remover.exists) return mtw_fail(m, e, "invalid_payload", "The installer did not supply its generated removal program. No runtime files were changed.");
    if (repair) mode = previous->preinstall_mode;
    else MTW_TRY(mtw_preinstall_mode(m, &mode, e));
    MTW_TRY(mtw_relative_record(m, MEDIEVAL_UNINSTALL_NAME, &current_remover, e));
    if (current_remover.exists && (!repair || previous->legacy || strcmp(current_remover.sha256, previous->uninstaller_sha256)))
        return mtw_fail(m, e, "uninstaller_name_in_use", "The requested uninstaller filename is occupied by an unrecognized or changed file. Preserve it outside the game folder before retrying setup.");
    if (m->options.installer && *m->options.installer) MTW_TRY(mtw_external_record(m->options.installer, &installer, e));
    MTW_TRY(mtw_guid(id, e)); mtw_utc(utc);
    MTW_TRY(medieval_receipt_draft(d, &m->identity, previous, mode, remover.sha256, m->options.version, id, utc,
                                 m->options.terrain_fix ? json_get(m->payload, "locked_settings") : NULL,
                                 installer.exists ? installer.sha256 : NULL, &receipt, e));
    MTW_TRY(mtw_boolean(d, receipt, "terrain_fix_enabled", m->options.terrain_fix, e) &&
            mtw_boolean(d, receipt, "scroll_fix_enabled", m->options.scroll_fix, e) &&
            mtw_boolean(d, receipt, "sprite_fix_enabled", m->options.sprite_fix, e) &&
            mtw_text(d, receipt, "sprite_delivery", "direct-exe", e));
    MTW_TRY(mtw_registry_snapshot(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, e) &&
            medieval_registry_ownership(&m->identity, &registry, mtw_s(receipt, "installation_id"), e));
    /* Check every registration this upgrade owns before archives, receipts,
       uninstaller retirement or runtime publication can change the target. */
    MTW_TRY(patch_registry_writable(&m->identity.registry, "CurrentUser", "Registry32", m->identity.registration_key, e));
    if (repair && previous->legacy) {
        MTW_TRY(medieval_legacy_registrations(d, &m->identity, &legacy, e));
        for (raw = legacy->child; raw; raw = raw->next)
            MTW_TRY(patch_registry_writable(&m->identity.registry, mtw_s(raw, "hive"), mtw_s(raw, "view"), mtw_s(raw, "name"), e));
    }
    old_executable = mtw_live(m, MEDIEVAL_STATE_NAME "/Uninstall.exe", e);
    MTW_TRY(old_executable);
    if (repair && previous->legacy) {
        PatchFileRecord old;
        MTW_TRY(mtw_archive_source(m, sources, &count, "legacy-install-manifest.json", MTW_RECEIPT_RELATIVE, e) &&
                mtw_record(m, old_executable, &old, e));
        if (old.exists) {
            char *product, *company;
            MTW_TRY(mtw_version_string(m, old_executable, L"ProductName", &product, e) &&
                    mtw_version_string(m, old_executable, L"CompanyName", &company, e));
            if ((!mtw_is(product, MEDIEVAL_PRODUCT_NAME) && !mtw_is(product, "Unofficial Medieval: Total War Patch")) || !mtw_is(company, "Louie Woolger"))
                return mtw_fail(m, e, "legacy_uninstaller_conflict", "The old metadata-directory Uninstall.exe has an unrecognized identity. Preserve it outside the game folder before migrating.");
            MTW_TRY(mtw_archive_source(m, sources, &count, "legacy-uninstaller.exe", MEDIEVAL_STATE_NAME "/Uninstall.exe", e));
            retire_legacy = 1;
        }
    }
    files = json_get(receipt, "files");
    for (i = 0; i < 5; ++i) {
        PatchFileRecord current, sidecar;
        JsonValue *rec;
        int preserve, sidecar_created;
        const char *baseline;
        wchar_t *path = mtw_live(m, medieval_payload_names[i], e);
        MTW_TRY(mtw_record(m, path, &current, e));
        if (repair) {
            const MedievalFileState *prior = &previous->files[i];
            rec = json_get(files, medieval_payload_names[i]);
            if (current.exists && strcmp(current.sha256, prior->installed.sha256) &&
                (!prior->original.exists || strcmp(current.sha256, prior->original.sha256)))
                MTW_TRY(mtw_archive_source(m, sources, &count, medieval_payload_names[i], medieval_payload_names[i], e));
            if (previous->legacy && prior->original.exists) {
                snprintf(name, sizeof name, "legacy-original-%s", medieval_payload_names[i]);
                snprintf(relative, sizeof relative, MEDIEVAL_STATE_NAME "/originals/%s", medieval_payload_names[i]);
                MTW_TRY(mtw_archive_source(m, sources, &count, name, relative, e));
                if (medieval_old_patch(mode) && medieval_known_old_runtime(medieval_payload_names[i], prior->original.sha256, m->payload_records[i].sha256)) {
                    MTW_TRY(mtw_text(d, rec, "legacy_original_sha256", prior->original.sha256, e) &&
                            mtw_boolean(d, rec, "existed", 0, e) &&
                            json_set(d, rec, "original_sha256", json_new(d, JSON_NULL, e), e) &&
                            json_set(d, rec, "original_length", json_new(d, JSON_NULL, e), e) &&
                            json_set(d, rec, "snapshot_relative", json_new(d, JSON_NULL, e), e) &&
                            mtw_boolean(d, rec, "sidecar_created", 0, e) && mtw_text(d, rec, "baseline_kind", "older-patch-archived", e) &&
                            mtw_text(d, receipt, "restoration_kind", "older-patch-removed-with-archive", e));
                    retire_originals[i] = 1;
                }
            }
        } else {
            MedievalBaselineDecision decision = medieval_baseline_decision(medieval_payload_names[i], &current, mode, m->payload_records[i].sha256);
            preserve = decision.preserve_current; baseline = decision.baseline_kind;
            if (decision.archive_current) MTW_TRY(mtw_archive_source(m, sources, &count, medieval_payload_names[i], medieval_payload_names[i], e));
            if (!strcmp(baseline, "older-patch-archived")) MTW_TRY(mtw_text(d, receipt, "restoration_kind", "older-patch-removed-with-archive", e));
            rec = json_new(d, JSON_OBJECT, e);
            snprintf(relative, sizeof relative, "originals/%s", medieval_payload_names[i]);
            snprintf(name, sizeof name, "%s.unofficial-patch.bak", medieval_payload_names[i]);
            MTW_TRY(rec && mtw_boolean(d, rec, "existed", preserve, e) &&
                    json_set(d, rec, "original_sha256", preserve ? json_text(d, current.sha256, e) : json_new(d, JSON_NULL, e), e) &&
                    json_set(d, rec, "original_length", preserve ? json_number(d, (int64_t)current.length, e) : json_new(d, JSON_NULL, e), e) &&
                    json_set(d, rec, "snapshot_relative", preserve ? json_text(d, relative, e) : json_new(d, JSON_NULL, e), e) &&
                    json_set(d, rec, "installed_sha256", m->payload_records[i].exists ?
                             json_text(d, m->payload_records[i].sha256, e) : json_new(d, JSON_NULL, e), e) &&
                    mtw_number(d, rec, "installed_length", (int64_t)m->payload_records[i].length, e) &&
                    json_set(d, rec, "sidecar_relative", preserve ? json_text(d, name, e) : json_new(d, JSON_NULL, e), e) &&
                    mtw_boolean(d, rec, "sidecar_created", 0, e) && mtw_text(d, rec, "baseline_kind", baseline, e));
            MTW_TRY(mtw_relative_record(m, name, &sidecar, e));
            sidecar_created = preserve && strcmp(current.sha256, m->payload_records[i].sha256) && !sidecar.exists;
            MTW_TRY(mtw_boolean(d, rec, "sidecar_created", sidecar_created, e) && json_set(d, files, medieval_payload_names[i], rec, e));
            if (preserve) {
                PatchFileRecord reserved;
                original_sources[i] = path;
                snprintf(relative, sizeof relative, MEDIEVAL_STATE_NAME "/originals/%s", medieval_payload_names[i]);
                MTW_TRY(mtw_relative_record(m, relative, &reserved, e));
                if (reserved.exists) {
                    snprintf(name, sizeof name, "previous-state-%s", medieval_payload_names[i]);
                    MTW_TRY(mtw_archive_source(m, sources, &count, name, relative, e));
                }
            }
        }
        MTW_TRY(json_set(d, rec, "installed_sha256", m->payload_records[i].exists ?
                         json_text(d, m->payload_records[i].sha256, e) : json_new(d, JSON_NULL, e), e) &&
                mtw_number(d, rec, "installed_length", (int64_t)m->payload_records[i].length, e));
    }
    {
        PatchFileRecord current_exe, reserved;
        JsonValue *exe_rec = json_get(files, "Medieval_TW.exe");
        wchar_t *exe_path = mtw_live(m, "Medieval_TW.exe", e);
        MTW_TRY(exe_path && mtw_record(m, exe_path, &current_exe, e));
        if (!repair || previous->legacy || previous->old_v2) {
            if (!current_exe.exists || strcmp(current_exe.sha256, MEDIEVAL_EXECUTABLE_HASH))
                return mtw_fail(m, e, "unsupported_executable",
                                "A direct-fix EXE without a matching receipt cannot be adopted as an original.");
            exe_rec = json_new(d, JSON_OBJECT, e);
            MTW_TRY(exe_rec && mtw_boolean(d, exe_rec, "existed", 1, e) &&
                    mtw_text(d, exe_rec, "original_sha256", MEDIEVAL_EXECUTABLE_HASH, e) &&
                    mtw_number(d, exe_rec, "original_length", MTW_SCROLL_STOCK_SIZE, e) &&
                    mtw_text(d, exe_rec, "snapshot_relative", "originals/Medieval_TW.exe", e) &&
                    json_set(d, exe_rec, "sidecar_relative", json_new(d, JSON_NULL, e), e) &&
                    mtw_boolean(d, exe_rec, "sidecar_created", 0, e) &&
                    mtw_text(d, exe_rec, "baseline_kind", "verified-stock-executable", e) &&
                    json_set(d, files, "Medieval_TW.exe", exe_rec, e));
            original_sources[5] = exe_path;
            MTW_TRY(mtw_relative_record(m, MEDIEVAL_STATE_NAME "/originals/Medieval_TW.exe", &reserved, e));
            if (reserved.exists)
                MTW_TRY(mtw_archive_source(m, sources, &count, "previous-state-Medieval_TW.exe",
                                           MEDIEVAL_STATE_NAME "/originals/Medieval_TW.exe", e));
        } else {
            const MedievalFileState *prior_exe = &previous->files[5];
            if (!exe_rec || !current_exe.exists ||
                (strcmp(current_exe.sha256, prior_exe->installed.sha256) &&
                 strcmp(current_exe.sha256, prior_exe->original.sha256)))
                return mtw_fail(m, e, "unsupported_executable",
                                "The managed EXE changed outside the known stock/direct patch states.");
        }
        MTW_TRY(mtw_text(d, exe_rec, "installed_sha256", m->payload_records[5].sha256, e) &&
                mtw_number(d, exe_rec, "installed_length", (int64_t)m->payload_records[5].length, e));
    }
    MTW_TRY(mtw_archive(m, sources, count, "Installation migration or changed managed files", e) && mtw_fault(m, "after-preservation", e));
    mtw_utc(utc);
    MTW_TRY(mtw_text(d, receipt, "completed_utc", utc, e) && mtw_text(d, receipt, "recovery_archive", m->recovery_archive ? m->recovery_archive : "", e) &&
            json_seal(d, receipt, e) && medieval_receipt_validate(d, receipt, &m->identity, 0, &typed, e) &&
            mtw_transaction_prepare(m, &t, "install", &typed, e));
#define MTW_INSTALL(x) do { if (!(x)) goto failed; } while (0)
    for (k = 0; k < MEDIEVAL_FILE_COUNT; ++k) {
        i = original_order[k];
        if (!original_sources[i]) continue;
        snprintf(relative, sizeof relative, MEDIEVAL_STATE_NAME "/originals/%s", medieval_payload_names[i]);
        MTW_INSTALL(mtw_transaction_add_file(m, &t, relative, original_sources[i], e));
    }
    for (i = 0; i < 5; ++i) {
        if (!repair && typed.files[i].sidecar_created) {
            snprintf(relative, sizeof relative, "%s.unofficial-patch.bak", medieval_payload_names[i]);
            MTW_INSTALL(mtw_transaction_add_file(m, &t, relative, original_sources[i], e));
        }
        payload_source = m->options.terrain_fix ?
            mtw_path(m, m->payload_guard.canonical, medieval_payload_names[i], e) :
            (typed.files[i].original.exists ?
             mtw_path(m, m->state, typed.files[i].snapshot_relative, e) : NULL);
        MTW_INSTALL((!m->options.terrain_fix || payload_source) &&
                    mtw_transaction_add_file(m, &t, medieval_payload_names[i], payload_source, e));
        raw = json_get(t.json, "actions")->last;
        {
            PatchFileRecord staged;
            MTW_INSTALL(medieval_file_record(json_get(raw, "after"), &staged, e));
            if (!patch_record_equal(&staged, &typed.files[i].installed)) {
                mtw_fail(m, e, "invalid_payload", "The embedded runtime changed during staging."); goto failed;
            }
        }
    }
    {
        char *before_bytes = NULL;
        unsigned char *next_bytes = NULL;
        size_t before_length = 0, next_length = 0;
        PatchFileRecord staged;
        wchar_t *live_exe = mtw_live(m, "Medieval_TW.exe", e);
        wchar_t *next_exe = mtw_path(m, t.directory, "next-exe.bin", e);
        MTW_INSTALL(live_exe && next_exe &&
                    patch_file_read(&m->guard, live_exe, MTW_SCROLL_SPRITE_SIZE,
                                    &m->memory, &before_bytes, &before_length, e) &&
                    mtw_direct_exe_transform(&m->memory, (const unsigned char *)before_bytes,
                                              before_length, m->options.scroll_fix,
                                              m->options.sprite_fix, &next_bytes, &next_length, e) &&
                    mtw_fault(m, "before-stage-exe", e) &&
                    patch_file_write_new(&m->guard, next_exe, next_bytes, next_length, e) &&
                    mtw_transaction_add_file(m, &t, "Medieval_TW.exe", next_exe, e));
        raw = json_get(json_get(t.json, "actions")->last, "after");
        MTW_INSTALL(medieval_file_record(raw, &staged, e) &&
                    patch_record_equal(&staged, &typed.files[5].installed));
        MTW_INSTALL(mtw_record(m, next_exe, &staged, e) &&
                    patch_file_remove(&m->guard, next_exe, &staged, e));
        patch_context_free(&m->memory, before_bytes);
        patch_context_free(&m->memory, next_bytes);
    }
    MTW_INSTALL(mtw_fault(m, "before-stage-uninstaller", e) && mtw_transaction_add_file(m, &t, MEDIEVAL_UNINSTALL_NAME, m->options.uninstaller, e));
    raw = json_get(json_get(t.json, "actions")->last, "after");
    if (strcmp(mtw_s(raw, "sha256"), typed.uninstaller_sha256)) {
        mtw_fail(m, e, "invalid_payload", "The generated uninstaller changed after validation. Active files were retained."); goto failed;
    }
    receipt_source = mtw_path(m, t.directory, "next-receipt.json", e);
    MTW_INSTALL(mtw_checked_write(m, receipt, receipt_source, &empty, e) &&
                mtw_transaction_add_file(m, &t, MTW_RECEIPT_RELATIVE, receipt_source, e));
    {
        PatchFileRecord staged;
        MTW_INSTALL(mtw_record(m, receipt_source, &staged, e) && patch_file_remove(&m->guard, receipt_source, &staged, e));
    }
    MTW_INSTALL(mtw_installed_registry(m, &typed, &registry, &registration, e) &&
                mtw_transaction_add_registry(m, &t, "CurrentUser", "Registry32", m->identity.registration_key, registry.json, registration, e));
    if (retire_legacy) MTW_INSTALL(mtw_transaction_add_file(m, &t, MEDIEVAL_STATE_NAME "/Uninstall.exe", NULL, e));
    for (i = 0; i < 5; ++i) if (retire_originals[i]) {
        snprintf(relative, sizeof relative, MEDIEVAL_STATE_NAME "/originals/%s", medieval_payload_names[i]);
        MTW_INSTALL(mtw_transaction_add_file(m, &t, relative, NULL, e));
    }
    if (legacy) for (raw = legacy->child; raw; raw = raw->next)
        MTW_INSTALL(mtw_transaction_add_registry(m, &t, mtw_s(raw, "hive"), mtw_s(raw, "view"), mtw_s(raw, "name"), json_get(raw, "before"), json_get(raw, "after"), e));
    MTW_INSTALL(mtw_transaction_publish(m, &t, e) && mtw_transaction_commit(m, &t, e) && mtw_verify_runtime(m, e) &&
                mtw_verify_installed(m, &typed, e) && mtw_transaction_final(m, &t, e) && mtw_transaction_clear(m, &t, e));
    *result = json_new(d, JSON_OBJECT, e);
    MTW_TRY(*result && mtw_text(d, *result, "status", "ok", e) && mtw_text(d, *result, "action", repair ? "repair" : "install", e) &&
            mtw_text(d, *result, "preinstall_mode", mode, e) && mtw_text(d, *result, "installation_id", typed.installation_id, e) &&
            json_set(d, *result, "repair_count", json_clone(d, json_get(receipt, "repair_count"), e), e) &&
            mtw_text(d, *result, "target", m->identity.target, e) && mtw_text(d, *result, "registration_key", m->identity.registration_key, e) &&
            mtw_text(d, *result, "restoration_kind", mtw_s(receipt, "restoration_kind"), e));
    return 1;
failed:
    return mtw_transaction_failure(m, &t, e);
#undef MTW_INSTALL
}

static inline int mtw_retry_route(MedievalEngine *m, MedievalTransaction *t, size_t uninstall_index, PatchError *e) {
    const MedievalAction *a = &t->typed.actions[uninstall_index];
    const MedievalReceipt *r = &t->typed.receipt;
    PatchFileRecord current, after;
    PatchRegistrySnapshot registry;
    JsonValue *installed;
    wchar_t *path = mtw_live(m, MEDIEVAL_UNINSTALL_NAME, e);
    if (strcmp(a->before_file.sha256, r->uninstaller_sha256))
        return mtw_fail(m, e, "receipt_invalid", "The retained uninstaller does not match the trusted receipt.");
    MTW_TRY(mtw_record(m, path, &current, e));
    if (!patch_record_equal(&current, &a->before_file)) {
        if (current.exists) {
            MedievalArchiveSource source[1];
            size_t count = 0;
            MTW_TRY(mtw_archive_source(m, source, &count, "conflicting-uninstaller.exe", MEDIEVAL_UNINSTALL_NAME, e) &&
                    mtw_archive(m, source, count, "Uninstaller changed during final cleanup", e) &&
                    mtw_record(m, path, &after, e));
            if (!patch_record_matches(&after, &current))
                return mtw_fail(m, e, "concurrent_change", "The uninstaller changed again after archiving; it was preserved.");
            MTW_TRY(patch_file_remove(&m->guard, path, &current, e));
        }
        MTW_TRY(mtw_transaction_file(m, t, uninstall_index, 0, e));
    }
    MTW_TRY(mtw_record(m, path, &after, e));
    if (!patch_record_equal(&after, &a->before_file))
        return mtw_fail(m, e, "verification_failed", "The retry uninstaller could not be verified.");
    MTW_TRY(mtw_registry_snapshot(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, e) &&
            medieval_registry_ownership(&m->identity, &registry, r->installation_id, e) &&
            mtw_installed_registry(m, r, &registry, &installed, e) &&
            mtw_registry_write_checked(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, installed, e));
    return mtw_verify_installed(m, r, e);
}
static inline int mtw_complete_removal(MedievalEngine *m, MedievalTransaction *t, JsonValue **result, PatchError *error) {
    PatchError cleanup_error = {0};
    PatchError *e = error;
    const MedievalReceipt *r = &t->typed.receipt;
    const MedievalAction *remover = NULL, *registration = NULL, *receipt_action = NULL;
    size_t i, uninstaller_index = 0;
    PatchFileRecord current;
    PatchRegistrySnapshot registry, removed_registry;
    JsonValue *removed, *raw;
    wchar_t *path;
    int unknown, unknown_originals, equal;
    for (i = 0; i < t->typed.count; ++i) {
        const MedievalAction *a = &t->typed.actions[i];
        size_t index;
        if (a->is_registry) {
            if (!strcmp(a->name, m->identity.registration_key)) registration = a;
            continue;
        }
        if (!strcmp(a->relative, MEDIEVAL_UNINSTALL_NAME)) { remover = a; uninstaller_index = i; }
        if (!strcmp(a->relative, MTW_RECEIPT_RELATIVE)) receipt_action = a;
        if (medieval_path_role(a->relative, &index) == 1) {
            MTW_TRY(mtw_record(m, mtw_live(m, a->relative, e), &current, e));
            if (!patch_record_equal(&current, &a->after_file))
                return mtw_fail(m, e, "recovery_conflict", "Restored runtime changed before cleanup: %s. Keep recovery state and resolve the changed file before retrying.", a->relative);
        }
    }
    if (!remover || !registration || !receipt_action) return json_invalid(e, "Incomplete removal transaction.");
    m->outcome->restoration = "verified";
    m->observation_count = 0;
    m->recovery_archive = (char *)mtw_s(t->json, "recovery_archive");
    e = &cleanup_error;
#define MTW_CLEAN(x) do { if (!(x)) goto incomplete; } while (0)
    /* A terminal journal may already have retired its after-copy. Validate any
       surviving live receipt against the retained baseline before retiring even
       one private original, not just against the journal's file checksum. */
    path = mtw_live(m, MTW_RECEIPT_RELATIVE, e);
    MTW_CLEAN(mtw_record(m, path, &current, e));
    if (current.exists) {
        MedievalReceipt live_receipt;
        if (!patch_record_equal(&current, &receipt_action->after_file)) {
            mtw_fail(m, e, "cleanup_pending", "The receipt changed during cleanup; it was preserved."); goto incomplete;
        }
        MTW_CLEAN(mtw_read_json(m, &m->guard, path, &raw, NULL, e) &&
                  medieval_receipt_validate(&m->document, raw, &m->identity, 1, &live_receipt, e) &&
                  mtw_receipt_bound(m, &live_receipt, r, "restored", e));
    }
    for (i = 0; i < medieval_receipt_file_count(r); ++i) {
        if (!r->files[i].original.exists) continue;
        path = mtw_path(m, m->state, r->files[i].snapshot_relative, e);
        MTW_CLEAN(mtw_record(m, path, &current, e));
        if (current.exists) {
            if (!patch_record_equal(&current, &r->files[i].original)) {
                mtw_fail(m, e, "cleanup_pending", "An original changed during cleanup: %s. It was preserved.", r->files[i].name); goto incomplete;
            }
            MTW_CLEAN(patch_file_remove(&m->guard, path, &current, e));
        }
    }
    path = mtw_live(m, MTW_RECEIPT_RELATIVE, e);
    MTW_CLEAN(mtw_record(m, path, &current, e));
    if (current.exists) {
        if (!patch_record_equal(&current, &receipt_action->after_file)) {
            mtw_fail(m, e, "cleanup_pending", "The receipt changed during cleanup; it was preserved."); goto incomplete;
        }
        MTW_CLEAN(mtw_unknown_children(m, m->state, 1, &unknown, e) &&
                  mtw_unknown_children(m, mtw_path(m, m->state, "originals", e), 0, &unknown_originals, e));
        if (!unknown && !unknown_originals) MTW_CLEAN(patch_file_remove(&m->guard, path, &current, e));
    }
    MTW_CLEAN(mtw_remove_empty(m, mtw_path(m, m->state, "originals", e), e) && mtw_remove_empty(m, m->state, e));
    MTW_CLEAN(mtw_registry_snapshot(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, e) &&
              mtw_registry_compare(m, &registry, &registration->after_registry, &equal, e));
    if (!equal) {
        MTW_CLEAN(medieval_registry_ownership(&m->identity, &registry, r->installation_id, e) &&
                  patch_registry_removed(&m->identity.registry, &m->document, &registry, &removed, e) &&
                  patch_registry_validate(&m->identity.registry, removed, &removed_registry, e) &&
                  mtw_registry_compare(m, &registry, &removed_registry, &equal, e));
        if (!equal) MTW_CLEAN(mtw_registry_write_checked(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, removed, e));
    }
    MTW_CLEAN(mtw_fault(m, "cleanup-uninstaller", e));
    path = mtw_live(m, MEDIEVAL_UNINSTALL_NAME, e);
    MTW_CLEAN(mtw_record(m, path, &current, e));
    if (current.exists) {
        if (!patch_record_equal(&current, &remover->after_file)) {
            mtw_fail(m, e, "cleanup_pending", "The root uninstaller changed during cleanup; it was retained."); goto incomplete;
        }
        MTW_CLEAN(patch_file_remove(&m->guard, path, &current, e));
    }
    m->outcome->removal_completed = 1;
    if (!mtw_transaction_clear(m, t, e)) {
        patch_error_set(error, "cleanup_pending", "Game restoration and removal of the Windows entry and uninstaller are complete. Recovery-file cleanup is incomplete; preserve the retired recovery directory.", cleanup_error.win32);
        return 0;
    }
    e = error;
    if (mtw_directory_exists(m->state)) MTW_TRY(mtw_warning(m, "Unknown files in the patch metadata directory were retained.", e));
    *result = json_new(&m->document, JSON_OBJECT, e);
    return *result && mtw_text(&m->document, *result, "status", "ok", e) &&
           mtw_text(&m->document, *result, "action", "restore", e) && mtw_text(&m->document, *result, "restoration", "verified", e) &&
           mtw_text(&m->document, *result, "restored_preinstall_mode", r->preinstall_mode, e) &&
           mtw_text(&m->document, *result, "restoration_kind", mtw_s(r->json, "restoration_kind"), e) &&
           mtw_text(&m->document, *result, "registration", "removed", e) && mtw_text(&m->document, *result, "uninstaller", "removed", e) &&
           mtw_text(&m->document, *result, "target", m->identity.target, e);
incomplete:
    {
        PatchError retry = {0};
        char first[2048];
        int verified;
        memcpy(first, m->outcome->detail, sizeof first);
        verified = mtw_retry_route(m, t, uninstaller_index, &retry);
        snprintf(m->outcome->detail, sizeof m->outcome->detail,
                 "Game restoration is verified, but final cleanup is incomplete: %.1300s %s",
                 first[0] ? first : (e && e->message ? e->message : "Cleanup failed."),
                 verified ? "Keep and run the verified root uninstaller again." :
                 "A verified retry executable could not be restored. Keep all recovery files and run the current setup to finish recovery.");
    }
    patch_error_set(error, "cleanup_pending", "Game restoration is verified, but final cleanup is incomplete. Keep the verified removal program and recovery journal for retry.", cleanup_error.win32);
    return 0;
#undef MTW_CLEAN
}
static inline int mtw_remove(MedievalEngine *m, const MedievalReceipt *receipt, JsonValue **result, PatchError *e) {
    MedievalTransaction t = {0};
    MedievalArchiveSource sources[48];
    size_t count = 0, i;
    PatchRegistrySnapshot registry;
    JsonValue *removed, *restored, *raw;
    PatchFileRecord current, empty = {0};
    wchar_t *uninstaller, *next;
    char relative[192];
    MTW_TRY(mtw_observe(m, e));
    if (receipt->legacy) return mtw_fail(m, e, "migration_required", "This is a legacy installation. Run the current setup once to create its root-level uninstaller and verified removal identity, then uninstall.");
    MTW_TRY(mtw_registry_snapshot(m, "CurrentUser", "Registry32", m->identity.registration_key, &registry, e) &&
            medieval_registry_ownership(&m->identity, &registry, receipt->installation_id, e));
    for (i = 0; i < medieval_receipt_file_count(receipt); ++i) {
        const MedievalFileState *f = &receipt->files[i];
        MTW_TRY(mtw_relative_record(m, f->name, &current, e));
        if (current.exists && strcmp(current.sha256, f->installed.sha256) &&
            (!f->original.exists || strcmp(current.sha256, f->original.sha256)))
            MTW_TRY(mtw_archive_source(m, sources, &count, f->name, f->name, e));
    }
    uninstaller = mtw_live(m, MEDIEVAL_UNINSTALL_NAME, e);
    MTW_TRY(mtw_record(m, uninstaller, &current, e));
    if (!current.exists) return mtw_fail(m, e, "uninstaller_missing", "The root uninstaller is missing. Run setup to recover the verified removal program before removing the patch.");
    if (strcmp(current.sha256, receipt->uninstaller_sha256))
        return mtw_fail(m, e, "uninstaller_changed", "The root uninstaller has changed. Preserve it outside the game folder, then run setup to recover the verified removal program.");
    MTW_TRY(mtw_writable(m, 0, e) && mtw_archive(m, sources, count, "Changed managed files preserved before patch removal", e) &&
            mtw_fault(m, "after-preservation", e) && mtw_transaction_prepare(m, &t, "restore", receipt, e));
#define MTW_REMOVE(x) do { if (!(x)) goto failed; } while (0)
    MTW_REMOVE(mtw_fault(m, "before-stage-originals", e));
    for (i = 0; i < medieval_receipt_file_count(receipt); ++i) {
        const MedievalFileState *f = &receipt->files[i];
        wchar_t *source = f->original.exists ? mtw_path(m, m->state, f->snapshot_relative, e) : NULL;
        PatchFileRecord after;
        if (f->original.exists && !source) goto failed;
        MTW_REMOVE(mtw_transaction_add_file(m, &t, f->name, source, e));
        raw = json_get(json_get(t.json, "actions")->last, "after");
        MTW_REMOVE(medieval_file_record(raw, &after, e));
        if (!patch_record_equal(&after, &f->original)) {
            mtw_fail(m, e, "receipt_invalid", "The private original for %s changed after validation. Runtime files were retained.", f->name); goto failed;
        }
        if (f->sidecar_created) {
            snprintf(relative, sizeof relative, "%s.unofficial-patch.bak", f->name);
            MTW_REMOVE(mtw_relative_record(m, relative, &current, e));
            if (patch_record_equal(&current, &f->original)) MTW_REMOVE(mtw_transaction_add_file(m, &t, relative, NULL, e));
        }
    }
    MTW_REMOVE(mtw_transaction_add_file(m, &t, MEDIEVAL_UNINSTALL_NAME, uninstaller, e));
    if (strcmp(mtw_s(json_get(json_get(t.json, "actions")->last, "after"), "sha256"), receipt->uninstaller_sha256)) {
        mtw_fail(m, e, "uninstaller_changed", "The root uninstaller changed during removal preparation. Runtime files were retained."); goto failed;
    }
    restored = json_clone(&m->document, receipt->json, e);
    next = mtw_path(m, t.directory, "next-receipt.json", e);
    MTW_REMOVE(restored && mtw_text(&m->document, restored, "status", "restored", e) &&
               mtw_checked_write(m, restored, next, &empty, e) && mtw_transaction_add_file(m, &t, MTW_RECEIPT_RELATIVE, next, e) &&
               mtw_record(m, next, &current, e) && patch_file_remove(&m->guard, next, &current, e) &&
               patch_registry_removed(&m->identity.registry, &m->document, &registry, &removed, e) &&
               mtw_transaction_add_registry(m, &t, "CurrentUser", "Registry32", m->identity.registration_key, registry.json, removed, e) &&
               mtw_transaction_publish(m, &t, e) && mtw_transaction_commit(m, &t, e));
    return mtw_complete_removal(m, &t, result, e);
failed:
    return mtw_transaction_failure(m, &t, e);
#undef MTW_REMOVE
}

static inline int mtw_initialize_log(MedievalEngine *m, PatchError *e) {
    wchar_t *log, *dedicated, *parent, *native;
    const wchar_t *game = m->guard.canonical, *payload = m->payload_guard.canonical;
    const wchar_t *inputs[3] = {m->options.request, m->options.installer, m->options.uninstaller};
    PatchGuard input_guards[3];
    HANDLE candidate = INVALID_HANDLE_VALUE;
    int equal, under, allowed, ok = 0;
    size_t i;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER zero;
    if (!m->options.log || !*m->options.log) return 1;
    memset(input_guards, 0, sizeof input_guards);
    /* No diagnostic handle becomes writable authority until target, payload,
       every supplied input and the log itself have passed guarded validation. */
#define MTW_LOG_CHECK(x) do { if (!(x)) goto done; } while (0)
    if (!game || !payload) {
        mtw_fail(m, e, "invalid_request", "Guarded input paths are required before opening diagnostics."); goto done;
    }
    MTW_LOG_CHECK(patch_path_normalize(&m->memory, m->options.log, &log, e));
    dedicated = mtw_path(m, game, "Unofficial Medieval Patch Logs", e);
    MTW_LOG_CHECK(dedicated && patch_path_equal(&m->guard.platform, log, game, &equal, e) &&
                  patch_path_under(&m->guard.platform, log, game, &under, e) &&
                  patch_path_under(&m->guard.platform, log, dedicated, &allowed, e));
    if (equal || (under && !allowed)) {
        mtw_fail(m, e, "invalid_request", "Diagnostics must use a separate log folder, not game, payload or recovery files."); goto done;
    }
    MTW_LOG_CHECK(patch_path_equal(&m->guard.platform, log, payload, &equal, e) &&
                  patch_path_under(&m->guard.platform, log, payload, &under, e));
    if (equal || under) {
        mtw_fail(m, e, "invalid_request", "Diagnostics must use a separate log folder, not game, payload or recovery files."); goto done;
    }
    for (i = 0; i < 3; ++i) if (inputs[i] && *inputs[i]) {
        wchar_t *input, *input_parent;
        MTW_LOG_CHECK(patch_path_normalize(&m->memory, inputs[i], &input, e) &&
                      patch_parent_path(&m->memory, input, &input_parent, e) &&
                      patch_guard_open_internal(&input_guards[i], input_parent, 1, e) &&
                      patch_guard_file(&input_guards[i], input, e) &&
                      patch_path_equal(&m->guard.platform, log, input, &equal, e));
        if (equal) {
            mtw_fail(m, e, "invalid_request", "The diagnostic log aliases an installer input; all input files were preserved."); goto done;
        }
    }
    MTW_LOG_CHECK(patch_parent_path(&m->memory, log, &parent, e) &&
                  patch_guard_open_internal(&m->log_guard, parent, 1, e) && patch_guard_file(&m->log_guard, log, e) &&
                  patch_native_path(&m->memory, log, &native, e));
    candidate = CreateFileW(native, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, NULL);
    if (candidate == INVALID_HANDLE_VALUE) {
        patch_error_set(e, "diagnostic_write_failed", "Could not open the persistent diagnostic log.", GetLastError()); goto done;
    }
    MTW_LOG_CHECK(patch_handle_info(candidate, 0, &info, e) &&
                  patch_final_path(&m->log_guard, candidate, log, &m->memory, NULL, e));
    zero.QuadPart = 0;
    if (!SetFilePointerEx(candidate, zero, NULL, FILE_END)) {
        patch_error_set(e, "diagnostic_write_failed", "Could not append the diagnostic log.", GetLastError()); goto done;
    }
    m->log_handle = candidate; candidate = INVALID_HANDLE_VALUE; ok = 1;
done:
    if (candidate != INVALID_HANDLE_VALUE) CloseHandle(candidate);
    for (i = 0; i < 3; ++i) patch_guard_close(&input_guards[i]);
    return ok;
#undef MTW_LOG_CHECK
}
static inline int mtw_game_closed(MedievalEngine *m, PatchError *e) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W p;
    DWORD pid = 0, last = 0;
    int busy = 0;
    wchar_t *target = mtw_path(m, m->guard.canonical, "Medieval_TW.exe", e);
    if (!target) { if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap); return 0; }
    if (snap == INVALID_HANDLE_VALUE) {
        patch_error_set(e, "game_running", "Could not check whether the game is running.", GetLastError()); return 0;
    }
    memset(&p, 0, sizeof p); p.dwSize = sizeof p;
    if (!Process32FirstW(snap, &p)) last = GetLastError();
    else do {
        PatchError query = {0};
        int matches;
        if (_wcsicmp(p.szExeFile, L"Medieval_TW.exe")) continue;
        if (!patch_process_is_target(&m->guard.platform, p.th32ProcessID, target, &matches, &query) || matches) {
            busy = 1; pid = p.th32ProcessID; break;
        }
    } while (Process32NextW(snap, &p));
    if (!busy && !last) last = GetLastError();
    CloseHandle(snap);
    if (busy) return mtw_fail(m, e, "game_running", "Close this Medieval: Total War game before continuing (PID %lu).", (unsigned long)pid);
    if (last != ERROR_NO_MORE_FILES) {
        patch_error_set(e, "game_running", "Could not finish checking whether the game is running.", last); return 0;
    }
    return 1;
}
static inline int mtw_initialize(MedievalEngine *m, PatchError *e) {
    PatchFileRecord executable;
    wchar_t *path, *mutex_name;
    DWORD wait;
    size_t i;
    patch_platform_init(&m->guard.platform);
    MTW_TRY(patch_guard_open(&m->guard, m->options.target, e));
    path = mtw_path(m, m->guard.canonical, "Medieval_TW.exe", e);
    MTW_TRY(path && patch_file_pin(&m->guard, path, &m->executable_pin, &executable, e));
    if (!executable.exists) return mtw_fail(m, e, "target_missing", "Medieval_TW.exe was not found in the selected folder.");
    if (strcmp(executable.sha256, MEDIEVAL_EXECUTABLE_HASH) &&
        strcmp(executable.sha256, MTW_SCROLL_PATCHED_SHA256) &&
        strcmp(executable.sha256, MTW_SPRITE_ONLY_SHA256) &&
        strcmp(executable.sha256, MTW_SCROLL_SPRITE_SHA256))
        return mtw_fail(m, e, "unsupported_executable", "This Medieval_TW.exe build is unsupported (SHA-256 %s). No files were changed.", executable.sha256);
    if (mtw_is(m->options.operation, "Install") || mtw_is(m->options.operation, "Restore")) {
        /* The verified EXE can now be a transaction target. A read-only pin
           would block its replacement; the later observation and transaction
           compare the full identity again before publication. */
        CloseHandle(m->executable_pin);
        m->executable_pin = INVALID_HANDLE_VALUE;
    }
    if (m->options.fault && *m->options.fault) {
        wchar_t enabled[8] = {0};
        PatchFileRecord marker;
        GetEnvironmentVariableW(L"MTW_ENABLE_LIFECYCLE_FAULTS", enabled, 8);
        MTW_TRY(mtw_relative_record(m, ".umtwp-test-fixture", &marker, e));
        if (wcscmp(enabled, L"1") || !marker.exists)
            return mtw_fail(m, e, "invalid_request", "Fault injection is restricted to explicitly marked disposable test fixtures.");
    }
    /* The C core uses XP-capable primitives, but the present terrain payload
       imports QueryDisplayConfig and must never be installed below Windows 7. */
    if (mtw_is(m->options.operation, "Install") && m->options.terrain_fix) {
        OSVERSIONINFOW os;
        memset(&os, 0, sizeof os); os.dwOSVersionInfoSize = sizeof os;
        if (!GetVersionExW(&os)) { patch_error_set(e, "platform_unsupported", "Could not verify the Terrain Movement Fix platform requirements.", GetLastError()); return 0; }
        if ((m->options.fault && strstr(m->options.fault, "os:pre-win7")) || os.dwMajorVersion < 6 || (os.dwMajorVersion == 6 && os.dwMinorVersion < 1))
            return mtw_fail(m, e, "platform_unsupported", "The Terrain Movement Fix requires Windows 7 or later. No runtime or registration changes were made.");
    }
    m->state = mtw_path(m, m->guard.canonical, MEDIEVAL_STATE_NAME, e);
    m->transaction_path = mtw_path(m, m->guard.canonical, MTW_TRANSACTION_NAME, e);
    MTW_TRY(m->state && m->transaction_path && medieval_identity_open(&m->identity, &m->guard, e) &&
            patch_utf8_to_wide(&m->memory, m->identity.mutex_name, strlen(m->identity.mutex_name), &mutex_name, e));
    m->mutex = CreateMutexW(NULL, FALSE, mutex_name);
    if (!m->mutex) { patch_error_set(e, "operation_in_progress", "Could not acquire the installation lock. Close another patch operation or use the installing account.", GetLastError()); return 0; }
    wait = WaitForSingleObject(m->mutex, 0);
    m->mutex_owned = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    if (!m->mutex_owned) return mtw_fail(m, e, "operation_in_progress", "Another patch operation is using this game folder. Wait for it to finish and retry.");
    {
        PatchError payload_error = {0};
        if (!patch_guard_open(&m->payload_guard, m->options.payload, &payload_error) ||
            !mtw_read_json(m, &m->payload_guard, mtw_path(m, m->payload_guard.canonical, "payload-manifest.json", &payload_error), &m->payload, NULL, &payload_error)) {
            patch_error_set(e, "invalid_payload", "The embedded compatibility payload is missing or invalid.", payload_error.win32); return 0;
        }
    }
    if (strcmp(mtw_s(json_get(m->payload, "target_executable"), "sha256"), MEDIEVAL_EXECUTABLE_HASH))
        return mtw_fail(m, e, "invalid_payload", "The payload targets an unexpected game executable.");
    for (i = 0; i < MEDIEVAL_RUNTIME_COUNT && m->options.terrain_fix; ++i) {
        const JsonValue *raw = json_get(json_get(m->payload, "files"), medieval_payload_names[i]);
        const JsonValue *length = json_get(raw, "length");
        const char *hash = medieval_string(raw, "sha256", 0, e);
        PatchFileRecord actual;
        if (!hash) return 0;
        if (!medieval_hash_valid(hash) || !length || length->type != JSON_NUMBER || length->number <= 0)
            return mtw_fail(m, e, "invalid_payload", "Embedded payload descriptor is invalid for %s.", medieval_payload_names[i]);
        path = mtw_path(m, m->payload_guard.canonical, medieval_payload_names[i], e);
        MTW_TRY(path && patch_file_record(&m->payload_guard, path, &actual, e));
        if (!actual.exists || strcmp(actual.sha256, hash) || actual.length != (uint64_t)length->number)
            return mtw_fail(m, e, "invalid_payload", "Embedded payload verification failed for %s.", medieval_payload_names[i]);
        m->payload_records[i] = actual;
    }
    m->payload_records[5].exists = 1;
    m->payload_records[5].length = m->options.scroll_fix ?
        (m->options.sprite_fix ? MTW_SCROLL_SPRITE_SIZE : MTW_SCROLL_PATCHED_SIZE) :
        (m->options.sprite_fix ? MTW_SPRITE_ONLY_SIZE : MTW_SCROLL_STOCK_SIZE);
    memcpy(m->payload_records[5].sha256,
           m->options.scroll_fix ?
               (m->options.sprite_fix ? MTW_SCROLL_SPRITE_SHA256 : MTW_SCROLL_PATCHED_SHA256) :
               (m->options.sprite_fix ? MTW_SPRITE_ONLY_SHA256 : MEDIEVAL_EXECUTABLE_HASH), 65);
    return (!m->options.terrain_fix ||
            mtw_config_check(m, &m->payload_guard, mtw_path(m, m->payload_guard.canonical, "dgVoodoo.conf", e), e)) &&
           mtw_initialize_log(m, e) && mtw_log(m, m->options.operation, e);
}
static inline void medieval_outcome_close(MedievalOutcome *outcome) {
    if (!outcome) return;
    json_document_close(&outcome->document); outcome->result = NULL;
}
static inline void mtw_engine_close(MedievalEngine *m) {
    if (m->executable_pin != INVALID_HANDLE_VALUE) CloseHandle(m->executable_pin);
    if (m->mutex_owned) ReleaseMutex(m->mutex);
    if (m->mutex) CloseHandle(m->mutex);
    if (m->log_handle != INVALID_HANDLE_VALUE) CloseHandle(m->log_handle);
    json_document_close(&m->document);
    medieval_identity_close(&m->identity);
    patch_guard_close(&m->log_guard); patch_guard_close(&m->payload_guard); patch_guard_close(&m->guard);
    patch_context_close(&m->memory);
}
static inline int medieval_failure_result(MedievalOutcome *o, const MedievalOptions *options,
                                          const PatchError *error, const char *archive, PatchError *e) {
    JsonDocument *d = &o->document;
    JsonValue *r;
    /* The result classification must survive even if allocating its JSON fails. */
    o->exit_code = o->removal_completed ? 4 : mtw_is(o->restoration, "verified") ? 3 : 2;
    if (error && mtw_is(error->code, "elevation_required")) o->exit_code = ERROR_ELEVATION_REQUIRED;
    r = json_new(d, JSON_OBJECT, e);
    const char *message = o->detail[0] ? o->detail : error && error->message ? error->message : "The operation could not complete.";
    MTW_TRY(r && mtw_text(d, r, "status", "error", e) && mtw_text(d, r, "code", error && error->code ? error->code : "unexpected_error", e) &&
            mtw_text(d, r, "operation", options && options->operation ? options->operation : "", e) && mtw_text(d, r, "message", message, e) &&
            mtw_text(d, r, "rollback", o->rollback ? o->rollback : "not-needed", e) &&
            mtw_text(d, r, "restoration", o->restoration ? o->restoration : "not-started", e) &&
            mtw_boolean(d, r, "removal_completed", o->removal_completed, e) && mtw_text(d, r, "recovery_archive", archive ? archive : "", e));
    if (error && error->win32) MTW_TRY(mtw_number(d, r, "win32_error", error->win32, e));
    o->result = d->root = r;
    return 1;
}
static inline int medieval_run(const MedievalOptions *options, MedievalOutcome *outcome, PatchError *e) {
    MedievalEngine m;
    MedievalReceipt receipt;
    JsonValue *recovered = NULL, *result = NULL;
    const char *mode = NULL;
    int pending = 0, present = 0, ok = 0;
    char *bytes = NULL;
    size_t length;
    PatchError diagnostic = {0};
    if (!options || !outcome) { patch_error_set(e, "invalid_argument", "Options and an empty outcome are required.", ERROR_INVALID_PARAMETER); return 0; }
    memset(&m, 0, sizeof m);
    m.options = *options; m.outcome = outcome; m.executable_pin = m.log_handle = INVALID_HANDLE_VALUE;
    if (!m.options.selection_provided) { m.options.terrain_fix = 1; m.options.sprite_fix = 1; }
    outcome->rollback = "not-needed"; outcome->restoration = "not-started"; outcome->exit_code = 2;
    if (!options->target || !*options->target || !options->payload || !*options->payload || !options->version ||
        (!mtw_is(options->operation, "Inspect") && !mtw_is(options->operation, "Install") &&
         !mtw_is(options->operation, "Verify") && !mtw_is(options->operation, "Restore"))) {
        mtw_fail(&m, e, "invalid_request", "A valid operation, target, payload and patch version are required."); goto done;
    }
    if (mtw_is(options->operation, "Install") && !m.options.terrain_fix &&
        !m.options.scroll_fix && !m.options.sprite_fix) {
        mtw_fail(&m, e, "invalid_selection", "Select at least one fix before installing."); goto done;
    }
    m.warnings = json_new(&m.document, JSON_ARRAY, e);
    if (!m.warnings || !mtw_initialize(&m, e)) goto done;
    if (options->require_owner && *options->require_owner && strcmp(options->require_owner, m.identity.owner_sid)) {
        mtw_fail(&m, e, "wrong_account", "Continue with the same Windows account that started setup. Administrator credentials for another account cannot be used to transfer this patch installation."); goto done;
    }
    if ((mtw_is(options->operation, "Install") || mtw_is(options->operation, "Restore")) && !mtw_game_closed(&m, e)) goto done;
    if (!mtw_transaction_recover(&m, &recovered, &pending, e)) goto done;
    if (!mtw_is(options->operation, "Restore")) {
        /* Recovery has finished. A subsequent requested operation owns its own
           outcome; an Install failure must not advertise incomplete removal. */
        outcome->removal_completed = 0;
        outcome->restoration = "not-started";
        outcome->rollback = "not-needed";
    }
    if (!(mtw_is(options->operation, "Restore") && recovered) && !(mtw_is(options->operation, "Inspect") && pending))
        if (!mtw_read_receipt(&m, &receipt, &present, e)) goto done;
    if (mtw_is(options->operation, "Inspect")) {
        if (pending) mode = "recovery-pending";
        else if (present) mode = receipt.preinstall_mode;
        else if (!mtw_preinstall_mode(&m, &mode, e)) goto done;
        result = json_new(&m.document, JSON_OBJECT, e);
        if (!result || !mtw_text(&m.document, result, "status", "ok", e) || !mtw_text(&m.document, result, "action", "inspect", e) ||
            !mtw_text(&m.document, result, "mode", mode, e) || !mtw_boolean(&m.document, result, "recovery_pending", pending, e) ||
            !mtw_boolean(&m.document, result, "managed_installation", present || pending, e) ||
            !mtw_text(&m.document, result, "target", m.identity.target, e) ||
            !mtw_text(&m.document, result, "target_executable_sha256", MEDIEVAL_EXECUTABLE_HASH, e)) goto done;
    } else if (mtw_is(options->operation, "Verify")) {
        if (present && !receipt.legacy) {
            size_t verify_index;
            for (verify_index = 0; verify_index < medieval_receipt_file_count(&receipt); ++verify_index)
                m.payload_records[verify_index] = receipt.files[verify_index].installed;
            if (!receipt.old_v2 &&
                !medieval_boolean(receipt.json, "terrain_fix_enabled", &m.options.terrain_fix, e)) goto done;
        }
        if (!mtw_verify_runtime(&m, e) || (present && !receipt.legacy && !mtw_verify_installed(&m, &receipt, e))) goto done;
        result = json_new(&m.document, JSON_OBJECT, e);
        if (!result || !mtw_text(&m.document, result, "status", "ok", e) || !mtw_text(&m.document, result, "action", "verify", e) ||
            !mtw_text(&m.document, result, "mode", "r186", e) || !mtw_text(&m.document, result, "target", m.identity.target, e) ||
            !mtw_text(&m.document, result, "target_executable_sha256", MEDIEVAL_EXECUTABLE_HASH, e)) goto done;
    } else if (mtw_is(options->operation, "Install")) {
        if (!mtw_writable(&m, mtw_is(options->fault, "space:maximum") ? UINT64_MAX : 16 * 1024 * 1024, e) ||
            !mtw_install(&m, present ? &receipt : NULL, &result, e)) goto done;
    } else if (recovered) result = recovered;
    else {
        if (!present) { mtw_fail(&m, e, "receipt_invalid", "No managed patch installation was found. Keep this folder and any recovery files; run setup to inspect it."); goto done; }
        if (!mtw_remove(&m, &receipt, &result, e)) goto done;
    }
    if (!mtw_text(&m.document, result, "recovery_archive", m.recovery_archive ? m.recovery_archive : "", e) ||
        !json_set(&m.document, result, "warnings", json_clone(&m.document, m.warnings, e), e) ||
        !mtw_fault(&m, "before-result-log", e) || !json_write_value(&m.document, result, NULL, &bytes, &length, e) ||
        !mtw_log(&m, bytes, e)) goto done;
    outcome->result = json_clone(&outcome->document, result, e);
    if (!outcome->result) goto done;
    outcome->document.root = outcome->result; outcome->exit_code = 0; ok = 1;
done:
    if (!ok) {
        if (!e || !e->code) patch_error_set(e, "unexpected_error", "The operation could not complete.", GetLastError());
        medieval_failure_result(outcome, options, e, m.recovery_archive, &diagnostic);
        if (outcome->result && m.guard.recovery_path) {
            char *retained;
            size_t retained_length;
            if (patch_wide_to_utf8(&m.memory, m.guard.recovery_path, wcslen(m.guard.recovery_path), &retained, &retained_length, &diagnostic))
                mtw_text(&outcome->document, outcome->result, "recovery_file", retained, &diagnostic);
        }
        if (outcome->result && json_write_value(&outcome->document, outcome->result, NULL, &bytes, &length, &diagnostic))
            mtw_log(&m, bytes, &diagnostic);
    }
    mtw_engine_close(&m);
    return ok;
}

#undef MTW_TRY
#endif
