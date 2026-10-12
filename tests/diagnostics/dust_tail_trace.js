// Diagnostic only. Relate exact dgVoodoo dust pixel-shader GPU draws to the
// game-facing D3D9 DrawIndexedPrimitiveUP call that the bridge brackets.
const HOSTLONG = ptr("0x0095EAF8");
const START = 1174;
const END = 1177;
const shaderPrefix = [0x44,0x58,0x42,0x43,0xfc,0x54,0x7f,0x88,0xc6,0x46,
                      0x81,0x58,0x61,0x2e,0x2f,0x8d,0x96,0x0b,0x5f,0xfd];
const installed = new Set();
const dustShaderObjects = new Set();
const boundShaders = new Map();
let activeCall = 0;
let nextCall = 0;
let translatedDraws = 0;
let outsideDraws = 0;
let done = false;

function method(object, slot) {
    return object.readPointer().add(slot * Process.pointerSize).readPointer();
}
function hook(address, label, callbacks) {
    const key = label + ':' + address.toString();
    if (installed.has(key)) return;
    installed.add(key);
    Interceptor.attach(address, callbacks);
}
function isDustShader(data) {
    if (!data || data.byteLength !== 876) return false;
    const bytes = new Uint8Array(data);
    for (let i = 0; i < shaderPrefix.length; i++)
        if (bytes[i] !== shaderPrefix[i]) return false;
    return true;
}
function hookD3D11(device, context) {
    const getResources = new NativeFunction(method(context, 73), 'void',
        ['pointer', 'uint', 'uint', 'pointer'], 'stdcall');
    const getTargets = new NativeFunction(method(context, 89), 'void',
        ['pointer', 'uint', 'pointer', 'pointer'], 'stdcall');
    function release(pointer) {
        if (!pointer.isNull()) {
            const fn = new NativeFunction(method(pointer, 2), 'uint', ['pointer'], 'stdcall');
            fn(pointer);
        }
    }
    function renderTargetFormat(target) {
        if (target.isNull()) return -1;
        const desc = Memory.alloc(64);
        const getDesc = new NativeFunction(method(target, 8), 'void',
            ['pointer', 'pointer'], 'stdcall');
        getDesc(target, desc);
        return desc.readU32();
    }
    hook(method(device, 15), 'CreatePixelShader', {
        onEnter(args) {
            this.out = args[4];
            this.isDust = false;
            const n = args[2].toUInt32();
            if (n !== 876 || args[1].isNull()) return;
            this.isDust = isDustShader(args[1].readByteArray(n));
        },
        onLeave(hr) {
            if (this.isDust && hr.toInt32() >= 0 && !this.out.isNull()) {
                const shader = this.out.readPointer();
                dustShaderObjects.add(shader.toString());
                send({kind: 'dust_shader_created', shader: shader.toString(),
                      hostlong: HOSTLONG.readU32()});
            }
        }
    });
    hook(method(context, 9), 'PSSetShader', {
        onEnter(args) { boundShaders.set(args[0].toString(), args[1].toString()); }
    });
    hook(method(context, 12), 'DrawIndexed', {
        onEnter(args) {
            const hostlong = HOSTLONG.readU32();
            if (hostlong < START || hostlong > END || done) return;
            const shader = boundShaders.get(args[0].toString());
            if (!shader || !dustShaderObjects.has(shader)) return;
            const resourceOut = Memory.alloc(Process.pointerSize);
            const targetOut = Memory.alloc(Process.pointerSize);
            const depthOut = Memory.alloc(Process.pointerSize);
            resourceOut.writePointer(NULL);
            targetOut.writePointer(NULL);
            depthOut.writePointer(NULL);
            getResources(args[0], 0, 1, resourceOut);
            getTargets(args[0], 1, targetOut, depthOut);
            const resource = resourceOut.readPointer();
            const target = targetOut.readPointer();
            const depth = depthOut.readPointer();
            const targetFormat = renderTargetFormat(target);
            translatedDraws++;
            if (!activeCall) outsideDraws++;
            send({kind: 'dust_gpu_draw', hostlong: hostlong,
                  shader: shader, index_count: args[1].toUInt32(),
                  texture_srv: resource.toString(), target_rtv: target.toString(),
                  target_format: targetFormat,
                  start_index: args[2].toUInt32(), base_vertex: args[3].toInt32(),
                  active_d3d9_call: activeCall,
                  translated_draws: translatedDraws,
                  outside_draws: outsideDraws});
            release(resource); release(target); release(depth);
        }
    });
    send({kind: 'd3d11_ready', device: device.toString(),
          context: context.toString()});
}
const d3d11 = Module.load('d3d11.dll');
for (const name of ['D3D11CreateDevice', 'D3D11CreateDeviceAndSwapChain']) {
    hook(d3d11.getExportByName(name), name, {
        onEnter(args) {
            this.deviceOut = args[name === 'D3D11CreateDevice' ? 7 : 9];
            this.contextOut = args[name === 'D3D11CreateDevice' ? 9 : 11];
        },
        onLeave(hr) {
            if (hr.toInt32() < 0) return;
            try { hookD3D11(this.deviceOut.readPointer(), this.contextOut.readPointer()); }
            catch (e) { send({kind: 'scope_error', where: name, error: String(e)}); }
        }
    });
}
const proxy = Process.getModuleByName('D3D9.dll');
hook(proxy.getExportByName('Direct3DCreate9'), 'Direct3DCreate9', {
    onLeave(result) {
        if (result.isNull()) return;
        hook(method(result, 16), 'D3D9CreateDevice', {
            onEnter(args) { this.out = args[6]; },
            onLeave(hr) {
                if (hr.toInt32() < 0 || this.out.isNull()) return;
                const game = this.out.readPointer();
                hook(method(game, 84), 'D3D9DrawIndexedPrimitiveUP', {
                    onEnter(args) {
                        this.call = 0;
                        try {
                            if (HOSTLONG.readU32() < START ||
                                args[1].toUInt32() !== 4 || args[2].toUInt32() !== 0 ||
                                args[3].toUInt32() < 100 || args[6].toUInt32() !== 101 ||
                                args[8].toUInt32() !== 32 || args[7].isNull() ||
                                (args[7].add(16).readU32() & 0x00ffffff) !== 0x0094771f)
                                return;
                            this.call = ++nextCall;
                            activeCall = this.call;
                            send({kind: 'd3d9_dust_enter', hostlong: HOSTLONG.readU32(),
                                  call: this.call, vertices: args[3].toUInt32(),
                                  primitives: args[4].toUInt32()});
                        } catch (e) {
                            send({kind: 'scope_error', where: 'd3d9_dust', error: String(e)});
                        }
                    },
                    onLeave() {
                        if (this.call) {
                            send({kind: 'd3d9_dust_leave', hostlong: HOSTLONG.readU32(),
                                  call: this.call});
                            activeCall = 0;
                        }
                    }
                });
                hook(method(game, 17), 'D3D9Present', {
                    onLeave() {
                        if (!done && HOSTLONG.readU32() >= END) {
                            done = true;
                            send({kind: 'scope_complete', hostlong: HOSTLONG.readU32(),
                                  d3d9_calls: nextCall,
                                  translated_draws: translatedDraws,
                                  outside_draws: outsideDraws});
                            Thread.sleep(2);
                        }
                    }
                });
                send({kind: 'd3d9_ready', game: game.toString()});
            }
        });
    }
});
send({kind: 'ready', proxy: proxy.path, d3d11: d3d11.path});
