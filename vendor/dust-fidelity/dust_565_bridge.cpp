// Restore D3D9 R5G6B5 dust blending through dgVoodoo's D3D11 backend.
// The backend normally blends these calls into an RGBA8 intermediate target.
// This bridge temporarily binds a real B5G6R5 target for the game's dust draw.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <climits>
#include <cmath>
#ifdef MTW_DIAGNOSTIC_DUST_STATES
#include <cstdarg>
#include <cstdio>
#endif
#include <cwchar>
#include <intrin.h>
#include "dust_atlas_alpha.h"
#include "dust_alpha_modulate_ps.h"
#include "dust_draw_identity.h"
#include "minhook/include/MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

template<class T> static void drop(T *&p) { if (p) { p->Release(); p = nullptr; } }

static ID3D11Device *g_device = nullptr;
static ID3D11Texture2D *g_scratch = nullptr;
static ID3D11RenderTargetView *g_scratch_rtv = nullptr;
static ID3D11ShaderResourceView *g_scratch_srv = nullptr;
static ID3D11RenderTargetView *g_original_rtv = nullptr;
static ID3D11DepthStencilView *g_original_dsv = nullptr;
static ID3D11VertexShader *g_vs = nullptr;
static ID3D11PixelShader *g_ps = nullptr;
static ID3D11BlendState *g_blend = nullptr;
static ID3D11DepthStencilState *g_depth = nullptr;
static ID3D11RasterizerState *g_raster = nullptr;
static ID3D11PixelShader *g_dust_alpha_ps = nullptr;
static ID3D11Device *g_dust_alpha_device = nullptr;
static ID3D11SamplerState *g_dust_source_sampler = nullptr;
static ID3D11SamplerState *g_dust_clamp_sampler = nullptr;
static IDirect3DBaseTexture9 *g_last_checked_dust_atlas = nullptr;
static UINT g_last_checked_dust_atlas_draws = 0;
static UINT g_width = 0, g_height = 0;

extern "C" void mtw_dust_simulation_set_graphics_ready(int ready);
extern "C" void mtw_dust_simulation_set_reference_microseconds(DWORD step);

typedef HRESULT (STDMETHODCALLTYPE *D3D9CreateDeviceFn)(
    IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT (STDMETHODCALLTYPE *D3D9DrawIndexedPrimitiveUPFn)(
    IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT,
    const void *, D3DFORMAT, const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *D3D9DrawPrimitiveUPFn)(
    IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *D3D9PresentFn)(
    IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef void (STDMETHODCALLTYPE *D3D11DrawIndexedFn)(
    ID3D11DeviceContext *, UINT, UINT, INT);
typedef void (STDMETHODCALLTYPE *D3D11DrawFn)(
    ID3D11DeviceContext *, UINT, UINT);
typedef void (STDMETHODCALLTYPE *D3D11OMSetRenderTargetsFn)(
    ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *, ID3D11DepthStencilView *);

static D3D9CreateDeviceFn original_create_device = nullptr;
static D3D9DrawIndexedPrimitiveUPFn original_draw_up = nullptr;
static D3D9DrawPrimitiveUPFn original_draw_primitive_up = nullptr;
static D3D9PresentFn original_present = nullptr;
static D3D11DrawIndexedFn original_draw_indexed = nullptr;
#ifdef MTW_DUST_DEFERRED_TAIL
static D3D11DrawFn original_draw = nullptr;
#endif
static D3D11OMSetRenderTargetsFn original_set_targets = nullptr;
static IDirect3DDevice9 *game_device = nullptr;
static ID3D11Device *backend_device = nullptr;
static ID3D11DeviceContext *backend_context = nullptr;
static void *hooked_draw_entry = nullptr;
#ifdef MTW_DUST_DEFERRED_TAIL
static void *hooked_nonindexed_entry = nullptr;
#endif
static void *hooked_targets_entry = nullptr;
static DWORD dust_thread = 0;
static bool dust_pending = false;
static bool dust_active = false;
static bool bridge_enabled = false;
static bool inside_bridge = false;
static ID3D11RenderTargetView *dust_original_target = nullptr;
#ifdef MTW_DUST_DEFERRED_TAIL
static ID3D11ShaderResourceView *dust_texture_srv = nullptr;
static ID3D11PixelShader *dust_pixel_shader = nullptr;
static ID3D11ShaderResourceView *known_dust_srv = nullptr;
static ID3D11PixelShader *known_dust_shader = nullptr;
static bool dust_tail_pending = false;
static bool dust_call_returned = false;
static UINT dust_indices_remaining = 0;
static volatile LONG tail_corrected = 0;
static volatile LONG tail_missing = 0;
static volatile LONG tail_target_changes = 0;
#endif
static volatile LONG selected_draws = 0;
static volatile LONG corrected_draws = 0;
static volatile LONG failed_draws = 0;
static volatile LONG translated_draws = 0;
static volatile LONG last_error = 0;
static volatile LONG last_target_format = 0;
static volatile LONG d3d11_hook_calls = 0;
static volatile LONG d3d11_pending_calls = 0;
static volatile LONG d3d11_context_misses = 0;
static volatile LONG d3d11_thread_misses = 0;
static volatile LONG registered_context = 0;
static volatile LONG no_gpu_draws = 0;
static volatile LONG begin_failures = 0;
static volatile LONG end_failures = 0;
static LONG pending_calls_at_selection = 0;
static DWORD frame_total_primitives = 0;
static DWORD frame_dust_primitives = 0;
static DWORD frame_total_vertices = 0;
static DWORD frame_dust_vertices = 0;
static DWORD frame_draw_calls = 0;
static double frame_dust_area_pixels = 0.0;
static volatile LONG last_total_primitives = 0;
static volatile LONG last_dust_primitives = 0;
static volatile LONG last_total_vertices = 0;
static volatile LONG last_dust_vertices = 0;
static volatile LONG last_draw_calls = 0;
static volatile LONG estimated_step_us = 20000;
static volatile LONG last_dust_area_pixels = 0;
static volatile LONG scratch_bound_translated = 0;
static volatile LONG scratch_unbound_translated = 0;
#ifdef MTW_DIAGNOSTIC_DUST_STATES
// Local diagnostic build only. The release build does no file I/O here.
static SRWLOCK dust_diag_lock = SRWLOCK_INIT;
static HANDLE dust_diag_file = INVALID_HANDLE_VALUE;
static bool dust_diag_checked = false;
static bool dust_diag_dumped_texture = false;
static UINT dust_diag_texture_samples = 0;
static UINT dust_diag_gpu_draws = 0;
static volatile LONG dust_diag_sampler_repairs = 0;

static void dust_diag_write(const char *format, ...) {
    AcquireSRWLockExclusive(&dust_diag_lock);
    if (!dust_diag_checked) {
        char path[MAX_PATH] = {};
        const DWORD size = GetEnvironmentVariableA("MTW_DUST_DIAG_PATH", path, MAX_PATH);
        if (size && size < MAX_PATH)
            dust_diag_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                                         nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        dust_diag_checked = true;
    }
    if (dust_diag_file != INVALID_HANDLE_VALUE) {
        char line[1024] = {};
        va_list args;
        va_start(args, format);
        const int size = vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        if (size > 0 && static_cast<size_t>(size) < sizeof(line)) {
            DWORD written = 0;
            WriteFile(dust_diag_file, line, static_cast<DWORD>(size), &written, nullptr);
        }
    }
    ReleaseSRWLockExclusive(&dust_diag_lock);
}

static void dust_diag_d3d9(IDirect3DDevice9 *device, const void *vertices,
                           UINT vertex_count, uintptr_t caller, uintptr_t image) {
    DWORD colors[5] = {};
    DWORD fvf = 0, blend = 0, src = 0, dst = 0, test = 0;
    DWORD alpha_op = 0, alpha_arg1 = 0, alpha_arg2 = 0;
    DWORD address_u = 0, address_v = 0, min_filter = 0, mag_filter = 0;
    float first_u[5] = {}, first_v[5] = {};
    float min_u = 1.0e9f, max_u = -1.0e9f, min_v = 1.0e9f, max_v = -1.0e9f;
    for (UINT vertex = 0; vertex < 5; ++vertex)
        CopyMemory(&colors[vertex], static_cast<const BYTE *>(vertices) +
                   vertex * 32u + 16u, sizeof(colors[vertex]));
    for (UINT vertex = 0; vertex < vertex_count && vertex < 50000u; ++vertex) {
        const BYTE *data = static_cast<const BYTE *>(vertices) + vertex * 32u;
        float u = 0.0f, v = 0.0f;
        CopyMemory(&u, data + 24u, sizeof(u));
        CopyMemory(&v, data + 28u, sizeof(v));
        if (vertex < 5u) { first_u[vertex] = u; first_v[vertex] = v; }
        if (std::isfinite(u) && std::isfinite(v)) {
            if (u < min_u) min_u = u;
            if (u > max_u) max_u = u;
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
        }
    }
    const int phase = mtw_dust_batch_phase(vertices, vertex_count, 32,
                                           caller, image);
    device->GetFVF(&fvf);
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    device->GetRenderState(D3DRS_SRCBLEND, &src);
    device->GetRenderState(D3DRS_DESTBLEND, &dst);
    device->GetRenderState(D3DRS_ALPHATESTENABLE, &test);
    device->GetTextureStageState(0, D3DTSS_ALPHAOP, &alpha_op);
    device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &alpha_arg1);
    device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &alpha_arg2);
    device->GetSamplerState(0, D3DSAMP_ADDRESSU, &address_u);
    device->GetSamplerState(0, D3DSAMP_ADDRESSV, &address_v);
    device->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
    device->GetSamplerState(0, D3DSAMP_MAGFILTER, &mag_filter);
    IDirect3DBaseTexture9 *base = nullptr;
    D3DSURFACE_DESC desc = {};
    HRESULT texture_hr = device->GetTexture(0, &base);
    if (SUCCEEDED(texture_hr) && base && base->GetType() == D3DRTYPE_TEXTURE)
        texture_hr = static_cast<IDirect3DTexture9 *>(base)->GetLevelDesc(0, &desc);
    DWORD texture_hash = 0, opaque_pixels = 0, opaque_border = 0;
    HRESULT lock_hr = E_NOTIMPL;
    if (base && desc.Format == D3DFMT_A1R5G5B5 &&
        desc.Width == 256 && desc.Height == 256 &&
        (++dust_diag_texture_samples % 128u) == 1u) {
        IDirect3DTexture9 *texture = static_cast<IDirect3DTexture9 *>(base);
        D3DLOCKED_RECT locked = {};
        lock_hr = texture->LockRect(0, &locked, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(lock_hr)) {
            texture_hash = 2166136261u;
            for (UINT y = 0; y < 256u; ++y) {
                const WORD *row = reinterpret_cast<const WORD *>(
                    static_cast<const BYTE *>(locked.pBits) + y * locked.Pitch);
                for (UINT x = 0; x < 256u; ++x) {
                    const WORD pixel = row[x];
                    opaque_pixels += (pixel & 0x8000u) != 0u;
                    if (x == 0u || x == 255u || y == 0u || y == 255u)
                        opaque_border += (pixel & 0x8000u) != 0u;
                    texture_hash = (texture_hash ^ pixel) * 16777619u;
                }
            }
            if (phase != MTW_DUST_NONE && !dust_diag_dumped_texture) {
                char path[MAX_PATH] = {};
                const DWORD size = GetEnvironmentVariableA(
                    "MTW_DUST_TEXTURE_DUMP_PATH", path, MAX_PATH);
                if (size && size < MAX_PATH) {
                    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                                              nullptr, CREATE_NEW,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (file != INVALID_HANDLE_VALUE) {
                        for (UINT y = 0; y < 256u; ++y) {
                            DWORD written = 0;
                            const BYTE *row = static_cast<const BYTE *>(locked.pBits) +
                                              y * locked.Pitch;
                            WriteFile(file, row, 512, &written, nullptr);
                        }
                        CloseHandle(file);
                        dust_diag_dumped_texture = true;
                    }
                }
            }
            texture->UnlockRect(0);
        }
    }
    dust_diag_write("d3d9 tick=%llu caller=%08llx verts=%u color=%08lx phase=%d "
                    "fvf=%lx blend=%lu src=%lu dst=%lu test=%lu alpha=%lu,%lu,%lu "
                    "tex=%p fmt=%u size=%ux%u texhr=%08lx lockhr=%08lx hash=%08lx "
                    "opaque=%lu border=%lu corners=%08lx,%08lx,%08lx,%08lx "
                    "uv=%.4f,%.4f:%.4f,%.4f:%.4f,%.4f:%.4f,%.4f:%.4f,%.4f "
                    "bounds=%.4f,%.4f,%.4f,%.4f sampler=%lu,%lu,%lu,%lu\r\n",
                    static_cast<unsigned long long>(GetTickCount64()),
                    static_cast<unsigned long long>(caller - image), vertex_count,
                    colors[0], phase,
                    fvf, blend, src, dst, test, alpha_op, alpha_arg1, alpha_arg2,
                    base, desc.Format, desc.Width, desc.Height,
                    static_cast<unsigned long>(texture_hr),
                    static_cast<unsigned long>(lock_hr), texture_hash, opaque_pixels,
                    opaque_border, colors[1], colors[2], colors[3], colors[4],
                    first_u[0], first_v[0], first_u[1], first_v[1],
                    first_u[2], first_v[2], first_u[3], first_v[3],
                    first_u[4], first_v[4],
                    min_u, max_u, min_v, max_v,
                    address_u, address_v, min_filter, mag_filter);
    drop(base);
}

