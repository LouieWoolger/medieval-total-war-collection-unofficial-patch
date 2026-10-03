/* Every mutation below is confined to the disposable root supplied by pytest. */
#include "patch_platform.h"
#include <stdio.h>
#include <wchar.h>

static BOOL WINAPI fixture_write(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI fixture_flush(HANDLE);
static BOOL WINAPI fixture_info(HANDLE, LPBY_HANDLE_FILE_INFORMATION);
static HANDLE WINAPI fixture_open(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
#define WriteFile fixture_write
#define FlushFileBuffers fixture_flush
#define CreateFileW fixture_open
#define GetFileInformationByHandle fixture_info
#include "patch_files.h"
#undef WriteFile
#undef FlushFileBuffers
#undef CreateFileW
#undef GetFileInformationByHandle

static unsigned passed, failed;
static int mode, write_fault, flush_fault, flush_at, flush_count, info_fault, open_swap;
static int ancestor_attempted, ancestor_moved;
static wchar_t ancestor_path[32768], ancestor_leaf[32768], ancestor_saved[32768];
static wchar_t fault_destination[32768], swap_target[32768], swap_saved[32768];
static wchar_t previous_slot[32768], previous_saved[32768];
static int previous_swap, previous_published, previous_swapped;
static PatchSetInfoFn real_set;
static PatchNtSetInfoFn real_nt;
static int legacy;
static wchar_t supplied_process_image[32768];
static BOOL WINAPI image_query(HANDLE h, DWORD flags, LPWSTR out, PDWORD length)
{
    size_t n=wcslen(supplied_process_image);(void)h;(void)flags;
    if(n>=*length){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    wcscpy(out,supplied_process_image);*length=(DWORD)n;return TRUE;
}
static DWORD WINAPI module_query(HANDLE h, HMODULE module, LPWSTR out, DWORD length)
{
    (void)module;return image_query(h,0,out,&length)?length:0;
}

static void check(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) ++passed; else ++failed;
}
static void rejected(int ok, const PatchError *e, const char *code, const char *name)
{
    check(!ok && e->code && (!code || !strcmp(code, e->code)), name);
    if (ok || !e->code || (code && strcmp(code, e->code)))
        printf("  got=%d code=%s native=%lu\n", ok, e->code ? e->code : "none", (unsigned long)e->win32);
}
static void join(wchar_t *out, const wchar_t *root, const wchar_t *leaf)
{
    swprintf(out, 32768, L"%ls\\%ls", root, leaf);
}
static int raw_write(const wchar_t *path, const char *text)
{
    DWORD n = 0;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    int ok = h != INVALID_HANDLE_VALUE && WriteFile(h, text, (DWORD)strlen(text), &n, NULL) && n == strlen(text);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    return ok;
}
static BOOL WINAPI fixture_write(HANDLE h, LPCVOID data, DWORD size, LPDWORD got, LPOVERLAPPED over)
{
    if (write_fault == 1) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    if (write_fault == 2) { *got = 0; return TRUE; }
    if (write_fault == 3 && size > 2) size = 2;
    return WriteFile(h, data, size, got, over);
}
static BOOL WINAPI fixture_flush(HANDLE h)
{
    ++flush_count;
    if (flush_fault || (flush_at && flush_at == flush_count)) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    return FlushFileBuffers(h);
}
static BOOL WINAPI fixture_info(HANDLE h, LPBY_HANDLE_FILE_INFORMATION info)
{
    BOOL ok=GetFileInformationByHandle(h,info);
    if(ok&&info_fault){info->nFileIndexHigh=0;info->nFileIndexLow=0;}
    return ok;
}
static HANDLE WINAPI fixture_open(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    if(previous_swap&&previous_published&&(a&GENERIC_READ)&&c==OPEN_EXISTING&&
       wcsstr(p,previous_slot)&&wcslen(wcsstr(p,previous_slot))==wcslen(previous_slot)){
        int action=previous_swap;previous_swap=0;
        /* The published copy's handle must have closed for this real rename to
           succeed. Substitute at the next read of the freshly created slot. */
        if(!MoveFileExW(previous_slot,previous_saved,0))ExitProcess(95);
        if(action==1&&!raw_write(previous_slot,"foreign"))ExitProcess(96);
        previous_swapped=1;
    }
    if(ancestor_leaf[0]&&wcsstr(p,ancestor_leaf)){
        ancestor_leaf[0]=0;ancestor_attempted=1;
        ancestor_moved=MoveFileExW(ancestor_path,ancestor_saved,0)!=FALSE;
        if(ancestor_moved&&!CreateDirectoryW(ancestor_path,NULL))ExitProcess(94);
    }
    if (open_swap && wcsstr(p, swap_target) && (a & DELETE)) {
        open_swap = 0;
        if (!MoveFileExW(swap_target, swap_saved, 0) || !CreateDirectoryW(swap_target, NULL)) ExitProcess(93);
    }
    return CreateFileW(p, a, s, sa, c, f, t);
}
static int before_info(int rename, const wchar_t *p)
{
    if (rename && mode == 5 && wcsstr(p, L".mtw-before-")) { mode = 0; return 0; }
    if (rename && mode == 6 && !wcsstr(p, L".mtw-before-")) { mode = 0; return 0; }
    if (!rename && mode == 7) { mode = 0; return 0; }
    return 1;
}
static void after_info(int rename, const wchar_t *p)
{
    if (!rename) return;
    if(previous_swap&&wcsstr(p,previous_slot)&&
       wcslen(wcsstr(p,previous_slot))==wcslen(previous_slot))previous_published=1;
    if (mode == 3 && wcsstr(p, fault_destination) &&
        wcslen(wcsstr(p, fault_destination)) == wcslen(fault_destination)) ExitProcess(79);
    if (mode == 4 && wcsstr(p, L".mtw-previous")) ExitProcess(80);
    if (wcsstr(p, L".mtw-before-")) {
        if (mode == 2) ExitProcess(77);
        if (mode == 1) { mode = 0; if (!raw_write(fault_destination, "foreign")) ExitProcess(91); }
    }
}
static BOOL WINAPI set_info(HANDLE h, int kind, LPVOID data, DWORD size)
{
    PatchRenameInfo *r = (PatchRenameInfo *)data;
    const wchar_t *p = kind == 3 ? r->FileName : L"";
    if (!before_info(kind == 3, p)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if (!real_set(h, kind, data, size)) return FALSE;
    after_info(kind == 3, p);
    return TRUE;
}
static LONG NTAPI nt_info(HANDLE h, PatchIoStatus *io, PVOID data, ULONG size, int kind)
{
    PatchRenameInfo *r = (PatchRenameInfo *)data;
    const wchar_t *p = kind == 10 ? r->FileName : L"";
    LONG result;
    if (!before_info(kind == 10, p)) return (LONG)0xc0000022;
    result = real_nt(h, io, data, size, kind);
    if (result >= 0) after_info(kind == 10, p);
    return result;
}
static void platform(PatchPlatform *p)
{
    patch_platform_init(p);
    real_set = p->set_info;
    real_nt = p->nt_set_info;
    if (legacy) { p->final_path = NULL; p->compare_ordinal = NULL; p->query_image = NULL; p->set_info = NULL; }
    else if (p->set_info) p->set_info = set_info;
    if (p->nt_set_info) p->nt_set_info = nt_info;
}
static int open_guard(PatchGuard *g, const wchar_t *path, PatchError *e)
{
    platform(&g->platform);
    return patch_guard_open(g, path, e);
}
static int content(PatchGuard *g, const wchar_t *p, const char *expected)
{
    PatchContext c = {0}; PatchError e = {0}; char *data = NULL; size_t n = 0;
    int ok = patch_file_read(g, p, 1024 * 1024, &c, &data, &n, &e) && n == strlen(expected) && !memcmp(data, expected, n);
    patch_context_close(&c);
    return ok;
}
static int write_text(PatchGuard *g, const wchar_t *p, const char *text)
{
    PatchError e = {0};
    return patch_file_write_new(g, p, text, strlen(text), &e);
}
static PatchFileRecord record(PatchGuard *g, const wchar_t *p)
{
    PatchError e = {0}; PatchFileRecord r = {0};
    if (!patch_file_record(g, p, &r, &e)) { printf("record failed %s %lu\n", e.code, (unsigned long)e.win32); ++failed; }
    return r;
}
static PatchFileRecord bytes_record(const char *text)
{
    PatchContext c = {0}; PatchFileRecord r = {0};
    r.exists = 1; r.length = strlen(text); patch_sha256_bytes(&c, text, r.length, r.sha256);
    return r;
}
static unsigned remnants(PatchGuard *g, const wchar_t *root, const wchar_t *glob, const char *text)
{
    wchar_t pattern[32768], path[32768]; WIN32_FIND_DATAW data; HANDLE h; unsigned n = 0;
    join(pattern, root, glob); h = FindFirstFileW(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { join(path, root, data.cFileName); if (content(g, path, text)) ++n; } while (FindNextFileW(h, &data));
    FindClose(h); return n;
}
static void test_strings(void)
{
    static const wchar_t *bad[] = {L"relative",L"\\\\server\\share",L"\\\\?\\F:\\x",L"F:\\x:stream",
        L"F:\\..\\x",L"F:\\x.\\y",L"F:\\x \\y",L"F:\\CON",L"F:\\com1.txt",L"F:\\LPT\u00b9",
        L"F:\\x\\\\y",L"G:\\foo\\\\",L"G:\\foo\\NUL.txt",L"G:\\foo\\CONIN$",L"G:\\foo\\a\x1"};
    static const char *invalid[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80", "\xf8\x88\x80\x80\x80"};
    PatchContext c = {0}; PatchError e = {0}; PatchPlatform p; wchar_t *w = NULL; char *u = NULL; size_t n; int equal; unsigned i;
    platform(&p);
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        memset(&e,0,sizeof e); rejected(patch_path_normalize(&c,bad[i],&w,&e),&e,"unsafe_path","unsafe path rejected");
    }
    check(patch_path_join(&c,L"G:/","foo",&w,&e) && !wcscmp(w,L"G:\\foo"),"path_join slash drive root");
    for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        memset(&e,0,sizeof e); rejected(patch_utf8_to_wide(&c,invalid[i],strlen(invalid[i]),&w,&e),&e,"receipt_invalid","invalid UTF8 rejected");
    }
    memset(&e,0,sizeof e);
    rejected(patch_wide_to_utf8(&c,L"\xd800",1,&u,&n,&e),&e,"receipt_invalid","unpaired Unicode surrogate rejected");
    check(patch_utf8_to_wide(&c,"\xc3\xa9 \xf0\x9f\x98\x80",7,&w,&e) &&
          patch_wide_to_utf8(&c,w,wcslen(w),&u,&n,&e) && n==7 && !memcmp(u,"\xc3\xa9 \xf0\x9f\x98\x80",7),"strict Unicode round trip");
    check(patch_path_equal(&p,L"AÉΣ",L"aéσ",&equal,&e) && equal,"ordinal Unicode case equality");
    check(patch_path_equal(&p,L"a-b",L"ab",&equal,&e) && !equal,"ordinal comparison keeps punctuation");
    check(patch_path_equal(&p,L"aß",L"ass",&equal,&e) && !equal,"ordinal comparison rejects linguistic expansion");
    check(patch_path_equal(&p,L"I",L"\u0131",&equal,&e)&&!equal,"ordinal dotless I remains distinct");
    check(patch_path_equal(&p,L"\u0130",L"i",&equal,&e)&&!equal,"ordinal dotted I remains distinct");
    check(patch_path_equal(&p,L"\u03a3",L"\u03c2",&equal,&e)&&!equal,"ordinal final sigma remains distinct");
    check(patch_path_equal(&p,L"\xd801\xdc00",L"\xd801\xdc28",&equal,&e)&&!equal,"ordinal supplementary case remains distinct");
    check(patch_path_equal(&p,L"\u00e9",L"e\u0301",&equal,&e)&&!equal,"ordinal composed and decomposed distinct");
    p.compare_ordinal = NULL; p.upcase = NULL; memset(&e,0,sizeof e);
    rejected(patch_path_equal(&p,L"a",L"A",&equal,&e),&e,"platform_unsupported","missing ordinal capability refuses");
    patch_context_close(&c);
}
typedef struct { unsigned calls, fail_at, live; } Allocator;
static void *allocate(void *user, size_t bytes)
{
    Allocator *a = (Allocator *)user; void *p;
    if (++a->calls == a->fail_at) return NULL;
    p = malloc(bytes); if (p) ++a->live; return p;
}
static void deallocate(void *user, void *p) { Allocator *a = (Allocator *)user; --a->live; free(p); }
static void test_pin_failures(const wchar_t *root)
{
    unsigned count = 0, i;
    wchar_t moved[32768]; swprintf(moved,32768,L"%ls-moved",root);
    for (i=0; i<=count; ++i) {
        PatchGuard g = {0}; PatchError e = {0}; Allocator a = {0}; int ok;
        a.fail_at=i; g.context.allocator.user=&a; g.context.allocator.allocate=allocate; g.context.allocator.deallocate=deallocate;
        ok=open_guard(&g,root,&e); if (!i) count=a.calls;
        check(ok == (i==0), "pin acquisition allocation result");
        patch_guard_close(&g); patch_guard_close(&g);
        check(a.live==0 && !g.context.resources && !g.context.bytes_owned,"partial pin acquisition releases all allocations");
        check(MoveFileExW(root,moved,0) && MoveFileExW(moved,root,0),"partial pin acquisition releases ancestors");
    }
    {PatchGuard g={0};PatchError e={0};wchar_t missing[32768];join(missing,root,L"does-not-exist");
     rejected(open_guard(&g,missing,&e),&e,"io_error","failed constructor missing leaf");patch_guard_close(&g);
     check(MoveFileExW(root,moved,0)&&MoveFileExW(moved,root,0),"failed constructor releases ancestor pins");}
}
static void test_files(const wchar_t *root, const wchar_t *evidence)
{
    PatchGuard g = {0}, ext = {0}, bad = {0}; PatchError e = {0}; PatchFileRecord before, src, actual, absent = {0}, expected;
    wchar_t sub[32768], f[32768], source[32768], copy[32768], other[32768], a[32768], b[32768], c[32768];
    PatchContext output = {0}; char *bytes = NULL; size_t length = 0; HANDLE h;
    check(open_guard(&g,root,&e),"guard open");
    join(a,root,L"clone");patch_guard_directory(&g,a,1,&e);check(open_guard(&ext,root,&e)&&open_guard(&bad,a,&e)&&
        !strcmp(g.identity,ext.identity)&&strcmp(g.identity,bad.identity),"identity follows directory rather than contents");patch_guard_close(&ext);patch_guard_close(&bad);
    check(strlen(g.identity)==25 && g.canonical && g.root,"directory identity canonical path");
    swprintf(a,32768,L"%ls-moved",root);
    check(!MoveFileExW(root,a,0),"directory pinned against rename");
    join(a,root,L"ancestor-race");patch_guard_directory(&g,a,1,&e);join(b,a,L"leaf");
    wcscpy(ancestor_path,a);wcscpy(ancestor_leaf,b);join(ancestor_saved,root,L"ancestor-race-old");
    check(patch_guard_directory(&g,b,1,&e)&&ancestor_attempted&&!ancestor_moved,"ancestor substitution blocked between successive pins");
    join(a,root,L"missing/child/file"); check(!record(&g,a).exists,"missing ancestor record");
    patch_error_set(&e,"prior_failure","A previous operation failed.",ERROR_CRC);
    check(patch_file_record(&g,a,&actual,&e)&&!actual.exists,"missing ancestor status independent of earlier error");
    join(sub,root,L"nested\\unicode-é"); check(patch_guard_directory(&g,sub,1,&e),"create Unicode ancestors");
    join(f,sub,L"日本-\xd83d\xde00.txt"); check(write_text(&g,f,"original"),"Unicode write"); before=record(&g,f);
    expected=bytes_record("original"); check(patch_record_equal(&before,&expected)&&strlen(before.identity)==25,"Unicode write hash read");
    check(content(&g,f,"original"),"read content"); memset(&e,0,sizeof e);
    rejected(patch_file_read(&g,f,2,&output,&bytes,&length,&e),&e,"receipt_invalid","bounded read");
    memset(&e,0,sizeof e); rejected(patch_file_write_new(&g,f,"foreign",7,&e),&e,NULL,"create collision");
    check(content(&g,f,"original"),"collision preserves original");
    join(source,root,L"source.bin"); check(write_text(&g,source,"replacement"),"source created"); src=record(&g,source);
    join(copy,root,L"copy.bin"); check(patch_file_copy_new(&g,source,copy,&src,&e),"copy_new expected record");
    actual=record(&g,copy); check(patch_record_equal(&actual,&src)&&strcmp(actual.identity,src.identity),"copy content equal with distinct identity");
    join(other,root,L"bad-copy"); memset(&e,0,sizeof e);
    rejected(patch_file_copy_new(&g,source,other,&before,&e),&e,"file_changed","copy changed expected");
    memset(&e,0,sizeof e); rejected(patch_file_replace(&g,source,f,&src,&src,&e),&e,"file_changed","replace changed expected");
    check(content(&g,f,"original"),"changed expected preserved");
    check(patch_file_replace(&g,source,f,&before,&src,&e)&&content(&g,f,"replacement"),"safe replacement");
    join(other,root,L"absent.bin"); check(patch_file_replace(&g,source,other,&absent,&src,&e),"replace absent expected");
    memset(&e,0,sizeof e); rejected(patch_file_remove(&g,f,&before,&e),&e,"file_changed","remove wrong expected");
    actual=record(&g,f); check(patch_file_remove(&g,f,&actual,&e)&&!record(&g,f).exists,"remove expected");
    check(patch_file_remove(&g,f,&absent,&e),"remove absent expected");
    SetFileAttributesW(copy,FILE_ATTRIBUTE_READONLY); actual=record(&g,copy); memset(&e,0,sizeof e);
    rejected(patch_file_remove(&g,copy,&actual,&e),&e,NULL,"readonly removal refused");
    check(content(&g,copy,"replacement"),"readonly preserved"); SetFileAttributesW(copy,FILE_ATTRIBUTE_NORMAL);
    h=CreateFileW(source,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL); check(h!=INVALID_HANDLE_VALUE,"lock fixture");
    memset(&e,0,sizeof e); rejected(patch_file_copy_new(&g,source,f,&src,&e),&e,NULL,"locked source refused"); CloseHandle(h);
    join(a,root,L"hard.bin"); check(CreateHardLinkW(a,source,NULL),"hardlink fixture"); memset(&e,0,sizeof e);
    rejected(patch_file_record(&g,source,&actual,&e),&e,"unsafe_path","hardlink source rejected"); memset(&e,0,sizeof e);
    rejected(patch_file_remove(&g,a,&src,&e),&e,"unsafe_path","hardlink target rejected"); DeleteFileW(a);
    memset(&e,0,sizeof e); rejected(patch_file_record(&g,evidence,&actual,&e),&e,"unsafe_path","root escape");
    memset(&e,0,sizeof e); rejected(open_guard(&bad,L"G:\\",&e),&e,"unsafe_path","drive root rejected"); patch_guard_close(&bad);
    join(a,root,L"publish"); join(b,root,L"published"); patch_guard_directory(&g,a,1,&e); join(c,a,L"child"); write_text(&g,c,"x");
    check(patch_guard_rename_directory(&g,a,b,&e),"directory publication"); join(c,b,L"child"); check(content(&g,c,"x"),"published child retained");
    { int removed=1; check(patch_guard_remove_empty(&g,b,&removed,&e)&&!removed,"nonempty directory preserved");
      actual=record(&g,c); patch_file_remove(&g,c,&actual,&e); check(patch_guard_remove_empty(&g,b,&removed,&e)&&removed,"empty directory removed"); }
    join(a,root,L"from-dir");join(b,root,L"collision-dir");patch_guard_directory(&g,a,1,&e);patch_guard_directory(&g,b,1,&e);memset(&e,0,sizeof e);
    rejected(patch_guard_rename_directory(&g,a,b,&e),&e,NULL,"directory rename collision");
    join(a,root,L"release");join(b,a,L"child");patch_guard_directory(&g,b,1,&e);
    memset(&e,0,sizeof e);rejected(patch_guard_release(&g,root,0,&e),&e,"unsafe_path","release root refused");
    memset(&e,0,sizeof e);rejected(patch_guard_release(&g,root,1,&e),&e,"unsafe_path","release_under root refused");
    memset(&e,0,sizeof e);rejected(patch_guard_release(&g,a,0,&e),&e,"unsafe_path","release with pinned child refused");
    check(patch_guard_release(&g,b,0,&e)&&RemoveDirectoryW(b),"released child can be removed");
    check(patch_guard_release(&g,a,0,&e)&&RemoveDirectoryW(a),"released parent can be removed");
    patch_guard_directory(&g,b,1,&e);check(patch_guard_release(&g,a,1,&e)&&RemoveDirectoryW(b)&&RemoveDirectoryW(a),"release_under releases subtree");
    swprintf(a,32768,L"%ls-moved",root);check(!MoveFileExW(root,a,0),"release_under retains root pin");
    join(a,root,L"directory-file");patch_guard_directory(&g,a,1,&e);memset(&e,0,sizeof e);
    rejected(patch_guard_file(&g,a,&e),&e,"unsafe_path","directory cannot be regular file");
    join(a,root,L"absent-parent/file");memset(&e,0,sizeof e);rejected(patch_guard_file(&g,a,&e),&e,NULL,"missing parent rejected by file");
    join(a,root,L"missing-leaf");check(patch_guard_file(&g,a,&e),"file allows absent leaf");
    join(a,source,L"child");memset(&e,0,sizeof e);rejected(patch_guard_directory(&g,a,1,&e),&e,NULL,"file cannot be traversed as directory");
    check(open_guard(&ext,evidence,&e),"evidence guard");join(a,evidence,L"junction");memset(&e,0,sizeof e);
    rejected(open_guard(&bad,a,&e),&e,"unsafe_path","junction root rejected");patch_guard_close(&bad);
    join(b,a,L"nested");memset(&e,0,sizeof e);rejected(open_guard(&bad,b,&e),&e,"unsafe_path","junction ancestor rejected");patch_guard_close(&bad);
    join(b,a,L"new");memset(&e,0,sizeof e);rejected(patch_guard_directory(&ext,b,1,&e),&e,"unsafe_path","junction traversal creation rejected");
    join(b,a,L"outside.bin");memset(&e,0,sizeof e);rejected(patch_file_read(&ext,b,100,&output,&bytes,&length,&e),&e,"unsafe_path","junction file read rejected");
    join(b,evidence,L"outside/outside.bin");check(content(&ext,b,"untouched"),"junction target unchanged");patch_guard_close(&ext);
    join(a,root,L"same-bytes");write_text(&g,a,"original");actual=record(&g,a);join(b,root,L"same-bytes-old");
    check(MoveFileExW(a,b,0)&&raw_write(a,"original"),"same bytes substituted with distinct object");memset(&e,0,sizeof e);
    rejected(patch_file_remove(&g,a,&actual,&e),&e,"file_changed","same bytes identity substitution rejected");check(content(&g,a,"original"),"substituted file preserved");
    join(c,root,L"substituted-source-copy");memset(&e,0,sizeof e);
    rejected(patch_file_copy_new(&g,a,c,&actual,&e),&e,"file_changed","same bytes copy source substitution rejected");
    join(a,root,L"source-bound-target");write_text(&g,a,"original");actual=record(&g,a);expected=bytes_record("different");memset(&e,0,sizeof e);
    rejected(patch_file_replace(&g,source,a,&actual,&expected,&e),&e,"file_changed","replacement binds expected source");
    check(content(&g,a,"original"),"changed source cannot alter destination");check(patch_file_replace(&g,source,a,&actual,&src,&e),"matching source can publish");
    join(a,root,L"pinned-game.exe");write_text(&g,a,"game");h=INVALID_HANDLE_VALUE;
    check(patch_file_pin(&g,a,&h,&actual,&e)&&h!=INVALID_HANDLE_VALUE,"supported executable pin acquired");join(b,root,L"pinned-game-renamed");
    check(!MoveFileExW(a,b,0)&&!DeleteFileW(a),"supported executable pin denies rename and delete");CloseHandle(h);
    check(MoveFileExW(a,b,0)&&MoveFileExW(b,a,0),"supported executable pin released explicitly");
    join(a,root,L"swap-empty");patch_guard_directory(&g,a,1,&e);wcscpy(swap_target,a);join(swap_saved,root,L"swap-empty-old");open_swap=1;memset(&e,0,sizeof e);
    { int removed=0;rejected(patch_guard_remove_empty(&g,a,&removed,&e),&e,"file_changed","empty directory substitution refused"); }
    check(GetFileAttributesW(a)!=INVALID_FILE_ATTRIBUTES,"substituted directory survives");
    join(a,root,L"swap-rename");patch_guard_directory(&g,a,1,&e);wcscpy(swap_target,a);join(swap_saved,root,L"swap-rename-old");join(b,root,L"swap-rename-dest");open_swap=1;memset(&e,0,sizeof e);
    rejected(patch_guard_rename_directory(&g,a,b,&e),&e,"file_changed","directory rename substitution refused");
    check(GetFileAttributesW(a)!=INVALID_FILE_ATTRIBUTES&&GetFileAttributesW(b)==INVALID_FILE_ATTRIBUTES,"substituted rename target survives");
    info_fault=1;memset(&e,0,sizeof e);rejected(patch_file_record(&g,source,&actual,&e),&e,"platform_unsupported","missing stable identity refuses");info_fault=0;
    patch_context_close(&output);patch_guard_close(&g);
}
static void test_failure_mutations(const wchar_t *root)
{
    PatchGuard g={0};PatchError e={0};PatchFileRecord before,src,actual;wchar_t source[32768],dest[32768],a[32768];unsigned i;
    open_guard(&g,root,&e);join(source,root,L"fault-source");write_text(&g,source,"replacement");src=record(&g,source);
    for(i=1;i<=2;++i){swprintf(dest,32768,L"%ls\\write-failure-%u",root,i);write_fault=(int)i;memset(&e,0,sizeof e);
        rejected(patch_file_write_new(&g,dest,"content",7,&e),&e,"io_error","stage write failure reported");write_fault=0;check(!record(&g,dest).exists,"failed write never publishes");}
    join(dest,root,L"short-write");write_fault=3;check(write_text(&g,dest,"complete-short-writes"),"short writes completed");write_fault=0;check(content(&g,dest,"complete-short-writes"),"short write content correct");
    join(dest,root,L"flush-failure");flush_fault=1;memset(&e,0,sizeof e);rejected(patch_file_write_new(&g,dest,"x",1,&e),&e,"io_error","stage flush failure reported");flush_fault=0;check(!record(&g,dest).exists,"failed stage flush never publishes");
    join(dest,root,L"published-flush-failure");write_text(&g,dest,"original");before=record(&g,dest);flush_at=2;flush_count=0;memset(&e,0,sizeof e);
    rejected(patch_file_replace(&g,source,dest,&before,&src,&e),&e,"recovery_required","published flush failure requires recovery");flush_at=0;
    check(content(&g,dest,"replacement")&&g.recovery_path&&content(&g,g.recovery_path,"original"),"failed published flush retains original and published bytes");
    for(i=5;i<=7;++i){swprintf(dest,32768,L"%ls\\rename-failure-%u",root,i);write_text(&g,dest,"original");before=record(&g,dest);mode=(int)i;memset(&e,0,sizeof e);
        rejected(patch_file_replace(&g,source,dest,&before,&src,&e),&e,i==7?"recovery_required":"io_error","replacement OS failure captured");
        check(content(&g,dest,i==7?"replacement":"original"),"replacement failure preserves usable content");mode=0;}
    join(dest,root,L"delete-failure");write_text(&g,dest,"original");before=record(&g,dest);mode=7;memset(&e,0,sizeof e);
    rejected(patch_file_remove(&g,dest,&before,&e),&e,"io_error","delete failure reported");check(content(&g,dest,"original"),"delete failure preserves target");
    join(dest,root,L"race-destination");write_text(&g,dest,"original");before=record(&g,dest);wcscpy(fault_destination,dest);mode=1;memset(&e,0,sizeof e);
    rejected(patch_file_replace(&g,source,dest,&before,&src,&e),&e,"recovery_required","rename gap foreign collision requires recovery");check(content(&g,dest,"foreign"),"rename gap foreign preserved");
    check(remnants(&g,root,L".mtw-before-*","original")>=2,"rename gap original retained in quarantine");
    join(dest,root,L"unsupported-mutation");write_text(&g,dest,"original");before=record(&g,dest);g.platform.set_info=NULL;g.platform.nt_set_info=NULL;memset(&e,0,sizeof e);
    rejected(patch_file_replace(&g,source,dest,&before,&src,&e),&e,"platform_unsupported","missing handle mutation API refuses");check(content(&g,dest,"original"),"unsupported mutation preserves target");
    join(a,root,L"unsupported-new");memset(&e,0,sizeof e);rejected(patch_file_write_new(&g,a,"x",1,&e),&e,"platform_unsupported","unsupported publish refuses before stage");check(!record(&g,a).exists,"unsupported publish creates no destination");
    actual=record(&g,dest);memset(&e,0,sizeof e);rejected(patch_file_remove(&g,dest,&actual,&e),&e,"platform_unsupported","unsupported deletion refuses");
    patch_guard_close(&g);
}
static void test_replace_allocations(const wchar_t *root)
{
    unsigned i,count=0;
    for(i=0;i<=count;++i){
        PatchGuard g={0};PatchError e={0};Allocator a={0};PatchFileRecord before,src;unsigned start,live;int ok;
        wchar_t source[32768],dest[32768];
        g.context.allocator.user=&a;g.context.allocator.allocate=allocate;g.context.allocator.deallocate=deallocate;
        open_guard(&g,root,&e);join(source,root,L"fault-source");src=record(&g,source);
        swprintf(dest,32768,L"%ls\\oom-replace-%u",root,i);write_text(&g,dest,"original");before=record(&g,dest);
        start=a.calls;live=a.live;if(i)a.fail_at=start+i;
        ok=patch_file_replace(&g,source,dest,&before,&src,&e);if(!i)count=a.calls-start;
        check(ok==(i==0),"replacement allocation failure result");a.fail_at=0;
        check(content(&g,dest,i?"original":"replacement"),"replacement allocation failure preserves usable original");
        check(a.live==live,"replacement allocation failure frees temporary ownership");
        patch_guard_close(&g);check(a.live==0,"replacement allocation failure closes guard ownership");
    }
}
static int child_fault(const wchar_t *root, int which)
{
    PatchGuard g={0};PatchError e={0};PatchFileRecord before,src;wchar_t source[32768];
    if(!open_guard(&g,root,&e))return 90;
    join(fault_destination,root,which==8?L"crash-destination":L"journal.json");before=record(&g,fault_destination);
    mode=which==8?2:which;
    if(which==8){join(source,root,L"crash-source");src=record(&g,source);patch_file_replace(&g,source,fault_destination,&before,&src,&e);}
    else patch_file_write_atomic_owned(&g,fault_destination,"{\"revision\":2}",14,&before,&e);
    patch_guard_close(&g);return 78;
}
static void test_owned(const wchar_t *root)
{
    PatchGuard g={0};PatchError e={0};PatchFileRecord before,actual,absent={0};wchar_t journal[32768],previous[32768],a[32768],b[32768],exe[32768];int which;
    open_guard(&g,root,&e);join(journal,root,L"journal.json");swprintf(previous,32768,L"%ls.mtw-previous",journal);
    check(patch_file_write_atomic_owned(&g,journal,"{\"revision\":1}",14,&absent,&e)&&content(&g,journal,"{\"revision\":1}"),"owned journal initial publication");before=record(&g,journal);
    check(patch_file_write_atomic_owned(&g,journal,"{\"revision\":2}",14,&before,&e)&&content(&g,journal,"{\"revision\":2}")&&!record(&g,previous).exists,"owned update retires backup");
    memset(&e,0,sizeof e);rejected(patch_file_write_atomic_owned(&g,journal,"bad",3,&before,&e),&e,"file_changed","owned expected mismatch");write_text(&g,previous,"foreign");actual=record(&g,journal);memset(&e,0,sizeof e);
    rejected(patch_file_write_atomic_owned(&g,journal,"bad",3,&actual,&e),&e,"recovery_required","owned foreign backup preserved");check(content(&g,previous,"foreign"),"foreign backup unchanged");before=record(&g,previous);patch_file_remove(&g,previous,&before,&e);
    patch_file_copy_new(&g,journal,previous,&actual,&e);check(patch_file_write_atomic_owned(&g,journal,"{\"revision\":3}",14,&actual,&e)&&content(&g,journal,"{\"revision\":3}"),"owned matching previous slot reused");
    join(a,root,L"raced-journal.json");write_text(&g,a,"{\"revision\":1}");actual=record(&g,a);wcscpy(fault_destination,a);mode=1;memset(&e,0,sizeof e);
    rejected(patch_file_write_atomic_owned(&g,a,"{\"revision\":2}",14,&actual,&e),&e,"recovery_required","owned foreign publication race");swprintf(b,32768,L"%ls.mtw-previous",a);
    check(content(&g,a,"foreign")&&content(&g,b,"{\"revision\":1}"),"owned race preserves foreign and previous");
    GetModuleFileNameW(NULL,exe,32768);
    for(which=2;which<=8;++which){wchar_t command[32768];STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};DWORD exitcode=0;int launched;
        if(which>4&&which<8)continue;
        swprintf(a,32768,L"%ls\\owned-fault-%d",root,which);patch_guard_directory(&g,a,1,&e);
        join(journal,a,which==8?L"crash-destination":L"journal.json");write_text(&g,journal,which==8?"original":"{\"revision\":1}");
        if(which==8){join(b,a,L"crash-source");write_text(&g,b,"replacement");}
        swprintf(command,32768,L"\"%ls\" --fault \"%ls\" %d %ls",exe,a,which,legacy?L"legacy":L"modern");si.cb=sizeof si;si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
        launched=CreateProcessW(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi);check(launched,"interruption child launched");
        if(launched){check(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0,"interruption child terminated");GetExitCodeProcess(pi.hProcess,&exitcode);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);check(exitcode==(which==3?79u:which==4?80u:77u),"interruption boundary reached");}
        if(which==8){check(!record(&g,journal).exists,"interruption leaves destination absent");check(remnants(&g,a,L".mtw-*","original")==1&&remnants(&g,a,L".mtw-*","replacement")==1,"interruption retains original and stage");continue;}
        swprintf(previous,32768,L"%ls.mtw-previous",journal);check(content(&g,previous,"{\"revision\":1}"),"interruption previous retained");
        if(which==2){check(!record(&g,journal).exists,"owned gap primary absent");before=record(&g,previous);patch_file_copy_new(&g,previous,journal,&before,&e);actual=record(&g,journal);check(patch_file_write_atomic_owned(&g,journal,"{\"revision\":2}",14,&actual,&e)&&content(&g,journal,"{\"revision\":2}"),"owned gap recovered and update resumed");}
        else check(content(&g,journal,which==3?"{\"revision\":2}":"{\"revision\":1}"),"owned primary old or new remains");
    }
    patch_guard_close(&g);
}
static void test_previous_substitution(const wchar_t *root, int action)
{
    PatchGuard g={0};PatchError e={0};PatchFileRecord before,after;wchar_t journal[32768];int ok;
    check(open_guard(&g,root,&e),"previous substitution guard opened");
    join(journal,root,L"journal.json");check(write_text(&g,journal,"{\"revision\":1}"),"previous substitution original created");
    before=record(&g,journal);swprintf(previous_slot,32768,L"%ls.mtw-previous",journal);join(previous_saved,root,L"retained-original-copy");
    previous_swap=action;previous_published=0;previous_swapped=0;
    ok=patch_file_write_atomic_owned(&g,journal,"{\"revision\":2}",14,&before,&e);
    check(previous_swapped,"previous slot substituted after copy handle closed");
    rejected(ok,&e,"recovery_required","changed fresh previous slot refuses primary publication");
    after=record(&g,journal);check(patch_record_matches(&after,&before),"fresh previous substitution leaves original primary unchanged");
    check(action==1?content(&g,previous_slot,"foreign"):!record(&g,previous_slot).exists,
          "fresh previous substitution preserves foreign or missing slot");
    check(content(&g,previous_saved,"{\"revision\":1}"),"substitution retains verified original copy");
    previous_swap=0;patch_guard_close(&g);
}
int wmain(int argc, wchar_t **argv)
{
    if(argc==2&&!wcscmp(argv[1],L"--wait")){Sleep(60000);return 0;}
    if(argc>=4)legacy=!wcscmp(argv[argc-1],L"legacy");
    if(argc==5&&!wcscmp(argv[1],L"--fault"))return child_fault(argv[2],_wtoi(argv[3]));
    if(argc==3&&!wcscmp(argv[1],L"--key")){
        PatchContext c={0};PatchError e={0};wchar_t *path=NULL,*lower=NULL;char *u=NULL,digest[65];size_t n,i;int ok;
        ok=patch_path_normalize(&c,argv[2],&path,&e)&&patch_invariant_lower(&c,path,&lower,&e)&&
           patch_wide_to_utf8(&c,lower,wcslen(lower),&u,&n,&e)&&patch_sha256_bytes(&c,u,n,digest);
        if(ok){for(i=0;i<64;++i)if(digest[i]>='A'&&digest[i]<='F')digest[i]+='a'-'A';printf("UnofficialMedievalPatch-%.32s\n",digest);}
        patch_context_close(&c);return ok?0:1;
    }
    if(argc==4&&!wcscmp(argv[1],L"--identity")){
        PatchGuard g={0};PatchError e={0};PatchContext c={0};char key[65],*u=NULL;size_t n;int ok;
        ok=open_guard(&g,argv[2],&e)&&patch_guard_key(&g,key,&e)&&patch_wide_to_utf8(&c,g.canonical,wcslen(g.canonical),&u,&n,&e);
        if(ok)printf("%s\n%s\n%s\n",g.identity,key,u);else printf("%s\n",e.code?e.code:"error");patch_context_close(&c);patch_guard_close(&g);return ok?0:1;
    }
    if(argc==5&&!wcscmp(argv[1],L"--previous-substitute")){
        test_previous_substitution(argv[2],!wcscmp(argv[3],L"foreign")?1:2);
    }else if(argc==4&&!wcscmp(argv[1],L"--links")){
        PatchGuard g={0},bad={0};PatchError e={0};PatchFileRecord record,expected=bytes_record("untouched");wchar_t path[32768];
        if(!open_guard(&g,argv[2],&e))return 90;
        join(path,argv[2],L"file-link");rejected(patch_file_record(&g,path,&record,&e),&e,"unsafe_path","file symlink record refused");
        memset(&e,0,sizeof e);rejected(patch_file_remove(&g,path,&expected,&e),&e,"unsafe_path","file symlink removal refused");
        join(path,argv[2],L"directory-link");memset(&e,0,sizeof e);rejected(patch_guard_directory(&g,path,0,&e),&e,"unsafe_path","reparse directory rejected");
        memset(&e,0,sizeof e);rejected(open_guard(&bad,path,&e),&e,"unsafe_path","symlink root refused");patch_guard_close(&bad);patch_guard_close(&g);
    }else if(argc==6&&!wcscmp(argv[1],L"--process")){
        PatchPlatform p;PatchError e={0};int matches=0;DWORD pid=(DWORD)wcstoul(argv[2],NULL,10);platform(&p);
        check(patch_process_is_target(&p,pid,argv[3],&matches,&e)&&matches,"target process detected");
        check(patch_process_is_target(&p,pid,argv[4],&matches,&e)&&!matches,"same named different game permitted");
        memset(&e,0,sizeof e);check(!patch_process_is_target(&p,0,argv[3],&matches,&e)&&matches,"inaccessible process identity conservative");
        p.query_image=NULL;p.module_image=NULL;memset(&e,0,sizeof e);check(!patch_process_is_target(&p,pid,argv[3],&matches,&e)&&matches,"missing process capability conservative");
        p.query_image=legacy?NULL:image_query;p.module_image=module_query;
        swprintf(supplied_process_image,32768,L"\\\\?\\%ls",argv[3]);memset(&e,0,sizeof e);
        check(patch_process_is_target(&p,pid,argv[3],&matches,&e)&&matches,"extended process image spelling resolves to same target");
        wcscpy(supplied_process_image,L"G:\\missing-process-image-fixture\\Medieval_TW.exe");memset(&e,0,sizeof e);
        check(!patch_process_is_target(&p,pid,argv[3],&matches,&e)&&matches,"unresolvable process image is conservative");
    }else if(argc==5&&!wcscmp(argv[1],L"--suite")){
        check(CreateDirectoryW(argv[2],NULL),"fixture root created");test_strings();test_pin_failures(argv[2]);test_files(argv[2],argv[3]);test_failure_mutations(argv[2]);test_replace_allocations(argv[2]);test_owned(argv[2]);
    }else return 2;
    printf("RESULT passed=%u failed=%u\n",passed,failed);return failed?1:0;
}
