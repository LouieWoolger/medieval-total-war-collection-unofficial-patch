#ifndef MTW_PATCH_FILES_H
#define MTW_PATCH_FILES_H

/* C99 handle-bound filesystem layer. The C++ native_files.h contracts are kept,
   with transient file IDs strengthening expectations obtained in this process.
   No cleanup routine recursively deletes, follows a link, or unlinks by path. */
#include "patch_platform.h"
#include "patch_identity.h"
#include <stdio.h>

typedef struct {
    int exists;
    char sha256[65];
    uint64_t length;
    /* Transient only: never add this field to the v2 receipt wire format. */
    char identity[26];
} PatchFileRecord;
typedef struct PatchPin {
    struct PatchPin *next;
    wchar_t *path;
    HANDLE handle;
    char identity[26];
} PatchPin;
typedef struct {
    PatchContext context;
    PatchPlatform platform;
    PatchPin *pins;
    wchar_t *root, *canonical;
    char identity[26];
    /* Borrowed until close/next successful replacement. Retained original path
       on recovery_required; the file itself is never deleted by guard_close. */
    wchar_t *recovery_path;
} PatchGuard;
typedef struct { HANDLE handle; wchar_t *path; int published, deleted; } PatchStage;

static inline int patch_record_equal(const PatchFileRecord *a, const PatchFileRecord *b)
{
    return a&&b&&a->exists==b->exists&&(!a->exists||(a->length==b->length&&!strcmp(a->sha256,b->sha256)));
}
/* Use content equality for verified copies/archives/backup slots. Use matches
   for an actual source or mutation target; an empty expected ID means an older
   on-disk record whose content still must match a newly pinned object. */