static void dust_diag_atlas_draw(IDirect3DDevice9 *device,
                                 D3DPRIMITIVETYPE kind, UINT min_vertex,
                                 UINT vertex_count, UINT primitive_count,
                                 D3DFORMAT index_format, const void *vertices,
                                 UINT stride, uintptr_t caller) {
    if (device != game_device || kind != D3DPT_TRIANGLELIST ||
        stride != 32u || !vertices || vertex_count < 5u) return;
    DWORD blend = 0, test = 0;
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    if (!blend) return;
    device->GetRenderState(D3DRS_ALPHATESTENABLE, &test);
    if (test) return;
    IDirect3DBaseTexture9 *base = nullptr;
    if (FAILED(device->GetTexture(0, &base)) || !base) return;
    D3DSURFACE_DESC desc = {};
    if (base->GetType() == D3DRTYPE_TEXTURE) {
        IDirect3DTexture9 *texture = static_cast<IDirect3DTexture9 *>(base);
        texture->GetLevelDesc(0, &desc);
    }
    if (desc.Width && desc.Height) {
        const uintptr_t image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        DWORD fvf = 0, alpha_op = 0, src = 0, dst = 0;
        DWORD color = 0;
        CopyMemory(&color, static_cast<const BYTE *>(vertices) + 16u,
                   sizeof(color));
        device->GetFVF(&fvf);
        device->GetRenderState(D3DRS_SRCBLEND, &src);
        device->GetRenderState(D3DRS_DESTBLEND, &dst);
        device->GetTextureStageState(0, D3DTSS_ALPHAOP, &alpha_op);
        dust_diag_write("particle tick=%llu caller=%08llx kind=%u min=%u verts=%u "
                        "prims=%u index=%u stride=%u fvf=%lx blend=%lu "
                        "test=%lu src=%lu dst=%lu alpha=%lu tex=%p fmt=%u "
                        "size=%ux%u color=%08lx\r\n",
                        static_cast<unsigned long long>(GetTickCount64()),
                        image && caller >= image
                            ? static_cast<unsigned long long>(caller - image) : 0ull,
                        kind, min_vertex, vertex_count, primitive_count,
                        index_format, stride, fvf, blend, test, src, dst,
                        alpha_op, base, desc.Format, desc.Width,
                        desc.Height, color);
    }
    drop(base);
}

static void dust_diag_d3d11(ID3D11DeviceContext *context, UINT count) {
    ID3D11BlendState *blend = nullptr;
    ID3D11ShaderResourceView *resource = nullptr;
    ID3D11PixelShader *shader = nullptr;
    ID3D11SamplerState *sampler = nullptr;
    ID3D11RenderTargetView *target = nullptr;
    FLOAT factors[4] = {};
    UINT mask = 0;
    context->OMGetBlendState(&blend, factors, &mask);
    context->PSGetShaderResources(0, 1, &resource);
    context->PSGetShader(&shader, nullptr, nullptr);
    context->PSGetSamplers(0, 1, &sampler);
    context->OMGetRenderTargets(1, &target, nullptr);
    D3D11_BLEND_DESC desc = {};
    if (blend) blend->GetDesc(&desc);
    D3D11_SAMPLER_DESC sampler_desc = {};
    if (sampler) sampler->GetDesc(&sampler_desc);
    const D3D11_RENDER_TARGET_BLEND_DESC &rt = desc.RenderTarget[0];
    dust_diag_write("d3d11 tick=%llu indices=%u blend=%p enable=%u src=%u dst=%u "
                    "srca=%u dsta=%u mask=%u write=%u srv=%p shader=%p "
                    "sampler=%p,%u,%u,%u "
                    "target=%p scratch=%u returned=%u remaining=%u\r\n",
                    static_cast<unsigned long long>(GetTickCount64()), count,
                    blend, rt.BlendEnable, rt.SrcBlend, rt.DestBlend,
                    rt.SrcBlendAlpha, rt.DestBlendAlpha, mask,
                    rt.RenderTargetWriteMask, resource, shader,
                    sampler, sampler_desc.AddressU, sampler_desc.AddressV,
                    sampler_desc.Filter, target,
                    target == g_scratch_rtv, dust_call_returned, dust_indices_remaining);
    if (resource && (++dust_diag_gpu_draws % 512u) == 1u) {
        ID3D11Resource *raw = nullptr;
        ID3D11Texture2D *source = nullptr;
        ID3D11Device *device = nullptr;
        ID3D11Texture2D *staging = nullptr;
        resource->GetResource(&raw);
        if (raw) raw->QueryInterface(__uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void **>(&source));
        context->GetDevice(&device);
        D3D11_TEXTURE2D_DESC texture_desc = {};
        HRESULT sample_hr = E_NOTIMPL;
        DWORD hash = 2166136261u;
        UINT opaque = 0, row_bytes = 0;
        if (source && device) {
            source->GetDesc(&texture_desc);
            D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
            staging_desc.Usage = D3D11_USAGE_STAGING;
            staging_desc.BindFlags = 0;
            staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            staging_desc.MiscFlags = 0;
            sample_hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
            if (SUCCEEDED(sample_hr)) {
                context->CopyResource(staging, source);
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                sample_hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
                if (SUCCEEDED(sample_hr)) {
                    row_bytes = mapped.RowPitch;
                    char directory[MAX_PATH] = {};
                    const DWORD size = GetEnvironmentVariableA(
                        "MTW_DUST_GPU_DUMP_DIR", directory, MAX_PATH);
                    char path[MAX_PATH] = {};
                    HANDLE file = INVALID_HANDLE_VALUE;
                    if (size && size < MAX_PATH &&
                        snprintf(path, MAX_PATH, "%s\\gpu-%06u.bin", directory,
                                 dust_diag_gpu_draws) < MAX_PATH)
                        file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                                           nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                           nullptr);
                    for (UINT y = 0; y < texture_desc.Height; ++y) {
                        const BYTE *row = static_cast<const BYTE *>(mapped.pData) +
                                          y * mapped.RowPitch;
                        for (UINT x = 0; x < mapped.RowPitch; ++x)
                            hash = (hash ^ row[x]) * 16777619u;
                        if (texture_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                            texture_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) {
                            for (UINT x = 0; x < texture_desc.Width; ++x)
                                opaque += row[x * 4u + 3u] != 0u;
                        }
                        if (file != INVALID_HANDLE_VALUE) {
                            DWORD written = 0;
                            WriteFile(file, row, mapped.RowPitch, &written, nullptr);
                        }
                    }
                    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                    context->Unmap(staging, 0);
                }
            }
        }
        dust_diag_write("gpu tick=%llu draw=%u srv=%p format=%u size=%ux%u "
                        "mips=%u pitch=%u hr=%08lx hash=%08lx nonzero_alpha=%u\r\n",
                        static_cast<unsigned long long>(GetTickCount64()),
                        dust_diag_gpu_draws, resource, texture_desc.Format,
                        texture_desc.Width, texture_desc.Height,
                        texture_desc.MipLevels, row_bytes,
                        static_cast<unsigned long>(sample_hr), hash, opaque);
        drop(staging); drop(device); drop(source); drop(raw);
    }
    drop(target); drop(sampler); drop(shader); drop(resource); drop(blend);
}
#endif
#ifdef MTW_DIAGNOSTIC_DEPTH_STATE
static volatile LONG depth_probe_done = 0;
static void log_dust_depth_state(ID3D11DeviceContext *context) {
    const DWORD hostlong = *(const volatile DWORD *)0x0095EAF8u;
    if (hostlong < 1175u ||
        InterlockedCompareExchange(&depth_probe_done, 1, 0) != 0) return;
    ID3D11DepthStencilState *state = nullptr;
    ID3D11BlendState *blend = nullptr;
    ID3D11DepthStencilView *dsv = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    ID3D11RasterizerState *raster = nullptr;
    UINT stencil_ref = 0;
    UINT sample_mask = 0;
    FLOAT blend_factors[4] = {};
    context->OMGetDepthStencilState(&state, &stencil_ref);
    context->OMGetBlendState(&blend, blend_factors, &sample_mask);
    context->OMGetRenderTargets(1, &rtv, &dsv);
    context->RSGetState(&raster);
    D3D11_DEPTH_STENCIL_DESC depth_desc = {};
    D3D11_BLEND_DESC blend_desc = {};
    D3D11_DEPTH_STENCIL_VIEW_DESC view_desc = {};
    D3D11_RASTERIZER_DESC raster_desc = {};
    D3D11_TEXTURE2D_DESC texture_desc = {};
    if (state) state->GetDesc(&depth_desc);
    if (blend) blend->GetDesc(&blend_desc);
    if (dsv) {
        dsv->GetDesc(&view_desc);
        ID3D11Resource *resource = nullptr;
        dsv->GetResource(&resource);
        ID3D11Texture2D *texture = nullptr;
        if (resource && SUCCEEDED(resource->QueryInterface(
                __uuidof(ID3D11Texture2D), (void **)&texture))) {
            texture->GetDesc(&texture_desc);
        }
        drop(texture);
        drop(resource);
    }
    if (raster) raster->GetDesc(&raster_desc);
    HMODULE self = nullptr;
    WCHAR path[MAX_PATH] = {};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void *)&log_dust_depth_state, &self) &&
        GetModuleFileNameW(self, path, MAX_PATH)) {
        WCHAR *slash = wcsrchr(path, L'\\');
        if (slash) {
            *(slash + 1) = L'\0';
            wcscat_s(path, MAX_PATH, L"dust-depth-probe.txt");
            HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ,
                                      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                char line[768] = {};
                int length = wsprintfA(line,
                    "hostlong=%lu depth_state=%u depth_enable=%u depth_write=%u depth_func=%u "
                    "dsv=%u dsv_format=%u resource_format=%u width=%u height=%u "
                    "raster=%u depth_bias=%d slope_bias_bits=%u scratch_rtv=%u "
                    "blend=%u blend_enable=%u src=%u dst=%u op=%u src_a=%u dst_a=%u op_a=%u "
                    "write_mask=%u sample_mask=%u\r\n",
                    hostlong, state != nullptr, depth_desc.DepthEnable,
                    depth_desc.DepthWriteMask, depth_desc.DepthFunc, dsv != nullptr,
                    view_desc.Format, texture_desc.Format, texture_desc.Width,
                    texture_desc.Height, raster != nullptr, raster_desc.DepthBias,
                    *(const UINT *)&raster_desc.SlopeScaledDepthBias,
                    rtv == g_scratch_rtv, blend != nullptr,
                    blend_desc.RenderTarget[0].BlendEnable,
                    blend_desc.RenderTarget[0].SrcBlend,
                    blend_desc.RenderTarget[0].DestBlend,
                    blend_desc.RenderTarget[0].BlendOp,
                    blend_desc.RenderTarget[0].SrcBlendAlpha,
                    blend_desc.RenderTarget[0].DestBlendAlpha,
                    blend_desc.RenderTarget[0].BlendOpAlpha,
                    blend_desc.RenderTarget[0].RenderTargetWriteMask,
                    sample_mask);
                DWORD written = 0;
                WriteFile(file, line, (DWORD)length, &written, nullptr);
                CloseHandle(file);
            }
        }
    }
    drop(raster); drop(dsv); drop(rtv); drop(blend); drop(state);
}
#endif