static inline int patch_record_matches(const PatchFileRecord *actual, const PatchFileRecord *expected)
{
    return patch_record_equal(actual,expected)&&(!expected->exists||!expected->identity[0]||!strcmp(actual->identity,expected->identity));
}
static inline int patch_file_missing(DWORD e) { return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND; }
static inline int patch_file_fail(PatchError *e, const char *message)
{
    DWORD code=GetLastError();patch_error_set(e,"io_error",message,code);return 0;
}
static inline void patch_local_context(PatchContext *local, const PatchGuard *guard)
{
    memset(local,0,sizeof(*local));local->allocator=guard->context.allocator;local->memory_limit=guard->context.memory_limit;
}
static inline int patch_native_path(PatchContext *c, const wchar_t *path, wchar_t **out, PatchError *e)
{
    size_t n=wcslen(path);void *memory=NULL;*out=NULL;
    if(!patch_alloc(c,n+5,sizeof(wchar_t),&memory,e))return 0;
    *out=(wchar_t *)memory;wcscpy(*out,L"\\\\?\\");wcscpy(*out+4,path);return 1;
}
static inline int patch_parent_path(PatchContext *c, const wchar_t *path, wchar_t **out, PatchError *e)
{
    wchar_t *p;
    if(!patch_wide_copy(c,path,out,e))return 0;
    p=wcsrchr(*out,L'\\');if(!p){patch_error_set(e,"unsafe_path","Parent path is unavailable.",ERROR_INVALID_NAME);return 0;}
    if(p==*out+2)p[1]=0;else *p=0;return 1;
}
static inline void patch_file_identity(const BY_HANDLE_FILE_INFORMATION *info, char out[26])
{
    snprintf(out,26,"%08lX:%08lX%08lX",(unsigned long)info->dwVolumeSerialNumber,
             (unsigned long)info->nFileIndexHigh,(unsigned long)info->nFileIndexLow);
}
static inline int patch_handle_info(HANDLE h, int directory, BY_HANDLE_FILE_INFORMATION *info, PatchError *e)
{
    if(!GetFileInformationByHandle(h,info))return patch_file_fail(e,"Could not inspect the pinned filesystem object.");
    if((info->dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!!(info->dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=!!directory||
       (!directory&&info->nNumberOfLinks!=1)){
        patch_error_set(e,"unsafe_path","A linked or unexpected filesystem object was refused.",ERROR_INVALID_DATA);return 0;
    }
    if(!info->nFileIndexHigh&&!info->nFileIndexLow){patch_error_set(e,"platform_unsupported","Stable filesystem identity is unavailable.",ERROR_NOT_SUPPORTED);return 0;}
    return 1;
}
static inline int patch_final_path(PatchGuard *g, HANDLE h, const wchar_t *path, PatchContext *c, wchar_t **output, PatchError *e)
{
    wchar_t *native=NULL,*buffer=NULL,*resolved=NULL;void *memory=NULL;DWORD n;int equal=0;
    if(output)*output=NULL;
    if(!patch_alloc(c,32768,sizeof(wchar_t),&memory,e))return 0;
    buffer=(wchar_t *)memory;
    if(g->platform.final_path){n=g->platform.final_path(h,buffer,32768,0);if(!n||n>=32768)return patch_file_fail(e,"Could not resolve the pinned final path.");}
    else {
        wchar_t drive[4]={path[0],L':',L'\\',0},device[1024],name[3]={path[0],L':',0};
        UINT type=GetDriveTypeW(drive);
        /* Mapped network and SUBST drives cannot provide the same DOS final-path
           binding on the compatibility route. Refuse them before any mutation. */
        if((type!=DRIVE_FIXED&&type!=DRIVE_REMOVABLE&&type!=DRIVE_RAMDISK)||!QueryDosDeviceW(name,device,1024)||wcsncmp(device,L"\\Device\\",8)){
            patch_error_set(e,"unsafe_path","An unaliased local drive is required.",ERROR_NOT_SUPPORTED);return 0;
        }
        if(!patch_native_path(c,path,&native,e))return 0;
        n=GetFullPathNameW(native,32768,buffer,NULL);if(!n||n>=32768)return patch_file_fail(e,"Could not resolve the local path.");
        if(!patch_wide_copy(c,buffer,&resolved,e))return 0;
        n=GetLongPathNameW(resolved,buffer,32768);if(!n||n>=32768)return patch_file_fail(e,"Could not establish the long path.");
    }
    if(wcsncmp(buffer,L"\\\\?\\",4)||!patch_path_normalize(c,buffer+4,&resolved,e)){
        patch_error_set(e,"unsafe_path","Nonlocal final path refused.",ERROR_INVALID_NAME);return 0;
    }
    if(!patch_path_equal(&g->platform,resolved,path,&equal,e))return 0;
    if(!equal){patch_error_set(e,"unsafe_path","Final path mismatch or short-path alias refused.",ERROR_INVALID_NAME);return 0;}
    if(output)*output=resolved;
    return 1;
}
static inline int patch_contained(PatchGuard *g, PatchContext *c, const wchar_t *path, int allow_root, wchar_t **out, PatchError *e)
{
    int equal=0,under=0;
    if(!g||!g->root){patch_error_set(e,"invalid_argument","An open guard is required.",ERROR_INVALID_HANDLE);return 0;}
    if(!patch_path_normalize(c,path,out,e)||!patch_path_equal(&g->platform,*out,g->root,&equal,e)||
       !patch_path_under(&g->platform,*out,g->root,&under,e))return 0;
    if((allow_root&&equal)||under)return 1;
    patch_error_set(e,"unsafe_path","Path escapes the guarded installation.",ERROR_INVALID_NAME);return 0;
}
static inline int patch_mutation_capable(PatchGuard *g, PatchError *e)
{
    if(g->platform.set_info||(g->platform.nt_set_info&&g->platform.nt_error))return 1;
    patch_error_set(e,"platform_unsupported","Handle-bound rename and deletion are unavailable.",ERROR_CALL_NOT_IMPLEMENTED);return 0;
}
static inline void patch_guard_close(PatchGuard *g)
{
    PatchPin *p;DWORD saved=GetLastError();
    if(!g)return;
    for(p=g->pins;p;p=p->next)if(p->handle&&p->handle!=INVALID_HANDLE_VALUE)CloseHandle(p->handle);
    patch_context_close(&g->context);g->pins=NULL;g->root=NULL;g->canonical=NULL;g->recovery_path=NULL;g->identity[0]=0;SetLastError(saved);
}
static inline int patch_pin_directory(PatchGuard *g, const wchar_t *path, int create, int absent, int *found, PatchError *e)
{
    PatchContext c;wchar_t *native=NULL,*copy=NULL;HANDLE h=INVALID_HANDLE_VALUE;BY_HANDLE_FILE_INFORMATION info;
    PatchPin *p,*pin=NULL;char identity[26];void *memory=NULL;int ok=0,equal;DWORD native_error;
    patch_local_context(&c,g);*found=0;
    if(!patch_native_path(&c,path,&native,e))goto done;
    h=CreateFileW(native,FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(h==INVALID_HANDLE_VALUE){native_error=GetLastError();
        if(patch_file_missing(native_error)&&create){
            if(!patch_mutation_capable(g,e))goto done;
            if(!CreateDirectoryW(native,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){patch_file_fail(e,"Could not create directory.");goto done;}
            h=CreateFileW(native,FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
            if(h==INVALID_HANDLE_VALUE){patch_file_fail(e,"Could not pin created directory.");goto done;}
        }else if(patch_file_missing(native_error)&&absent){ok=1;goto done;}
        else {patch_error_set(e,"io_error","Could not pin directory.",native_error);goto done;}
    }
    if(!patch_handle_info(h,1,&info,e)||!patch_final_path(g,h,path,&c,NULL,e))goto done;
    patch_file_identity(&info,identity);
    for(p=g->pins;p;p=p->next){
        if(!patch_path_equal(&g->platform,p->path,path,&equal,e))goto done;
        if(equal){if(strcmp(p->identity,identity)){patch_error_set(e,"unsafe_path","Pinned directory identity changed.",ERROR_INVALID_DATA);goto done;}
            ok=1;*found=1;goto done;}
    }
    if(!patch_wide_copy(&g->context,path,&copy,e)||!patch_alloc(&g->context,1,sizeof(PatchPin),&memory,e))goto done;
    pin=(PatchPin *)memory;pin->path=copy;copy=NULL;pin->handle=h;h=INVALID_HANDLE_VALUE;memcpy(pin->identity,identity,sizeof identity);
    pin->next=g->pins;g->pins=pin;ok=1;*found=1;
done:
    if(copy)patch_context_free(&g->context,copy);
    if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    patch_context_close(&c);return ok;
}
static inline int patch_pin_chain(PatchGuard *g, const wchar_t *path, int create, int absent, int *found, PatchError *e)
{
    PatchContext c;wchar_t *part=NULL;size_t i,n;int ok=0;patch_local_context(&c,g);*found=0;
    if(!patch_wide_copy(&c,path,&part,e))goto done;
    n=wcslen(part);part[3]=0;
    ok=patch_pin_directory(g,part,0,absent,found,e);
    if(!ok||!*found)goto done;
    wcscpy(part,path);
    for(i=3;i<=n;++i)if(i==n||part[i]==L'\\'){wchar_t saved=part[i];part[i]=0;
        if(i>3){ok=patch_pin_directory(g,part,create,absent,found,e);if(!ok||!*found)goto done;}
        part[i]=saved;
    }
    ok=1;
done:
    patch_context_close(&c);return ok;
}
static inline int patch_guard_open_internal(PatchGuard *g, const wchar_t *path, int allow_drive, PatchError *e)
{
    int found=0,equal;PatchPin *p;PatchContext local;wchar_t *canonical=NULL;
    if(!g){patch_error_set(e,"invalid_argument","A guard is required.",ERROR_INVALID_PARAMETER);return 0;}
    if(g->root||g->pins||g->context.resources){patch_error_set(e,"invalid_argument","Guard is already open.",ERROR_INVALID_PARAMETER);return 0;}
    if(!g->platform.initialized)patch_platform_init(&g->platform);
    if(!patch_path_normalize(&g->context,path,&g->root,e))goto failed;
    if(wcslen(g->root)==3&&!allow_drive){patch_error_set(e,"unsafe_path","Drive root cannot be an installation root.",ERROR_INVALID_NAME);goto failed;}
    if(!patch_pin_chain(g,g->root,0,0,&found,e)||!found)goto failed;
    patch_local_context(&local,g);
    for(p=g->pins;p;p=p->next){
        if(!patch_path_equal(&g->platform,p->path,g->root,&equal,e)){patch_context_close(&local);goto failed;}
        if(equal){
            if(!patch_final_path(g,p->handle,g->root,&local,&canonical,e)||!patch_wide_copy(&g->context,canonical,&g->canonical,e)){patch_context_close(&local);goto failed;}
            memcpy(g->identity,p->identity,sizeof g->identity);break;
        }
    }
    patch_context_close(&local);return 1;
failed:patch_guard_close(g);return 0;
}
static inline int patch_guard_open(PatchGuard *g, const wchar_t *path, PatchError *e)
{
    return patch_guard_open_internal(g,path,0,e);
}
static inline int patch_guard_key(PatchGuard *g, char output[65], PatchError *e)
{
    PatchContext c;wchar_t *lower=NULL;char *utf8=NULL;size_t n;int ok=0;patch_local_context(&c,g);output[0]=0;
    if(!g->canonical){patch_error_set(e,"invalid_argument","An open guard is required.",ERROR_INVALID_HANDLE);goto done;}
    if(!patch_invariant_lower(&c,g->canonical,&lower,e)||!patch_wide_to_utf8(&c,lower,wcslen(lower),&utf8,&n,e))goto done;
    if(!patch_sha256_bytes(&c,utf8,n,output)){patch_error_set(e,c.error.code,c.error.message,c.error.win32);goto done;}
    ok=1;
done:patch_context_close(&c);return ok;
}
static inline int patch_guard_directory(PatchGuard *g, const wchar_t *path, int create, PatchError *e)
{
    PatchContext c;wchar_t *p=NULL;int found=0,ok=0;patch_local_context(&c,g);
    if(!patch_contained(g,&c,path,1,&p,e))goto done;
    ok=patch_pin_chain(g,p,create,0,&found,e)&&found;
done:patch_context_close(&c);return ok;
}
static inline int patch_guard_release(PatchGuard *g, const wchar_t *path, int subtree, PatchError *e)
{
    PatchContext c;wchar_t *p=NULL;PatchPin *pin,**link;int under,equal,ok=0;patch_local_context(&c,g);
    if(!patch_contained(g,&c,path,0,&p,e))goto done;
    if(!subtree)for(pin=g->pins;pin;pin=pin->next){
        if(!patch_path_under(&g->platform,pin->path,p,&under,e))goto done;
        if(under){patch_error_set(e,"unsafe_path","Release child pins before their parent.",ERROR_INVALID_PARAMETER);goto done;}
    }
    for(link=&g->pins;*link;){pin=*link;
        if(!patch_path_equal(&g->platform,pin->path,p,&equal,e)||!patch_path_under(&g->platform,pin->path,p,&under,e))goto done;
        if(equal||(subtree&&under)){*link=pin->next;CloseHandle(pin->handle);patch_context_free(&g->context,pin->path);patch_context_free(&g->context,pin);}
        else link=&pin->next;
    }
    ok=1;
done:patch_context_close(&c);return ok;
}
static inline int patch_open_file(PatchGuard *g, const wchar_t *path, DWORD access, int absent, HANDLE *output, PatchError *e)
{
    PatchContext c;wchar_t *native=NULL;HANDLE h=INVALID_HANDLE_VALUE;BY_HANDLE_FILE_INFORMATION info;int ok=0;DWORD code;
    patch_local_context(&c,g);*output=INVALID_HANDLE_VALUE;
    if(!patch_native_path(&c,path,&native,e))goto done;
    h=CreateFileW(native,access,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(h==INVALID_HANDLE_VALUE){code=GetLastError();if(absent&&patch_file_missing(code)){ok=1;goto done;}patch_error_set(e,"io_error","Could not open the pinned file.",code);goto done;}
    if(!patch_handle_info(h,0,&info,e)||!patch_final_path(g,h,path,&c,NULL,e))goto done;
    *output=h;h=INVALID_HANDLE_VALUE;ok=1;
done:if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_context_close(&c);return ok;
}
static inline int patch_record_handle(PatchGuard *g, HANDLE h, PatchFileRecord *output, PatchError *e)
{
    BY_HANDLE_FILE_INFORMATION info,after;PatchFileRecord record={0};PatchContext c;int ok=0;patch_local_context(&c,g);
    memset(output,0,sizeof(*output));if(h==INVALID_HANDLE_VALUE)return 1;
    if(!patch_handle_info(h,0,&info,e))goto done;
    record.exists=1;record.length=((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow;patch_file_identity(&info,record.identity);
    if(!patch_sha256_handle(&c,h,record.sha256)){patch_error_set(e,c.error.code,c.error.message,c.error.win32);goto done;}
    if(!patch_handle_info(h,0,&after,e))goto done;
    if(info.nFileSizeHigh!=after.nFileSizeHigh||info.nFileSizeLow!=after.nFileSizeLow||info.nFileIndexHigh!=after.nFileIndexHigh||info.nFileIndexLow!=after.nFileIndexLow||info.dwVolumeSerialNumber!=after.dwVolumeSerialNumber){
        patch_error_set(e,"file_changed","Pinned file changed while hashing.",ERROR_INVALID_DATA);goto done;}
    *output=record;ok=1;
done:patch_context_close(&c);return ok;
}
/* Caller owns the returned handle and must close it before closing the guard.
   Holding this pin for the complete operation protects the supported game EXE. */
static inline int patch_file_pin(PatchGuard *g, const wchar_t *path, HANDLE *handle, PatchFileRecord *output, PatchError *e)
{
    PatchContext c;wchar_t *p=NULL,*parent=NULL;int found=0,ok=0;HANDLE h=INVALID_HANDLE_VALUE;patch_local_context(&c,g);
    *handle=INVALID_HANDLE_VALUE;memset(output,0,sizeof(*output));
    if(!patch_contained(g,&c,path,0,&p,e)||!patch_parent_path(&c,p,&parent,e)||!patch_pin_chain(g,parent,0,0,&found,e)||!found||
       !patch_open_file(g,p,GENERIC_READ,1,&h,e)||!patch_record_handle(g,h,output,e))goto done;
    *handle=h;h=INVALID_HANDLE_VALUE;ok=1;
done:if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_context_close(&c);return ok;
}
static inline int patch_file_record(PatchGuard *g, const wchar_t *path, PatchFileRecord *output, PatchError *e)
{
    PatchContext c;wchar_t *p=NULL,*parent=NULL;int found=0,ok=0;HANDLE h=INVALID_HANDLE_VALUE;patch_local_context(&c,g);memset(output,0,sizeof(*output));
    if(!patch_contained(g,&c,path,0,&p,e)||!patch_parent_path(&c,p,&parent,e)||!patch_pin_chain(g,parent,0,1,&found,e))goto done;
    if(!found){ok=1;goto done;}
    ok=patch_open_file(g,p,GENERIC_READ,1,&h,e)&&patch_record_handle(g,h,output,e);
done:if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_context_close(&c);return ok;
}
static inline int patch_guard_file(PatchGuard *g, const wchar_t *path, PatchError *e)
{
    PatchFileRecord r;HANDLE h=INVALID_HANDLE_VALUE;int ok=patch_file_pin(g,path,&h,&r,e);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);return ok;
}
static inline int patch_file_read(PatchGuard *g, const wchar_t *path, size_t maximum, PatchContext *out_context,
                                  char **output, size_t *length, PatchError *e)
{
    PatchContext c;wchar_t *p=NULL,*parent=NULL;HANDLE h=INVALID_HANDLE_VALUE;BY_HANDLE_FILE_INFORMATION info;void *memory=NULL;
    uint64_t size;size_t offset=0;int found=0,ok=0;patch_local_context(&c,g);*output=NULL;*length=0;
    if(!patch_contained(g,&c,path,0,&p,e)||!patch_parent_path(&c,p,&parent,e)||!patch_pin_chain(g,parent,0,0,&found,e)||!found||
       !patch_open_file(g,p,GENERIC_READ,0,&h,e)||!patch_handle_info(h,0,&info,e))goto done;
    size=((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow;
    if(size>maximum||size>=SIZE_MAX){patch_error_set(e,"receipt_invalid","File exceeds the bounded read limit.",ERROR_FILE_TOO_LARGE);goto done;}
    if(!patch_alloc(out_context,(size_t)size+1,1,&memory,e))goto done;
    while(offset<(size_t)size){DWORD got=0,want=(DWORD)(((size_t)size-offset)>32768?32768:(size_t)size-offset);
        if(!ReadFile(h,(char *)memory+offset,want,&got,NULL)||!got||got>want){patch_file_fail(e,"Could not complete bounded read.");goto done;}offset+=got;
    }
    *output=(char *)memory;memory=NULL;*length=(size_t)size;ok=1;
done:if(memory)patch_context_free(out_context,memory);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_context_close(&c);return ok;
}
static inline int patch_native_status(PatchGuard *g, HANDLE h, LONG status, PatchIoStatus *io, PatchError *e)
{
    if(status==0x103){DWORD wait=WaitForSingleObject(h,INFINITE);if(wait!=WAIT_OBJECT_0)return patch_file_fail(e,"Could not wait for filesystem completion.");status=io->value.status;}
    if(status>=0)return 1;
    patch_error_set(e,"io_error","Handle-bound filesystem operation failed.",g->platform.nt_error(status));return 0;
}
static inline int patch_handle_rename(PatchGuard *g, HANDLE h, const wchar_t *destination, PatchError *e)
{
    PatchContext c;wchar_t *native=NULL;void *memory=NULL;size_t chars,size;PatchRenameInfo *rename;int ok=0;patch_local_context(&c,g);
    if(!patch_mutation_capable(g,e)||!patch_native_path(&c,destination,&native,e))goto done;
    chars=wcslen(native);size=offsetof(PatchRenameInfo,FileName)+(chars+1)*sizeof(wchar_t);
    if(size>MAXDWORD||!patch_alloc(&c,1,size,&memory,e))goto done;
    rename=(PatchRenameInfo *)memory;rename->FileNameLength=(DWORD)(chars*sizeof(wchar_t));memcpy(rename->FileName,native,(chars+1)*sizeof(wchar_t));
    if(g->platform.set_info){ok=g->platform.set_info(h,3,rename,(DWORD)size)!=FALSE;if(!ok)patch_file_fail(e,"Could not publish pinned file rename.");}
    else {PatchIoStatus io={{0},0};LONG status;rename->FileName[1]=L'?'; /* \\?\ -> \??\ native DOS namespace. */
        status=g->platform.nt_set_info(h,&io,rename,(ULONG)size,10);ok=patch_native_status(g,h,status,&io,e);}
done:patch_context_close(&c);return ok;
}
static inline int patch_handle_delete(PatchGuard *g, HANDLE h, PatchError *e)
{
    BOOLEAN disposition=TRUE;
    if(!patch_mutation_capable(g,e))return 0;
    if(g->platform.set_info){if(g->platform.set_info(h,4,&disposition,sizeof disposition))return 1;return patch_file_fail(e,"Could not delete the known object.");}
    else {PatchIoStatus io={{0},0};LONG status=g->platform.nt_set_info(h,&io,&disposition,sizeof disposition,13);return patch_native_status(g,h,status,&io,e);}
}
static inline int patch_unique_path(PatchContext *c, const wchar_t *destination, const char *prefix, wchar_t **output, PatchError *e)
{
    static LONG sequence=0;char leaf[100];FILETIME now;wchar_t *parent=NULL;
    GetSystemTimeAsFileTime(&now);
    snprintf(leaf,sizeof leaf,"%s%08lX-%08lX%08lX-%08lX",prefix,(unsigned long)GetCurrentProcessId(),
             (unsigned long)now.dwHighDateTime,(unsigned long)now.dwLowDateTime,(unsigned long)InterlockedIncrement(&sequence));
    if(!patch_parent_path(c,destination,&parent,e))return 0;
    return patch_path_join(c,parent,leaf,output,e);
}
static inline int patch_new_stage(PatchGuard *g, PatchContext *c, const wchar_t *destination, PatchStage *stage, PatchError *e)
{
    wchar_t *native=NULL;BY_HANDLE_FILE_INFORMATION info;unsigned attempt;
    memset(stage,0,sizeof(*stage));stage->handle=INVALID_HANDLE_VALUE;
    if(!patch_mutation_capable(g,e))return 0;
    for(attempt=0;attempt<32;++attempt){
        if(!patch_unique_path(c,destination,".mtw-stage-",&stage->path,e)||!patch_native_path(c,stage->path,&native,e))return 0;
        stage->handle=CreateFileW(native,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(stage->handle!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)return patch_file_fail(e,"Could not create staging file.");
    }
    if(stage->handle==INVALID_HANDLE_VALUE)return patch_file_fail(e,"Could not reserve a unique staging file.");
    return patch_handle_info(stage->handle,0,&info,e)&&patch_final_path(g,stage->handle,stage->path,c,NULL,e);
}
static inline void patch_stage_close(PatchGuard *g, PatchStage *stage)
{
    DWORD saved=GetLastError();PatchError ignored={0};
    if(stage->handle!=INVALID_HANDLE_VALUE){
        if(!stage->published&&!stage->deleted)patch_handle_delete(g,stage->handle,&ignored);
        CloseHandle(stage->handle);stage->handle=INVALID_HANDLE_VALUE;
    }
    SetLastError(saved);
}
static inline int patch_write_bytes(HANDLE h, const void *bytes, size_t size, PatchError *e)
{
    const unsigned char *p=(const unsigned char *)bytes;
    if(!bytes&&size){patch_error_set(e,"invalid_argument","Write data is required.",ERROR_INVALID_PARAMETER);return 0;}
    while(size){DWORD n=0,want=(DWORD)(size>32768?32768:size);
        if(!WriteFile(h,p,want,&n,NULL))return patch_file_fail(e,"Could not write staging file.");
        if(!n||n>want){patch_error_set(e,"io_error","Staging write made invalid progress.",ERROR_WRITE_FAULT);return 0;}
        p+=n;size-=n;
    }
    return 1;
}
static inline int patch_copy_stage(PatchGuard *g, PatchContext *c, const wchar_t *source, const wchar_t *destination,
                                   const PatchFileRecord *expected, PatchStage *stage, PatchError *e)
{
    PatchGuard external={0};wchar_t *src=NULL,*parent=NULL;HANDLE h=INVALID_HANDLE_VALUE;PatchFileRecord actual,copy;
    unsigned char bytes[32768];LARGE_INTEGER zero;int ok=0;
    external.platform=g->platform;external.context.allocator=g->context.allocator;external.context.memory_limit=g->context.memory_limit;
    if(!expected){patch_error_set(e,"invalid_argument","An expected source record is required.",ERROR_INVALID_PARAMETER);goto done;}
    if(!patch_path_normalize(c,source,&src,e)||!patch_parent_path(c,src,&parent,e)||!patch_guard_open_internal(&external,parent,1,e)||
       !patch_file_pin(&external,src,&h,&actual,e))goto done;
    if(!actual.exists||!patch_record_matches(&actual,expected)){patch_error_set(e,"file_changed","Copy source differs from its expected record.",ERROR_INVALID_DATA);goto done;}
    if(!patch_new_stage(g,c,destination,stage,e))goto done;
    zero.QuadPart=0;if(!SetFilePointerEx(h,zero,NULL,FILE_BEGIN)){patch_file_fail(e,"Could not rewind copy source.");goto done;}
    for(;;){DWORD n=0;if(!ReadFile(h,bytes,sizeof bytes,&n,NULL)){patch_file_fail(e,"Could not read copy source.");goto done;}if(!n)break;
        if(!patch_write_bytes(stage->handle,bytes,n,e))goto done;}
    if(!FlushFileBuffers(stage->handle)){patch_file_fail(e,"Could not flush copied stage.");goto done;}
    if(!patch_record_handle(g,stage->handle,&copy,e))goto done;
    if(!patch_record_equal(&copy,&actual)){patch_error_set(e,"file_changed","Staged copy checksum mismatch.",ERROR_CRC);goto done;}
    ok=1;
done:if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_guard_close(&external);return ok;
}
static inline int patch_destination(PatchGuard *g, PatchContext *c, const wchar_t *path, wchar_t **dest, PatchError *e)
{
    wchar_t *parent=NULL;int found=0;
    return patch_mutation_capable(g,e)&&patch_contained(g,c,path,0,dest,e)&&patch_parent_path(c,*dest,&parent,e)&&
           patch_pin_chain(g,parent,0,0,&found,e)&&found;
}
static inline int patch_publish(PatchGuard *g, PatchStage *stage, const wchar_t *destination, PatchError *e)
{
    if(!patch_handle_rename(g,stage->handle,destination,e))return 0;
    stage->published=1;
    if(!FlushFileBuffers(stage->handle))return patch_file_fail(e,"Could not flush published file.");
    return 1;
}
static inline int patch_file_write_new(PatchGuard *g, const wchar_t *path, const void *bytes, size_t size, PatchError *e)
{
    PatchContext c;PatchStage stage={INVALID_HANDLE_VALUE,NULL,0,0};wchar_t *dest=NULL;int ok=0;patch_local_context(&c,g);
    if(!patch_destination(g,&c,path,&dest,e)||!patch_new_stage(g,&c,dest,&stage,e)||!patch_write_bytes(stage.handle,bytes,size,e))goto done;
    if(!FlushFileBuffers(stage.handle)){patch_file_fail(e,"Could not flush staged bytes.");goto done;}
    ok=patch_publish(g,&stage,dest,e);
done:patch_stage_close(g,&stage);patch_context_close(&c);return ok;
}
static inline int patch_file_copy_new(PatchGuard *g, const wchar_t *source, const wchar_t *path,
                                      const PatchFileRecord *expected, PatchError *e)
{
    PatchContext c;PatchStage stage={INVALID_HANDLE_VALUE,NULL,0,0};wchar_t *dest=NULL;int ok=0;patch_local_context(&c,g);
    if(!patch_destination(g,&c,path,&dest,e)||!patch_copy_stage(g,&c,source,dest,expected,&stage,e))goto done;
    ok=patch_publish(g,&stage,dest,e);
done:patch_stage_close(g,&stage);patch_context_close(&c);return ok;
}
static inline void patch_recovery_clear(PatchGuard *g)
{
    if(g->recovery_path){patch_context_free(&g->context,g->recovery_path);g->recovery_path=NULL;}
}
static inline int patch_replace_stage(PatchGuard *g, PatchContext *c, PatchStage *stage, const wchar_t *dest,
                                       const PatchFileRecord *expected, PatchError *error)
{
    HANDLE old=INVALID_HANDLE_VALUE;PatchFileRecord actual;PatchError attempt={0},cleanup={0};wchar_t *quarantine=NULL;
    int moved=0,ok=0,recovery=0;
    if(!expected){patch_error_set(&attempt,"invalid_argument","An expected destination record is required.",ERROR_INVALID_PARAMETER);goto failed;}
    if(!patch_open_file(g,dest,GENERIC_READ|DELETE,1,&old,&attempt)||!patch_record_handle(g,old,&actual,&attempt))goto failed;
    if(!patch_record_matches(&actual,expected)){patch_error_set(&attempt,"file_changed","Replacement destination changed.",ERROR_INVALID_DATA);goto failed;}
    if(old!=INVALID_HANDLE_VALUE){
        if(!patch_unique_path(c,dest,".mtw-before-",&quarantine,&attempt))goto failed;
        patch_recovery_clear(g);
        if(!patch_wide_copy(&g->context,quarantine,&g->recovery_path,&attempt))goto failed;
        if(!patch_handle_rename(g,old,quarantine,&attempt))goto failed;
        moved=1;
    }
    if(!patch_handle_rename(g,stage->handle,dest,&attempt))goto failed;
    stage->published=1;
    if(!FlushFileBuffers(stage->handle)){patch_file_fail(&attempt,"Could not flush replacement; original retained.");recovery=1;goto failed;}
    if(old!=INVALID_HANDLE_VALUE&&!patch_handle_delete(g,old,&attempt)){recovery=1;goto failed;}
    patch_recovery_clear(g);ok=1;goto done;
failed:
    if(!stage->published){
        if(stage->handle!=INVALID_HANDLE_VALUE&&patch_handle_delete(g,stage->handle,&cleanup))stage->deleted=1;
        if(moved){memset(&cleanup,0,sizeof cleanup);if(!patch_handle_rename(g,old,dest,&cleanup))recovery=1;else patch_recovery_clear(g);}
        else patch_recovery_clear(g);
    }
    if(recovery)patch_error_set(error,"recovery_required","Publication could not complete; retain the recorded original and recovery state.",attempt.win32);
    else patch_error_set(error,attempt.code?attempt.code:"io_error",attempt.message?attempt.message:"Replacement failed.",attempt.win32);
done:if(old!=INVALID_HANDLE_VALUE)CloseHandle(old);return ok;
}
static inline int patch_file_replace(PatchGuard *g, const wchar_t *source, const wchar_t *path,
                                     const PatchFileRecord *before, const PatchFileRecord *after, PatchError *e)
{
    PatchContext c;PatchStage stage={INVALID_HANDLE_VALUE,NULL,0,0};wchar_t *dest=NULL;int ok=0;patch_local_context(&c,g);
    if(!before||!after){patch_error_set(e,"invalid_argument","Expected source and destination records are required.",ERROR_INVALID_PARAMETER);goto done;}
    if(!patch_destination(g,&c,path,&dest,e)||!patch_copy_stage(g,&c,source,dest,after,&stage,e))goto done;
    ok=patch_replace_stage(g,&c,&stage,dest,before,e);
done:patch_stage_close(g,&stage);patch_context_close(&c);return ok;
}
static inline int patch_file_remove(PatchGuard *g, const wchar_t *path, const PatchFileRecord *expected, PatchError *e)
{
    PatchContext c;wchar_t *p=NULL,*parent=NULL;int found=0,ok=0;HANDLE h=INVALID_HANDLE_VALUE;PatchFileRecord actual={0};patch_local_context(&c,g);
    if(!expected){patch_error_set(e,"invalid_argument","An expected deletion record is required.",ERROR_INVALID_PARAMETER);goto done;}
    if(!patch_mutation_capable(g,e)||!patch_contained(g,&c,path,0,&p,e)||!patch_parent_path(&c,p,&parent,e)||!patch_pin_chain(g,parent,0,1,&found,e))goto done;
    if(found&&(!patch_open_file(g,p,GENERIC_READ|DELETE,1,&h,e)||!patch_record_handle(g,h,&actual,e)))goto done;
    if(!patch_record_matches(&actual,expected)){patch_error_set(e,"file_changed","Deletion target changed.",ERROR_INVALID_DATA);goto done;}
    ok=h==INVALID_HANDLE_VALUE||patch_handle_delete(g,h,e);
done:if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_context_close(&c);return ok;
}
/* Two checked slots preserve the C++ crash/retry protocol. The previous slot is
   a copy: compare content, then use its own transient identity when retiring it. */
static inline int patch_file_write_atomic_owned(PatchGuard *g, const wchar_t *path, const void *bytes, size_t size,
                                                const PatchFileRecord *expected, PatchError *e)
{
    PatchContext c;PatchStage stage={INVALID_HANDLE_VALUE,NULL,0,0};wchar_t *dest=NULL,*previous=NULL;void *memory=NULL;
    PatchFileRecord actual,fallback;size_t n;int ok=0;patch_local_context(&c,g);
    if(!expected){patch_error_set(e,"invalid_argument","An expected metadata record is required.",ERROR_INVALID_PARAMETER);goto done;}
    if(!patch_destination(g,&c,path,&dest,e))goto done;
    n=wcslen(dest);if(!patch_alloc(&c,n+14,sizeof(wchar_t),&memory,e))goto done;
    previous=(wchar_t *)memory;wcscpy(previous,dest);wcscat(previous,L".mtw-previous");
    if(!patch_file_record(g,dest,&actual,e)||!patch_file_record(g,previous,&fallback,e))goto done;
    if(!patch_record_matches(&actual,expected)){patch_error_set(e,"file_changed","Owned metadata changed.",ERROR_INVALID_DATA);goto done;}
    if(fallback.exists&&(!expected->exists||!patch_record_equal(&fallback,expected))){patch_error_set(e,"recovery_required","Different previous metadata is preserved.",ERROR_INVALID_DATA);goto done;}
    if(!patch_new_stage(g,&c,dest,&stage,e)||!patch_write_bytes(stage.handle,bytes,size,e))goto done;
    if(!FlushFileBuffers(stage.handle)){patch_file_fail(e,"Could not flush metadata stage.");goto done;}
    if(expected->exists&&!fallback.exists){
        if(!patch_file_copy_new(g,dest,previous,expected,e)||!patch_file_record(g,previous,&fallback,e))goto done;
        /* The copy's handle has closed: do not turn a substituted slot into
           deletion authority. Verify its content before primary publication,
           then retain this copy's own identity for the later checked removal. */
        if(!fallback.exists||!patch_record_equal(&fallback,expected)){
            patch_error_set(e,"recovery_required","Fresh previous metadata changed; primary publication refused.",ERROR_INVALID_DATA);goto done;
        }
    }
    if(!patch_replace_stage(g,&c,&stage,dest,expected,e))goto done;
    if(!FlushFileBuffers(stage.handle)){patch_file_fail(e,"Could not flush published metadata.");goto done;}
    if(expected->exists&&!patch_file_remove(g,previous,&fallback,e))goto done;
    ok=1;
done:patch_stage_close(g,&stage);patch_context_close(&c);return ok;
}
static inline int patch_find_pin_identity(PatchGuard *g, const wchar_t *path, char out[26], PatchError *e)
{
    PatchPin *pin;int equal;out[0]=0;
    for(pin=g->pins;pin;pin=pin->next){if(!patch_path_equal(&g->platform,pin->path,path,&equal,e))return 0;if(equal){memcpy(out,pin->identity,26);return 1;}}
    return 1;
}
static inline int patch_open_directory_mutation(PatchGuard *g, PatchContext *c, const wchar_t *path,
                                               const char *expected, int absent, HANDLE *output, PatchError *e)
{
    wchar_t *native=NULL;HANDLE h;BY_HANDLE_FILE_INFORMATION info;char identity[26];DWORD code;*output=INVALID_HANDLE_VALUE;
    if(!patch_native_path(c,path,&native,e))return 0;
    h=CreateFileW(native,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(h==INVALID_HANDLE_VALUE){code=GetLastError();if(absent&&patch_file_missing(code))return 1;patch_error_set(e,"io_error","Could not open pinned directory for mutation.",code);return 0;}
    if(!patch_handle_info(h,1,&info,e)||!patch_final_path(g,h,path,c,NULL,e)){CloseHandle(h);return 0;}
    patch_file_identity(&info,identity);
    if(expected[0]&&strcmp(expected,identity)){CloseHandle(h);patch_error_set(e,"file_changed","Directory changed before mutation.",ERROR_INVALID_DATA);return 0;}
    *output=h;return 1;
}
static inline int patch_guard_remove_empty(PatchGuard *g, const wchar_t *path, int *removed, PatchError *e)
{
    PatchContext c;wchar_t *p=NULL,*parent=NULL;HANDLE h=INVALID_HANDLE_VALUE;char expected[26];int found=0,ok=0;PatchError attempt={0};
    patch_local_context(&c,g);*removed=0;
    if(!patch_mutation_capable(g,e)||!patch_contained(g,&c,path,0,&p,e)||!patch_parent_path(&c,p,&parent,e)||!patch_pin_chain(g,parent,0,1,&found,e))goto done;
    if(!found){*removed=1;ok=1;goto done;}
    if(!patch_find_pin_identity(g,p,expected,e)||!patch_guard_release(g,p,0,e)||!patch_open_directory_mutation(g,&c,p,expected,1,&h,e))goto done;
    if(h==INVALID_HANDLE_VALUE){*removed=1;ok=1;goto done;}
    if(patch_handle_delete(g,h,&attempt)){*removed=1;ok=1;}
    else if(attempt.win32==ERROR_DIR_NOT_EMPTY)ok=1;
    else patch_error_set(e,attempt.code,attempt.message,attempt.win32);
done:if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_context_close(&c);return ok;
}
static inline int patch_guard_rename_directory(PatchGuard *g, const wchar_t *source, const wchar_t *destination, PatchError *e)
{
    PatchContext c;wchar_t *src=NULL,*dest=NULL,*parent=NULL;char expected[26];HANDLE h=INVALID_HANDLE_VALUE;int found=0,under,equal,ok=0;patch_local_context(&c,g);
    if(!patch_mutation_capable(g,e)||!patch_contained(g,&c,source,0,&src,e)||!patch_contained(g,&c,destination,0,&dest,e)||
       !patch_path_equal(&g->platform,src,dest,&equal,e)||!patch_path_under(&g->platform,src,dest,&under,e))goto done;
    if(equal||under){patch_error_set(e,"unsafe_path","Overlapping directory rename refused.",ERROR_INVALID_NAME);goto done;}
    if(!patch_path_under(&g->platform,dest,src,&under,e))goto done;
    if(under){patch_error_set(e,"unsafe_path","Overlapping directory rename refused.",ERROR_INVALID_NAME);goto done;}
    if(!patch_pin_chain(g,src,0,0,&found,e)||!found||!patch_parent_path(&c,dest,&parent,e)||!patch_pin_chain(g,parent,0,0,&found,e)||!found||
       !patch_find_pin_identity(g,src,expected,e)||!patch_guard_release(g,src,1,e)||!patch_open_directory_mutation(g,&c,src,expected,0,&h,e))goto done;
    ok=patch_handle_rename(g,h,dest,e);
done:if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);patch_context_close(&c);return ok;
}

#endif