struct DustRenderStatus {
    DWORD magic;
    DWORD version;
    DWORD process_id;
    volatile LONG selected_draws;
    volatile LONG corrected_draws;
    volatile LONG failed_draws;
    volatile LONG translated_draws;
    volatile LONG last_error;
    volatile LONG last_target_format;
    volatile LONG d3d11_hook_calls;
    volatile LONG d3d11_pending_calls;
    volatile LONG d3d11_context_misses;
    volatile LONG d3d11_thread_misses;
    volatile LONG registered_context;
    volatile LONG no_gpu_draws;
    volatile LONG begin_failures;
    volatile LONG end_failures;
    volatile LONG last_total_primitives;
    volatile LONG last_dust_primitives;
    volatile LONG estimated_step_us;
    volatile LONG last_total_vertices;
    volatile LONG last_dust_vertices;
    volatile LONG last_draw_calls;
    volatile LONG scratch_bound_translated;
    volatile LONG scratch_unbound_translated;
    volatile LONG last_dust_area_pixels;
#ifdef MTW_DUST_DEFERRED_TAIL
    volatile LONG tail_corrected;
    volatile LONG tail_missing;
    volatile LONG tail_target_changes;
#endif
};
static DustRenderStatus *render_status = nullptr;
static HANDLE render_status_mapping = nullptr;

static void publish_status() {
    if (!render_status) return;
    render_status->selected_draws = selected_draws;
    render_status->corrected_draws = corrected_draws;
    render_status->failed_draws = failed_draws;
    render_status->translated_draws = translated_draws;
    render_status->last_error = last_error;
    render_status->last_target_format = last_target_format;
    render_status->d3d11_hook_calls = d3d11_hook_calls;
    render_status->d3d11_pending_calls = d3d11_pending_calls;
    render_status->d3d11_context_misses = d3d11_context_misses;
    render_status->d3d11_thread_misses = d3d11_thread_misses;
    render_status->registered_context = registered_context;
    render_status->no_gpu_draws = no_gpu_draws;
    render_status->begin_failures = begin_failures;
    render_status->end_failures = end_failures;
    render_status->last_total_primitives = last_total_primitives;
    render_status->last_dust_primitives = last_dust_primitives;
    render_status->estimated_step_us = estimated_step_us;
    render_status->last_total_vertices = last_total_vertices;
    render_status->last_dust_vertices = last_dust_vertices;
    render_status->last_draw_calls = last_draw_calls;
    render_status->scratch_bound_translated = scratch_bound_translated;
    render_status->scratch_unbound_translated = scratch_unbound_translated;
    render_status->last_dust_area_pixels = last_dust_area_pixels;
#ifdef MTW_DUST_DEFERRED_TAIL
    render_status->tail_corrected = tail_corrected;
    render_status->tail_missing = tail_missing;
    render_status->tail_target_changes = tail_target_changes;
#endif
}

static void initialize_status() {
    if (render_status) return;
    WCHAR name[64];
    wsprintfW(name, L"Local\\MTW_DUST_RENDER_STATUS_%lu", GetCurrentProcessId());
    render_status_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, sizeof(DustRenderStatus), name);
    if (!render_status_mapping) return;
    render_status = static_cast<DustRenderStatus *>(MapViewOfFile(
        render_status_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DustRenderStatus)));
    if (!render_status) return;
    ZeroMemory(render_status, sizeof(*render_status));
    render_status->magic = 0x5244574du; // MTWR
#ifdef MTW_DUST_DEFERRED_TAIL
    render_status->version = 8;
#else
    render_status->version = 7;
#endif
    render_status->process_id = GetCurrentProcessId();
}

struct SavedState {
    ID3D11RenderTargetView *rtv = nullptr;
    ID3D11DepthStencilView *dsv = nullptr;
    ID3D11BlendState *blend = nullptr;
    FLOAT blend_factors[4] = {};
    UINT sample_mask = 0;
    ID3D11DepthStencilState *depth = nullptr;
    UINT stencil_ref = 0;
    ID3D11RasterizerState *raster = nullptr;
    ID3D11InputLayout *layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11VertexShader *vs = nullptr;
    ID3D11GeometryShader *gs = nullptr;
    ID3D11PixelShader *ps = nullptr;
    ID3D11ShaderResourceView *srv = nullptr;
    UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};

    void get(ID3D11DeviceContext *ctx) {
        ctx->OMGetRenderTargets(1, &rtv, &dsv);
        ctx->OMGetBlendState(&blend, blend_factors, &sample_mask);
        ctx->OMGetDepthStencilState(&depth, &stencil_ref);
        ctx->RSGetState(&raster);
        ctx->RSGetViewports(&viewport_count, viewports);
        ctx->IAGetInputLayout(&layout);
        ctx->IAGetPrimitiveTopology(&topology);
        ctx->VSGetShader(&vs, nullptr, nullptr);
        ctx->GSGetShader(&gs, nullptr, nullptr);
        ctx->PSGetShader(&ps, nullptr, nullptr);
        ctx->PSGetShaderResources(0, 1, &srv);
    }

    void restore(ID3D11DeviceContext *ctx) {
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->GSSetShader(gs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->IASetInputLayout(layout);
        ctx->IASetPrimitiveTopology(topology);
        ctx->RSSetState(raster);
        if (viewport_count) ctx->RSSetViewports(viewport_count, viewports);
        ctx->OMSetBlendState(blend, blend_factors, sample_mask);
        ctx->OMSetDepthStencilState(depth, stencil_ref);
        ctx->OMSetRenderTargets(1, &rtv, dsv);
        drop(srv); drop(ps); drop(gs); drop(vs); drop(layout); drop(raster);
        drop(depth); drop(blend); drop(dsv); drop(rtv);
    }
};

static HRESULT make_shaders(ID3D11Device *device) {
    if (g_vs && g_ps && g_blend && g_depth && g_raster) return S_OK;
    drop(g_vs); drop(g_ps); drop(g_blend); drop(g_depth); drop(g_raster);
    static const char *source =
        "Texture2D src : register(t0);"
        "float4 VS(uint id : SV_VertexID) : SV_POSITION {"
        "  float2 p = id == 0 ? float2(-1,-1) : id == 1 ? float2(-1,3) : float2(3,-1);"
        "  return float4(p,0,1); }"
        "float4 PS(float4 p : SV_POSITION) : SV_TARGET {"
        "  return src.Load(int3(int2(p.xy),0)); }";
    ID3DBlob *vs = nullptr, *ps = nullptr, *errors = nullptr;
    HRESULT hr = D3DCompile(source, lstrlenA(source), nullptr, nullptr, nullptr,
                            "VS", "vs_4_0", 0, 0, &vs, &errors);
    if (FAILED(hr)) { drop(errors); return hr; }
    drop(errors);
    hr = D3DCompile(source, lstrlenA(source), nullptr, nullptr, nullptr,
                    "PS", "ps_4_0", 0, 0, &ps, &errors);
    if (FAILED(hr)) { drop(vs); drop(errors); return hr; }
    drop(errors);
    hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                                      nullptr, &g_ps);
    drop(vs); drop(ps);
    if (FAILED(hr)) return hr;
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&bd, &g_blend);
    if (FAILED(hr)) return hr;
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = device->CreateDepthStencilState(&dd, &g_depth);
    if (FAILED(hr)) return hr;
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    return device->CreateRasterizerState(&rd, &g_raster);
}

// dgVoodoo builds the fixed-function dust shader from the current D3D9 alpha
// stage. A stale or incorrect alpha stage can turn every masked billboard into
// an opaque square. Use the exact verified shader for this draw, preserving its
// original input registers, diffuse/specular terms, and fog behavior.
static HRESULT make_dust_alpha_shader(ID3D11Device *device) {
    if (g_dust_alpha_device == device && g_dust_alpha_ps) return S_OK;
    drop(g_dust_clamp_sampler);
    drop(g_dust_source_sampler);
    drop(g_dust_alpha_ps);
    drop(g_dust_alpha_device);
    const HRESULT hr = device->CreatePixelShader(
        k_dust_alpha_modulate_ps, sizeof(k_dust_alpha_modulate_ps),
        nullptr, &g_dust_alpha_ps);
    if (SUCCEEDED(hr)) {
        device->AddRef();
        g_dust_alpha_device = device;
    }
    return hr;
}

static HRESULT make_scratch(ID3D11Device *device, UINT width, UINT height) {
    if (g_scratch && device == g_device && width == g_width && height == g_height)
        return S_OK;
    drop(g_scratch_srv); drop(g_scratch_rtv); drop(g_scratch);
    UINT support = 0;
    HRESULT hr = device->CheckFormatSupport(DXGI_FORMAT_B5G6R5_UNORM, &support);
    if (FAILED(hr) || (support & (D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                                  D3D11_FORMAT_SUPPORT_BLENDABLE |
                                  D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) !=
                         (D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                          D3D11_FORMAT_SUPPORT_BLENDABLE |
                          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        return E_NOTIMPL;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B5G6R5_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&td, nullptr, &g_scratch);
    if (FAILED(hr)) return hr;
    hr = device->CreateRenderTargetView(g_scratch, nullptr, &g_scratch_rtv);
    if (FAILED(hr)) return hr;
    hr = device->CreateShaderResourceView(g_scratch, nullptr, &g_scratch_srv);
    if (SUCCEEDED(hr)) {
        if (g_device != device) {
            drop(g_device);
            device->AddRef();
            g_device = device;
        }
        g_width = width;
        g_height = height;
    }
    return hr;
}

static HRESULT blit(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *src,
                    ID3D11RenderTargetView *dst) {
    SavedState old;
    old.get(ctx);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<FLOAT>(g_width);
    viewport.Height = static_cast<FLOAT>(g_height);
    viewport.MaxDepth = 1.0f;
    ctx->OMSetRenderTargets(1, &dst, nullptr);
    ctx->OMSetBlendState(g_blend, nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(g_depth, 0);
    ctx->RSSetState(g_raster);
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &src);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView *none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    old.restore(ctx);
    return S_OK;
}

extern "C" __declspec(dllexport) int MtwBeginDust(ID3D11Device *device,
        ID3D11DeviceContext *ctx, ID3D11RenderTargetView *original) {
    if (!device || !ctx || !original || g_original_rtv) return E_INVALIDARG;
    if (g_device && g_device != device) {
        drop(g_scratch_srv); drop(g_scratch_rtv); drop(g_scratch);
        drop(g_vs); drop(g_ps); drop(g_blend); drop(g_depth); drop(g_raster);
        drop(g_device);
        g_width = g_height = 0;
    }
    ID3D11Resource *resource = nullptr;
    ID3D11Texture2D *texture = nullptr;
    original->GetResource(&resource);
    HRESULT hr = resource ? resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                                     reinterpret_cast<void **>(&texture)) : E_FAIL;
    drop(resource);
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC td = {};
    texture->GetDesc(&td);
    last_target_format = static_cast<LONG>(td.Format);
    hr = td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS && td.SampleDesc.Count == 1
         ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr)) hr = make_shaders(device);
    if (SUCCEEDED(hr)) hr = make_scratch(device, td.Width, td.Height);
    ID3D11ShaderResourceView *source = nullptr;
    if (SUCCEEDED(hr)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(texture, &sd, &source);
    }
    if (SUCCEEDED(hr)) hr = blit(ctx, source, g_scratch_rtv);
    drop(source); drop(texture);
    if (FAILED(hr)) return hr;
    ctx->OMGetRenderTargets(1, nullptr, &g_original_dsv);
    original->AddRef();
    g_original_rtv = original;
    ctx->OMSetRenderTargets(1, &g_scratch_rtv, g_original_dsv);
    return S_OK;
}

extern "C" __declspec(dllexport) int MtwEndDust(ID3D11DeviceContext *ctx) {
    if (!ctx || !g_original_rtv) return E_INVALIDARG;
    HRESULT hr = blit(ctx, g_scratch_srv, g_original_rtv);
    ctx->OMSetRenderTargets(1, &g_original_rtv, g_original_dsv);
    drop(g_original_dsv);
    drop(g_original_rtv);
    return hr;
}

extern "C" __declspec(dllexport) ID3D11RenderTargetView *MtwScratchRtv() {
    return g_scratch_rtv;
}

extern "C" __declspec(dllexport) int MtwIsScratchBound(ID3D11DeviceContext *ctx) {
    if (!ctx || !g_scratch_rtv) return 0;
    ID3D11RenderTargetView *bound = nullptr;
    ctx->OMGetRenderTargets(1, &bound, nullptr);
    const int result = bound == g_scratch_rtv;
    drop(bound);
    return result;
}

static bool patch_vtable_slot(void *object, UINT index, void *replacement,
                              void **original) {
    if (!object || !replacement || !original) return false;
    void **table = *reinterpret_cast<void ***>(object);
    void **slot = &table[index];
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &protection))
        return false;
    void *found = *slot;
    bool installed = found == replacement;
    if (!installed && found) {
        *original = found;
        InterlockedExchangePointer(reinterpret_cast<void *volatile *>(slot), replacement);
        installed = *slot == replacement;
    }
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return installed && *original != nullptr;
}

static int game_dust_draw_phase(IDirect3DDevice9 *device,
                                D3DPRIMITIVETYPE kind, UINT min_vertex,
                                UINT vertex_count, UINT primitive_count,
                                const void *vertices, D3DFORMAT index_format,
                                UINT stride, uintptr_t caller) {
    // The game submits one dust billboard as five vertices and four triangles.
    // Small batches use the same texture and color; the old 100-primitive cutoff
    // left those billboards on dgVoodoo's RGBA8 target during dissipation.
    if (device != game_device || kind != D3DPT_TRIANGLELIST ||
        min_vertex != 0 || vertex_count < 5 || primitive_count < 4 ||
        vertex_count % 5u != 0u || primitive_count % 4u != 0u ||
        vertex_count / 5u != primitive_count / 4u ||
        primitive_count > UINT_MAX / 3u ||
        !vertices || index_format != D3DFMT_INDEX16 || stride != 32)
        return MTW_DUST_NONE;
    DWORD color = 0, fvf = 0;
    CopyMemory(&color, static_cast<const BYTE *>(vertices) + 16, sizeof(color));
    const uintptr_t image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
#ifdef MTW_DIAGNOSTIC_DUST_STATES
    if (image && caller >= image && caller - image == MTW_DUST_DRAW_RETURN_RVA)
        dust_diag_d3d9(device, vertices, vertex_count, caller, image);
#endif
    const int first_phase = mtw_dust_color_phase(color, caller, image);
    const bool possible_mixed = image != 0u && caller >= image &&
        caller - image == MTW_DUST_DRAW_RETURN_RVA;
    if ((first_phase == MTW_DUST_NONE && !possible_mixed) ||
        FAILED(device->GetFVF(&fvf)) || fvf != 0x1c4u)
        return MTW_DUST_NONE;
    IDirect3DBaseTexture9 *base = nullptr;
    if (FAILED(device->GetTexture(0, &base)) || !base) return MTW_DUST_NONE;
    bool match = false;
    if (base->GetType() == D3DRTYPE_TEXTURE) {
        D3DSURFACE_DESC desc = {};
        IDirect3DTexture9 *texture = static_cast<IDirect3DTexture9 *>(base);
        match = SUCCEEDED(texture->GetLevelDesc(0, &desc)) &&
                desc.Format == D3DFMT_A1R5G5B5 &&
                desc.Width == 256 && desc.Height == 256;
    }
    base->Release();
    return match ? mtw_dust_batch_phase(vertices, vertex_count, stride,
                                        caller, image) : MTW_DUST_NONE;
}

static void repair_dust_atlas_alpha(IDirect3DDevice9 *device) {
    IDirect3DBaseTexture9 *base = nullptr;
    if (FAILED(device->GetTexture(0, &base)) || !base) return;
    bool full_check = base != g_last_checked_dust_atlas;
    if (base == g_last_checked_dust_atlas) {
        full_check = ++g_last_checked_dust_atlas_draws >= 1024u;
    }
    if (full_check) {
        g_last_checked_dust_atlas = base;
        g_last_checked_dust_atlas_draws = 0u;
    }
    if (base->GetType() == D3DRTYPE_TEXTURE) {
        IDirect3DTexture9 *texture = static_cast<IDirect3DTexture9 *>(base);
        D3DSURFACE_DESC desc = {};
        if (SUCCEEDED(texture->GetLevelDesc(0, &desc)) &&
            desc.Format == D3DFMT_A1R5G5B5 &&
            desc.Width == 256u && desc.Height == 256u) {
            D3DLOCKED_RECT locked = {};
            const HRESULT lock_hr = texture->LockRect(0, &locked, nullptr, 0);
            if (SUCCEEDED(lock_hr)) {
                const bool needs_repair = full_check ||
                    mtw_dust_atlas_alpha_sentinels_bad(
                        static_cast<const WORD *>(locked.pBits), locked.Pitch);
                if (!needs_repair) {
                    texture->UnlockRect(0);
                    drop(base);
                    return;
                }
#ifdef MTW_DIAGNOSTIC_DUST_STATES
                static bool injected_once = false;
                const bool inject_full_atlas = GetEnvironmentVariableA(
                    "MTW_DUST_INJECT_OPAQUE_FULL_ATLAS", nullptr, 0) != 0;
                if (!injected_once && (inject_full_atlas || GetEnvironmentVariableA(
                        "MTW_DUST_INJECT_OPAQUE_ATLAS", nullptr, 0))) {
                    const UINT side = inject_full_atlas ? 256u : 128u;
                    for (UINT y = 0u; y < side; ++y) {
                        WORD *row = reinterpret_cast<WORD *>(
                            static_cast<BYTE *>(locked.pBits) + y * locked.Pitch);
                        for (UINT x = 0u; x < side; ++x)
                            row[x] |= 0x8000u;
                    }
                    injected_once = true;
                }
                const bool bypass = GetEnvironmentVariableA(
                    "MTW_DUST_SKIP_ATLAS_REPAIR", nullptr, 0) != 0;
                const unsigned changed = bypass ? 0u : mtw_repair_dust_atlas_alpha(
                    static_cast<WORD *>(locked.pBits), locked.Pitch,
                    UINT64_C(0x389100512363FEBD), 6847u);
                dust_diag_write("atlas tick=%llu texture=%p lock=%08lx changed=%u "
                                "bypass=%u\r\n",
                                static_cast<unsigned long long>(GetTickCount64()),
                                texture, static_cast<unsigned long>(lock_hr),
                                changed, bypass);
#else
                mtw_repair_dust_atlas_alpha(
                    static_cast<WORD *>(locked.pBits), locked.Pitch,
                    UINT64_C(0x389100512363FEBD), 6847u);
#endif
                texture->UnlockRect(0);
            } else {
                last_error = lock_hr;
                g_last_checked_dust_atlas = nullptr;
            }
        }
    }
    drop(base);
}

// The game's transformed dust billboard is five 32-byte vertices: center
// followed by four screen-space corners. This is submitted geometry, so the
// area term responds to actual particle size and camera distance rather than
// to a guessed screen occupancy. Invalid geometry contributes no area.
static double submitted_dust_area(const void *vertices, UINT vertex_count) {
    if (!vertices || vertex_count % 5u != 0u) return 0.0;
    const BYTE *bytes = static_cast<const BYTE *>(vertices);
    double total = 0.0;
    for (UINT base = 0; base < vertex_count; base += 5u) {
        double min_x = 1.0e30, min_y = 1.0e30;
        double max_x = -1.0e30, max_y = -1.0e30;
        bool valid = true;
        for (UINT corner = 1; corner <= 4u; ++corner) {
            float x, y;
            CopyMemory(&x, bytes + (base + corner) * 32u, sizeof(x));
            CopyMemory(&y, bytes + (base + corner) * 32u + 4u, sizeof(y));
            if (!std::isfinite(x) || !std::isfinite(y)) { valid = false; break; }
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
        if (valid && max_x > min_x && max_y > min_y)
            total += (max_x - min_x) * (max_y - min_y);
    }
    return total;
}

#ifdef MTW_DUST_DEFERRED_TAIL
static void clear_dust_scope() {
    drop(dust_texture_srv);
    drop(dust_pixel_shader);
    dust_original_target = nullptr;
    dust_active = false;
    dust_pending = false;
    dust_tail_pending = false;
    dust_call_returned = false;
    dust_indices_remaining = 0;
}

static void finish_deferred_dust(bool complete) {
    if (dust_active) {
        inside_bridge = true;
        const HRESULT finish = static_cast<HRESULT>(MtwEndDust(backend_context));
        inside_bridge = false;
        if (SUCCEEDED(finish) && complete) {
            InterlockedIncrement(&corrected_draws);
            if (dust_call_returned) InterlockedIncrement(&tail_corrected);
        } else {
            if (FAILED(finish)) {
                last_error = finish;
                InterlockedIncrement(&end_failures);
            }
            InterlockedIncrement(&failed_draws);
            InterlockedIncrement(&tail_missing);
        }
    }
    clear_dust_scope();
    publish_status();
}

// A replacement D3D11 context may arrive during a device transition. The old
// context may already be invalid, so release retained COM references without
// drawing into it. The interrupted frame is counted as a missing tail.
static void abandon_deferred_dust() {
    drop(known_dust_srv);
    drop(known_dust_shader);
    if (!dust_active && !dust_pending && !dust_tail_pending) return;
    if (dust_active) InterlockedIncrement(&tail_missing);
    drop(g_original_dsv);
    drop(g_original_rtv);
    clear_dust_scope();
    publish_status();
}

static bool is_deferred_dust_draw(ID3D11DeviceContext *context) {
    const ID3D11ShaderResourceView *expected_srv = dust_active
        ? dust_texture_srv : known_dust_srv;
    const ID3D11PixelShader *expected_shader = dust_active
        ? dust_pixel_shader : known_dust_shader;
    if (!expected_srv || !expected_shader) return false;
    ID3D11ShaderResourceView *resource = nullptr;
    ID3D11PixelShader *shader = nullptr;
    context->PSGetShaderResources(0, 1, &resource);
    context->PSGetShader(&shader, nullptr, nullptr);
    const bool same = resource == expected_srv && shader == expected_shader;
    drop(resource);
    drop(shader);
    return same;
}

static void remember_dust_signature() {
    if (!dust_texture_srv || !dust_pixel_shader) return;
    if (known_dust_srv != dust_texture_srv) {
        drop(known_dust_srv);
        dust_texture_srv->AddRef();
        known_dust_srv = dust_texture_srv;
    }
    if (known_dust_shader != dust_pixel_shader) {
        drop(known_dust_shader);
        dust_pixel_shader->AddRef();
        known_dust_shader = dust_pixel_shader;
    }
}

static void STDMETHODCALLTYPE dust_draw_nonindexed(ID3D11DeviceContext *context,
                                                   UINT count, UINT start) {
    if (dust_tail_pending && dust_active && !inside_bridge &&
        context == backend_context &&
        GetCurrentThreadId() == dust_thread)
        finish_deferred_dust(false);
    original_draw(context, count, start);
}
#endif

static void STDMETHODCALLTYPE dust_draw_indexed(ID3D11DeviceContext *context,
                                                UINT count, UINT start, INT base) {
    InterlockedIncrement(&d3d11_hook_calls);
    if (dust_pending) {
        InterlockedIncrement(&d3d11_pending_calls);
        if (context != backend_context)
            InterlockedIncrement(&d3d11_context_misses);
        if (GetCurrentThreadId() != dust_thread)
            InterlockedIncrement(&d3d11_thread_misses);
    }
    if (dust_pending && context == backend_context &&
        GetCurrentThreadId() == dust_thread) {
#ifdef MTW_DUST_DEFERRED_TAIL
        // The backend may submit none, one, or several GPU draws after the
        // D3D9 call returns. Match an established dust shader/texture then
        // keep RGB565 bound until all of that call's indices have been drawn.
        if (dust_call_returned && !is_deferred_dust_draw(context)) {
            if (dust_active) finish_deferred_dust(false);
            original_draw_indexed(context, count, start, base);
            return;
        }
        if (!count || count % 3u != 0u || count > dust_indices_remaining) {
            if (dust_active) finish_deferred_dust(false);
            original_draw_indexed(context, count, start, base);
            return;
        }
        if (dust_call_returned && dust_active && !MtwIsScratchBound(context)) {
            inside_bridge = true;
            context->OMSetRenderTargets(1, &g_scratch_rtv, g_original_dsv);
            inside_bridge = false;
        }
#endif
        if (!dust_active) {
            ID3D11RenderTargetView *target = nullptr;
            context->OMGetRenderTargets(1, &target, nullptr);
            if (target) {
                inside_bridge = true;
                HRESULT hr = static_cast<HRESULT>(
                    MtwBeginDust(backend_device, context, target));
                inside_bridge = false;
                last_error = hr;
                dust_active = SUCCEEDED(hr);
                if (!dust_active) InterlockedIncrement(&begin_failures);
                if (dust_active) dust_original_target = g_original_rtv;
                drop(target);
            }
        }
        if (dust_active) {
#ifdef MTW_DUST_DEFERRED_TAIL
            if (!dust_texture_srv && !dust_pixel_shader) {
                context->PSGetShaderResources(0, 1, &dust_texture_srv);
                context->PSGetShader(&dust_pixel_shader, nullptr, nullptr);
                if (!dust_call_returned) remember_dust_signature();
            }
#endif
            InterlockedIncrement(&translated_draws);
#ifdef MTW_DIAGNOSTIC_DUST_STATES
            dust_diag_d3d11(context, count);
#endif
#ifdef MTW_DIAGNOSTIC_DEPTH_STATE
            log_dust_depth_state(context);
#endif
#ifdef MTW_DIAGNOSTIC_SCRATCH_CHECK
            if (MtwIsScratchBound(context))
                InterlockedIncrement(&scratch_bound_translated);
            else
                InterlockedIncrement(&scratch_unbound_translated);
#endif
        }
    }
    ID3D11PixelShader *previous_shader = nullptr;
    ID3D11SamplerState *previous_sampler = nullptr;
    bool replaced_shader = false;
    bool replaced_sampler = false;
    if (dust_pending && dust_active && context == backend_context &&
        GetCurrentThreadId() == dust_thread &&
        g_dust_alpha_device == backend_device && g_dust_alpha_ps) {
#ifdef MTW_DIAGNOSTIC_DUST_STATES
        const bool bypass = GetEnvironmentVariableA(
            "MTW_DUST_SKIP_ALPHA_REPAIR", nullptr, 0) != 0;
#else
        const bool bypass = false;
#endif
        if (!bypass) {
            context->PSGetShader(&previous_shader, nullptr, nullptr);
            context->PSSetShader(g_dust_alpha_ps, nullptr, 0);
            replaced_shader = true;
        }
        context->PSGetSamplers(0, 1, &previous_sampler);
        if (previous_sampler) {
            D3D11_SAMPLER_DESC desc = {};
            previous_sampler->GetDesc(&desc);
            if (desc.AddressU != D3D11_TEXTURE_ADDRESS_CLAMP ||
                desc.AddressV != D3D11_TEXTURE_ADDRESS_CLAMP) {
#ifdef MTW_DIAGNOSTIC_DUST_STATES
                const bool skip_clamp = GetEnvironmentVariableA(
                    "MTW_DUST_SKIP_SAMPLER_REPAIR", nullptr, 0) != 0;
#else
                const bool skip_clamp = false;
#endif
                if (!skip_clamp) {
                    if (g_dust_source_sampler != previous_sampler ||
                        !g_dust_clamp_sampler) {
                        drop(g_dust_clamp_sampler);
                        drop(g_dust_source_sampler);
                        previous_sampler->AddRef();
                        g_dust_source_sampler = previous_sampler;
                        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                        const HRESULT clamp_hr = backend_device->CreateSamplerState(
                            &desc, &g_dust_clamp_sampler);
                        if (FAILED(clamp_hr)) last_error = clamp_hr;
                    }
                    if (g_dust_clamp_sampler) {
                        context->PSSetSamplers(0, 1, &g_dust_clamp_sampler);
                        replaced_sampler = true;
#ifdef MTW_DIAGNOSTIC_DUST_STATES
                        if (InterlockedIncrement(&dust_diag_sampler_repairs) <= 16) {
                            ID3D11SamplerState *bound_sampler = nullptr;
                            context->PSGetSamplers(0, 1, &bound_sampler);
                            D3D11_SAMPLER_DESC bound_desc = {};
                            if (bound_sampler) bound_sampler->GetDesc(&bound_desc);
                            dust_diag_write("clamp tick=%llu original=%p replacement=%p "
                                            "bound=%p address=%u,%u filter=%u\r\n",
                                            static_cast<unsigned long long>(GetTickCount64()),
                                            previous_sampler, g_dust_clamp_sampler,
                                            bound_sampler, bound_desc.AddressU,
                                            bound_desc.AddressV, bound_desc.Filter);
                            drop(bound_sampler);
                        }
#endif
                    }
                }
            }
        }
    }
    original_draw_indexed(context, count, start, base);
    if (replaced_sampler)
        context->PSSetSamplers(0, 1, &previous_sampler);
    drop(previous_sampler);
    if (replaced_shader) {
        context->PSSetShader(previous_shader, nullptr, 0);
        drop(previous_shader);
    }
#ifdef MTW_DUST_DEFERRED_TAIL
    if (dust_pending && dust_active && context == backend_context &&
        GetCurrentThreadId() == dust_thread && !inside_bridge) {
        dust_indices_remaining -= count;
        if (!dust_indices_remaining) finish_deferred_dust(true);
    }
#endif
}

static void STDMETHODCALLTYPE dust_set_targets(ID3D11DeviceContext *context,
        UINT count, ID3D11RenderTargetView *const *targets,
        ID3D11DepthStencilView *depth) {
#ifdef MTW_DUST_DEFERRED_TAIL
    if (dust_tail_pending && dust_active && !inside_bridge &&
        context == backend_context && GetCurrentThreadId() == dust_thread &&
        !(count == 1 && targets && targets[0] == dust_original_target)) {
        InterlockedIncrement(&tail_target_changes);
        finish_deferred_dust(false);
    }
#endif
    if (dust_active && !inside_bridge && context == backend_context &&
        GetCurrentThreadId() == dust_thread && count == 1 && targets &&
        targets[0] == dust_original_target && g_scratch_rtv) {
        ID3D11RenderTargetView *scratch_target = g_scratch_rtv;
        original_set_targets(context, 1, &scratch_target, depth);
        return;
    }
    original_set_targets(context, count, targets, depth);
}

static HRESULT STDMETHODCALLTYPE dust_draw_up(IDirect3DDevice9 *device,
        D3DPRIMITIVETYPE kind, UINT min_vertex, UINT vertex_count,
        UINT primitive_count, const void *indices, D3DFORMAT index_format,
        const void *vertices, UINT stride) {
#ifdef MTW_DUST_DEFERRED_TAIL
    // A subsequent game draw is the boundary for an undelivered GPU batch.
    // Fully deferred work observed in the dense battle arrived before it.
    if (device == game_device && dust_call_returned && dust_pending)
        finish_deferred_dust(false);
#endif
    frame_total_primitives += primitive_count;
    frame_total_vertices += vertex_count;
    ++frame_draw_calls;
#ifdef MTW_DIAGNOSTIC_DUST_STATES
    dust_diag_atlas_draw(device, kind, min_vertex, vertex_count,
                         primitive_count, index_format, vertices, stride,
                         reinterpret_cast<uintptr_t>(_ReturnAddress()));
#endif
    const bool may_select = bridge_enabled && !dust_pending && backend_context &&
#ifdef MTW_DUST_DEFERRED_TAIL
        !dust_tail_pending;
#else
        true;
#endif
    const int phase = may_select
        ? game_dust_draw_phase(device, kind, min_vertex, vertex_count,
                               primitive_count, vertices, index_format, stride,
                               reinterpret_cast<uintptr_t>(_ReturnAddress()))
        : MTW_DUST_NONE;
    const bool selected = phase != MTW_DUST_NONE;
    if (selected) {
        repair_dust_atlas_alpha(device);
#ifdef MTW_DIAGNOSTIC_DUST_STATES
        if (GetEnvironmentVariableA("MTW_DUST_INJECT_BAD_ALPHA", nullptr, 0))
            device->SetTextureStageState(0, D3DTSS_ALPHAOP,
                                         D3DTOP_SELECTARG2);
        if (GetEnvironmentVariableA("MTW_DUST_INJECT_WRAP", nullptr, 0)) {
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        }
#endif
        // Keep the native-step estimator's original work categories. The
        // gray phase is a graphics repair; its particles already matched the
        // native reference before this bridge selection was extended.
        if (mtw_dust_counts_as_reference_work(phase)) {
            frame_dust_primitives += primitive_count;
            frame_dust_vertices += vertex_count;
            frame_dust_area_pixels += submitted_dust_area(vertices, vertex_count);
        }
        dust_thread = GetCurrentThreadId();
        dust_pending = true;
#ifdef MTW_DUST_DEFERRED_TAIL
        dust_call_returned = false;
        dust_tail_pending = false;
        dust_indices_remaining = primitive_count * 3u;
#endif
        pending_calls_at_selection = d3d11_pending_calls;
        InterlockedIncrement(&selected_draws);
    }
    HRESULT hr = original_draw_up(device, kind, min_vertex, vertex_count,
                                  primitive_count, indices, index_format,
                                  vertices, stride);
    if (selected) {
#ifdef MTW_DUST_DEFERRED_TAIL
        if (dust_pending && dust_indices_remaining) {
            if (!dust_active && d3d11_pending_calls != pending_calls_at_selection) {
                InterlockedIncrement(&failed_draws);
                clear_dust_scope();
            } else {
                // No in-call GPU draw is not proof that the game dust was
                // culled: dgVoodoo can deliver its whole batch after return.
                if (!dust_active) InterlockedIncrement(&no_gpu_draws);
                dust_call_returned = true;
                dust_tail_pending = true;
            }
        }
        publish_status();
#else
        if (dust_active) {
            inside_bridge = true;
            HRESULT finish = static_cast<HRESULT>(MtwEndDust(backend_context));
            inside_bridge = false;
            if (SUCCEEDED(finish)) InterlockedIncrement(&corrected_draws);
            else {
                last_error = finish;
                InterlockedIncrement(&end_failures);
                InterlockedIncrement(&failed_draws);
            }
        } else {
            if (d3d11_pending_calls == pending_calls_at_selection)
                InterlockedIncrement(&no_gpu_draws);
            else
                InterlockedIncrement(&failed_draws);
        }
        dust_original_target = nullptr;
        dust_active = false;
        dust_pending = false;
        publish_status();
#endif
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE dust_draw_primitive_up(IDirect3DDevice9 *device,
        D3DPRIMITIVETYPE kind, UINT primitive_count, const void *vertices,
        UINT stride) {
    if (device == game_device) {
        frame_total_primitives += primitive_count;
        ++frame_draw_calls;
    }
    return original_draw_primitive_up(device, kind, primitive_count, vertices,
                                      stride);
}

static HRESULT STDMETHODCALLTYPE dust_present(IDirect3DDevice9 *device,
        const RECT *source, const RECT *destination, HWND window,
        const RGNDATA *dirty) {
#ifdef MTW_DUST_DEFERRED_TAIL
    if (device == game_device && dust_pending && dust_call_returned)
        finish_deferred_dust(false);
#endif
    if (device == game_device && bridge_enabled && frame_draw_calls > 0) {
        const DWORD non_dust = frame_total_vertices > frame_dust_vertices
            ? frame_total_vertices - frame_dust_vertices : 0;
        // Native D3D9 Present intervals at ten checkpoints on the supported
        // 2560x1440 setup, fitted to submitted game work on two saved replays.
        // The 14.5-ms floor is the directly measured low-work cadence.
        // See the artifact-root native-present-work-model.json and its
        // leave-one-out error audit; this is a local compatibility target.
        double estimate = 187.834199 + 4.794142 * frame_draw_calls +
                          0.576567 * non_dust +
                          0.349897 * frame_dust_vertices +
                          69.397035 * (frame_dust_area_pixels / 1000000.0);
        if (estimate < 14500.0) estimate = 14500.0;
        if (estimate > 100000.0) estimate = 100000.0;
        const DWORD step = static_cast<DWORD>(estimate + 0.5);
        mtw_dust_simulation_set_reference_microseconds(step);
        last_total_primitives = static_cast<LONG>(frame_total_primitives);
        last_dust_primitives = static_cast<LONG>(frame_dust_primitives);
        last_total_vertices = static_cast<LONG>(frame_total_vertices);
        last_dust_vertices = static_cast<LONG>(frame_dust_vertices);
        last_draw_calls = static_cast<LONG>(frame_draw_calls);
        last_dust_area_pixels = frame_dust_area_pixels > LONG_MAX
            ? LONG_MAX : static_cast<LONG>(frame_dust_area_pixels + 0.5);
        estimated_step_us = static_cast<LONG>(step);
        publish_status();
    }
    frame_total_primitives = frame_dust_primitives = 0;
    frame_total_vertices = frame_dust_vertices = frame_draw_calls = 0;
    frame_dust_area_pixels = 0.0;
    return original_present(device, source, destination, window, dirty);
}

static HRESULT STDMETHODCALLTYPE dust_create_device(IDirect3D9 *d3d9, UINT adapter,
        D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *parameters,
        IDirect3DDevice9 **output) {
    HRESULT hr = original_create_device(d3d9, adapter, type, window, flags,
                                        parameters, output);
    if (SUCCEEDED(hr) && output && *output) {
        game_device = *output;
        const bool primitive_hook = patch_vtable_slot(game_device, 83,
                reinterpret_cast<void *>(dust_draw_primitive_up),
                reinterpret_cast<void **>(&original_draw_primitive_up));
        const bool draw_hook = patch_vtable_slot(game_device, 84,
                reinterpret_cast<void *>(dust_draw_up),
                reinterpret_cast<void **>(&original_draw_up));
        const bool present_hook = patch_vtable_slot(game_device, 17,
                reinterpret_cast<void *>(dust_present),
                reinterpret_cast<void **>(&original_present));
        bridge_enabled = primitive_hook && draw_hook && present_hook;
        if (bridge_enabled && backend_context)
            mtw_dust_simulation_set_graphics_ready(1);
    }
    return hr;
}

extern "C" int mtw_dust_graphics_wrap_d3d9(IDirect3D9 *d3d9) {
    initialize_status();
    return patch_vtable_slot(d3d9, 16,
        reinterpret_cast<void *>(dust_create_device),
        reinterpret_cast<void **>(&original_create_device)) ? 1 : 0;
}

extern "C" int mtw_dust_graphics_register_d3d11(ID3D11Device *device,
                                                   ID3D11DeviceContext *context) {
    if (!device || !context) return 0;
    mtw_dust_simulation_set_graphics_ready(0);
#ifdef MTW_DUST_DEFERRED_TAIL
    if (backend_context && backend_context != context)
        abandon_deferred_dust();
#endif
    void **table = *reinterpret_cast<void ***>(context);
    void *draw_entry = table[12];
#ifdef MTW_DUST_DEFERRED_TAIL
    void *nonindexed_entry = table[13];
#endif
    void *targets_entry = table[33];
    if (hooked_draw_entry &&
        (hooked_draw_entry != draw_entry || hooked_targets_entry != targets_entry
#ifdef MTW_DUST_DEFERRED_TAIL
         || hooked_nonindexed_entry != nonindexed_entry
#endif
        ))
        return 0;
    if (!hooked_draw_entry) {
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return 0;
        status = MH_CreateHook(draw_entry, reinterpret_cast<void *>(dust_draw_indexed),
                               reinterpret_cast<void **>(&original_draw_indexed));
        if (status != MH_OK) return 0;
        status = MH_CreateHook(targets_entry, reinterpret_cast<void *>(dust_set_targets),
                               reinterpret_cast<void **>(&original_set_targets));
        if (status != MH_OK) {
            MH_RemoveHook(draw_entry);
            return 0;
        }
#ifdef MTW_DUST_DEFERRED_TAIL
        status = MH_CreateHook(nonindexed_entry,
                               reinterpret_cast<void *>(dust_draw_nonindexed),
                               reinterpret_cast<void **>(&original_draw));
        if (status != MH_OK) {
            MH_RemoveHook(draw_entry);
            MH_RemoveHook(targets_entry);
            return 0;
        }
#endif
        if (MH_EnableHook(draw_entry) != MH_OK ||
            MH_EnableHook(targets_entry) != MH_OK
#ifdef MTW_DUST_DEFERRED_TAIL
            || MH_EnableHook(nonindexed_entry) != MH_OK
#endif
        ) {
            MH_DisableHook(draw_entry);
            MH_DisableHook(targets_entry);
#ifdef MTW_DUST_DEFERRED_TAIL
            MH_DisableHook(nonindexed_entry);
#endif
            MH_RemoveHook(draw_entry);
            MH_RemoveHook(targets_entry);
#ifdef MTW_DUST_DEFERRED_TAIL
            MH_RemoveHook(nonindexed_entry);
#endif
            return 0;
        }
        hooked_draw_entry = draw_entry;
        hooked_targets_entry = targets_entry;
#ifdef MTW_DUST_DEFERRED_TAIL
        hooked_nonindexed_entry = nonindexed_entry;
#endif
    }
    backend_device = device;
    backend_context = context;
    const HRESULT shader_hr = make_dust_alpha_shader(device);
    if (FAILED(shader_hr)) last_error = shader_hr;
    registered_context = static_cast<LONG>(reinterpret_cast<UINT_PTR>(context));
    if (game_device && original_draw_up && original_draw_primitive_up)
        mtw_dust_simulation_set_graphics_ready(1);
    return 1;
}
