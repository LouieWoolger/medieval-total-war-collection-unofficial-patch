"""Real C module acceptance; all binaries and fixtures live in pytest's external root."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid
import winreg
import pytest

ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(os.name != "nt", reason="native Win32 state fixtures")

def build_state_fixture(directory, source):
    cc = os.environ.get("MTW_CC") or shutil.which("i686-w64-mingw32-gcc.exe")
    assert cc, "Set MTW_CC to an i686 MinGW C99 compiler"
    triple = subprocess.check_output([cc,"-dumpmachine"],text=True).strip()
    assert triple == "i686-w64-mingw32"
    version = subprocess.check_output([cc,"-dumpfullversion","-dumpversion"],text=True).strip()
    bindir = Path(cc).resolve().parent
    output = directory / (Path(source).stem + ".exe")
    args = [cc,"-B",str(bindir.parent/"libexec/gcc"/triple/version)+os.sep,"-std=c99",
            "-D_WIN32_WINNT=0x0501","-Os","-Wall","-Wextra","-Werror","-municode",
            "-static","-static-libgcc","-Wl,--major-subsystem-version,5,--minor-subsystem-version,1",
            "-I",str(ROOT/"src"),str(ROOT/"tests"/source),"-o",str(output),"-ladvapi32"]
    result = subprocess.run(args,capture_output=True,text=True,timeout=120,
        env=dict(os.environ,PATH=str(bindir)+os.pathsep+os.environ["PATH"]))
    (directory/"compile.json").write_text(json.dumps(dict(command=args,exit=result.returncode,stdout=result.stdout,stderr=result.stderr),indent=2),encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    assert not result.stderr
    return output


@pytest.fixture(scope="module")
def state_exe(tmp_path_factory):
    return build_state_fixture(tmp_path_factory.mktemp("c-state-build"), "native_state.c")


@pytest.fixture(scope="module")
def boundary_exe(tmp_path_factory):
    return build_state_fixture(tmp_path_factory.mktemp("c-registry-boundary"), "native_registry_boundary.c")

def invoke(exe, args):
    return subprocess.run([str(exe),*map(str,args)],capture_output=True,timeout=45)

def raw_registry_value(key,name):
    import ctypes
    query=ctypes.WinDLL("advapi32").RegQueryValueExW
    query.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p]
    query.restype=ctypes.c_long
    size=ctypes.c_uint32();kind=ctypes.c_uint32()
    assert query(int(key),name,None,ctypes.byref(kind),None,ctypes.byref(size))==0
    data=ctypes.create_string_buffer(size.value)
    assert query(int(key),name,None,ctypes.byref(kind),data,ctypes.byref(size))==0
    return kind.value,data.raw[:size.value]

def canonical(value):
    text=json.dumps(value,ensure_ascii=False,separators=(",",":"))
    for c in ("'","<",">","&","\u0085","\u2028","\u2029"):
        text=text.replace(c,f"\\u{ord(c):04x}")
    return text.encode("utf-8")

def test_c_json_bounds_and_allocation_cleanup(state_exe,tmp_path):
    result=invoke(state_exe,["--json-suite"])
    (tmp_path/"native.log").write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,result.stdout.decode()+result.stderr.decode()
    summary=re.search(rb"RESULT passed=(\d+) failed=(\d+)",result.stdout)
    assert summary and int(summary[1])>=65 and summary[2]==b"0"

def test_c_json_canonical_checksum_order_and_explicit_lengths(state_exe,tmp_path):
    body={"first":"' < > & \u0085 \u2028 \u2029 😀 é\x00tail","integrity_sha256":"stale","last":[-2**63,2**63-1,True,None]}
    path=tmp_path/"ordered.json";path.write_bytes(b"\xef\xbb\xbf"+json.dumps(body).encode())
    result=invoke(state_exe,["--seal",path]);assert result.returncode==0,result.stdout
    unsigned={key:value for key,value in body.items() if key!="integrity_sha256"}
    body["integrity_sha256"]=hashlib.sha256(canonical(unsigned)).hexdigest().upper()
    assert result.stdout==canonical(body)  # replacement keeps original member position

def test_c_json_frozen_powershell_canonical(state_exe,tmp_path):
    from test_lifecycle_native import generate_legacy_fixtures
    source=tmp_path
    generate_legacy_fixtures(source)
    path=source/"ps-canonical.json"
    result=invoke(state_exe,["--canonical",path]);assert result.returncode==0,result.stdout
    assert result.stdout==path.read_bytes()
    receipt=source/"legacy-v2-receipt.json"
    parsed=json.loads(receipt.read_text(encoding="utf-8-sig"))
    result=invoke(state_exe,["--seal",receipt]);assert result.returncode==0,result.stdout
    assert json.loads(result.stdout)["integrity_sha256"]==parsed["integrity_sha256"]

def test_c_registry_identity_frozen_cpp_vectors(state_exe):
    source=ROOT/"tests/fixtures/native_identity_vectors.json"
    vectors=json.loads(source.read_text(encoding="utf-8"))["vectors"]
    assert len(vectors)==14
    for vector in vectors:
        result=invoke(state_exe,["--key",vector["input"]]);assert result.returncode==0,result.stdout
        assert result.stdout.decode().splitlines()==[vector["registration_key"],vector["mutex"]]

@pytest.mark.parametrize("values,good", [
    ([{"name":"", "kind":"String","value":"head\x00tail"}],True),
    ([{"name":"dword", "kind":"DWord","value":-2**31},{"name":"qword","kind":"QWord","value":-2**63}],True),
    ([{"name":"bytes", "kind":"None","value":[0,255]},{"name":"multi","kind":"MultiString","value":["first","😀"]}],True),
    ([{"name":"multi", "kind":"MultiString","value":[]}],True),
    ([{"name":"bad\x00tail", "kind":"String","value":"x"}],False),
    ([{"name":"unknown", "kind":"Unknown","value":1}],False),
    ([{"name":"A", "kind":"String","value":"x"},{"name":"a","kind":"String","value":"x"}],False),
    ([{"name":"n", "kind":"DWord","value":2**31}],False),
    ([{"name":"n", "kind":"Binary","value":[256]}],False),
    ([{"name":"n", "kind":"MultiString","value":["a\x00b"]}],False),
    ([{"name":"n", "kind":"MultiString","value":[""]}],False),
])
def test_c_registry_snapshot_validation(state_exe,tmp_path,values,good):
    path=tmp_path/"registry.json";snapshot={"exists":True,"values":values,"subkeys":0};path.write_bytes(canonical(snapshot))
    result=invoke(state_exe,["--registry",path]);assert (result.returncode==0)==good,result.stdout
    if good: assert json.loads(result.stdout)==snapshot
    else: assert result.stdout.strip()==b"ERROR receipt_invalid"

@pytest.fixture
def isolated_registry():
    """Only this fresh UUID namespace is eligible for fixture cleanup."""
    token=uuid.uuid4().hex
    root=rf"Software\MedievalPatchLifecycleTests\{token}"
    created=[]
    def create(path):
        parts=path.split("\\")
        for end in range(len(root.split("\\")),len(parts)+1):
            part="\\".join(parts[:end])
            if part not in created: created.append(part)
        return winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,path,0,winreg.KEY_ALL_ACCESS|winreg.KEY_WOW64_64KEY)
    for hive in ("HKCU","HKLM"):
        for view in ("32","64"):
            with create(root+"\\"+hive+view) as key: winreg.SetValueEx(key,"IsolationSentinel",0,winreg.REG_SZ,token)
    def path(view="32"):
        return root+rf"\HKLM{view}\Software\Microsoft\Windows\CurrentVersion\Uninstall\Task3"
    try: yield root,path,create
    finally:
        # Exact task-owned paths, leaf first. No production uninstall parent.
        for item in sorted(created,key=len,reverse=True):
            assert item==root or item.startswith(root+"\\")
            try: winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,item,winreg.KEY_WOW64_64KEY)
            except FileNotFoundError: pass

def test_c_registry_live_round_trip_preserves_unknown_bytes(state_exe,tmp_path,isolated_registry):
    root,path,create=isolated_registry
    with create(path()) as key:
        for name,kind,value in (("",winreg.REG_SZ,"a\0b"),("HighDword",winreg.REG_DWORD,0x80000000),
            ("HighQword",winreg.REG_QWORD,0xFFFFFFFFFFFFFFFF),("Multi",winreg.REG_MULTI_SZ,["one","😀"]),
            ("EmptyMulti",winreg.REG_MULTI_SZ,[]),("Binary",winreg.REG_BINARY,b"\0\xff"),
            ("None",winreg.REG_NONE,b"\x05\0"),("DisplayName",winreg.REG_SZ,"old")):
            winreg.SetValueEx(key,name,0,kind,value)
    with create(path()+r"\PersonalChild") as key: winreg.SetValueEx(key,"Keep",0,winreg.REG_SZ,"personal")
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path()) as key:
        raw_before={name:raw_registry_value(key,name) for name in ("","HighDword","HighQword","Multi","EmptyMulti","Binary","None")}
    before=invoke(state_exe,["--live-read",root,"Registry32","modern"])
    assert before.returncode==0,before.stdout
    snapshot=json.loads(before.stdout);values={v["name"]:v for v in snapshot["values"]}
    assert values["HighDword"]["value"]==-2**31 and values["HighQword"]["value"]==-1
    assert values[""]["value"]=="a\0b" and values["Multi"]["value"]==["one","😀"] and snapshot["subkeys"]==1
    values["DisplayName"]["value"]="new"
    target=tmp_path/"target.json";target.write_bytes(canonical(snapshot))
    result=invoke(state_exe,["--live-set",root,"Registry32","modern",target]);assert result.returncode==0,result.stdout
    after=invoke(state_exe,["--live-read",root,"Registry32","modern"]);assert json.loads(after.stdout)==snapshot
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path()) as key:
        assert {name:raw_registry_value(key,name) for name in raw_before}==raw_before
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path(),0,winreg.KEY_ALL_ACCESS|winreg.KEY_WOW64_64KEY) as key:
        winreg.SetValueEx(key,"HighDword",0,winreg.REG_DWORD,7)
        winreg.SetValueEx(key,"DisplayName",0,winreg.REG_SZ,"leave me")
    result=invoke(state_exe,["--live-set",root,"Registry32","modern",target]);assert result.returncode==2
    assert result.stdout.strip()==b"ERROR registry_cleanup_conflict"
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path(),0,winreg.KEY_READ|winreg.KEY_WOW64_64KEY) as key:
        assert winreg.QueryValueEx(key,"DisplayName")[0]=="leave me"
        assert winreg.QueryValueEx(key,"HighDword")[0]==7

@pytest.mark.parametrize("view,route",[("Registry32","no-delete-ex"),("Registry64","no-delete-ex"),("Registry64","native32")])
def test_c_registry_value_removal_and_native32_alias(state_exe,tmp_path,isolated_registry,view,route):
    root,path,create=isolated_registry
    actual="32" if route=="native32" else view[-2:]
    with create(path(actual)) as key: winreg.SetValueEx(key,"DisplayName",0,winreg.REG_SZ,"owned")
    target=tmp_path/"absent.json";target.write_bytes(canonical({"exists":False,"values":[],"subkeys":0}))
    result=invoke(state_exe,["--live-set",root,view,route,target]);assert result.returncode==0,result.stdout
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path(actual)) as key:
        assert winreg.QueryInfoKey(key)[:2]==(0,0)
    observed=invoke(state_exe,["--live-read",root,view,route])
    assert observed.returncode==0,observed.stdout
    assert json.loads(observed.stdout)=={"exists":True,"subkeys":0,"values":[]}


@pytest.mark.parametrize("route",["modern","legacy"])
@pytest.mark.parametrize("mode",["control","value","subkey"])
def test_c_registry_final_flush_preserves_late_unknown_state(boundary_exe,tmp_path,isolated_registry,route,mode):
    root,path,create=isolated_registry
    with create(path()) as key:
        winreg.SetValueEx(key,"DisplayName",0,winreg.REG_SZ,"owned")
    # Record this child for exact task-only teardown, but leave it absent for injection.
    if mode=="subkey":
        with create(path()+r"\PersonalChild"): pass
        winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,path()+r"\PersonalChild",winreg.KEY_WOW64_64KEY)
    result=invoke(boundary_exe,[root,mode,route])
    assert result.returncode==0,result.stdout+result.stderr
    report=json.loads(result.stdout)
    (tmp_path/"boundary-result.json").write_text(json.dumps(report,indent=2))
    assert report["flushes"]==1 and report["values_at_injection"]==0
    assert report["injected"]==(mode!="control")
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path(),0,winreg.KEY_READ|winreg.KEY_WOW64_64KEY) as key:
        assert winreg.QueryInfoKey(key)[:2]==((1,0) if mode=="subkey" else (0,1) if mode=="value" else (0,0))
        if mode=="value":
            assert raw_registry_value(key,"PersonalValue")== (winreg.REG_SZ,"late unrelated state\0".encode("utf-16-le"))
    if mode=="subkey":
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path()+r"\PersonalChild") as key:
            assert raw_registry_value(key,"Keep")== (winreg.REG_SZ,"late unrelated state\0".encode("utf-16-le"))
    assert report["delete_calls"]==0
    assert report["setter_success"]==(mode=="control")
    assert report["error"]==("" if mode=="control" else "registry_verification_failed")


@pytest.mark.parametrize("exists,values,subkeys,expected",[
    (False,[],0,True), (True,[],0,True), (True,[],1,False),
    (True,[{"name":"Personal","kind":"String","value":"keep"}],0,False),
    (True,[{"name":"DisplayName","kind":"String","value":"foreign"}],0,False),
])
def test_c_absent_expected_state_only_matches_content_free_snapshots(state_exe,tmp_path,exists,values,subkeys,expected):
    absent={"exists":False,"values":[],"subkeys":0}
    physical={"exists":exists,"values":values,"subkeys":subkeys}
    for a,b in ((absent,physical),(physical,absent)):
        source=tmp_path/"comparison.json";source.write_bytes(canonical({"a":a,"b":b}))
        result=invoke(state_exe,["--registry-compare",source]);assert result.returncode==0,result.stdout
        report=json.loads(result.stdout)
        assert report["a"]==a and report["b"]==b
        assert report["strict"]==(not exists)
        assert report["expected"]==expected

@pytest.mark.parametrize("kind,raw",[(winreg.REG_MULTI_SZ,b"a\0\0\0\0\0b\0\0\0\0\0"),
    (winreg.REG_MULTI_SZ,b"a\0\0\0"),(winreg.REG_SZ,b"a"),(winreg.REG_DWORD,b"\0"),(12345,b"1234")])
def test_c_registry_malformed_live_data_refused(state_exe,isolated_registry,kind,raw):
    import ctypes
    root,path,create=isolated_registry
    api=ctypes.WinDLL("advapi32").RegSetValueExW
    api.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32]
    api.restype=ctypes.c_long
    with create(path()) as key:assert api(int(key),"Personal",0,kind,raw,len(raw))==0
    result=invoke(state_exe,["--live-read",root,"Registry32","modern"])
    assert result.returncode==2 and result.stdout.strip()==b"ERROR registry_invalid",result.stdout

PAYLOAD_NAMES=["dgVoodoo_D3D9.dll","ddraw.dll","D3DImm.dll","dgVoodoo.conf","D3D9.dll"]
STATE=".unofficial-medieval-total-war-patch"
UNINSTALL="Uninstall Unofficial Medieval Patch.exe"
EXE_HASH="23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"

def seal(value):
    value["integrity_sha256"]=hashlib.sha256(canonical({k:v for k,v in value.items() if k!="integrity_sha256"})).hexdigest().upper()
    return value

@pytest.fixture
def receipt_fixture(state_exe,tmp_path):
    game=tmp_path/"Medieval é Ω 😀";game.mkdir()
    result=invoke(state_exe,["--identity",game]);assert result.returncode==0,result.stdout
    identity=json.loads(result.stdout)
    files={}
    original=b"earliest personal original\0"
    originals=game/STATE/"originals";originals.mkdir(parents=True)
    for name in PAYLOAD_NAMES:
        (originals/name).write_bytes(original)
        files[name]={"existed":True,"original_sha256":hashlib.sha256(original).hexdigest().upper(),
            "original_length":len(original),"snapshot_relative":"originals/"+name,"installed_sha256":"A"*64,
            "installed_length":100,"sidecar_relative":name+".unofficial-patch.bak","sidecar_created":True}
    receipt={"schema":"unofficial-medieval-total-war-patch-install-v2","status":"installed","installation_id":str(uuid.uuid4()),
        "installer_version":"1.0.0","installer_sha256":None,"installed_utc":"2026-10-02T00:00:00Z",
        "target_directory":identity["target"],"target_executable_sha256":EXE_HASH,"preinstall_mode":"clean","repair_count":0,
        "files":files,"locked_settings":{},"directory_identity":identity["directory_identity"],"owner_sid":identity["owner_sid"],
        "registration_key":identity["registration_key"],"uninstaller_sha256":"B"*64,"restoration_kind":"verified-preinstall"}
    return game,seal(receipt),identity

def test_c_receipt_preserves_v1_v2_and_earliest_originals(state_exe,tmp_path,receipt_fixture):
    game,receipt,identity=receipt_fixture
    path=tmp_path/"receipt.json";path.write_bytes(canonical(receipt))
    result=invoke(state_exe,["--receipt",game,path,"originals"]);assert result.returncode==0,result.stdout
    assert result.stdout==path.read_bytes()
    receipt["schema"]="unofficial-medieval-total-war-patch-install-v1"
    for field in ("integrity_sha256","owner_sid","directory_identity","registration_key","uninstaller_sha256"):receipt.pop(field)
    path.write_bytes(canonical(receipt));result=invoke(state_exe,["--receipt",game,path,"originals"])
    assert result.returncode==0 and result.stdout==path.read_bytes(),result.stdout
    (game/STATE/"originals"/"D3D9.dll").write_bytes(b"changed")
    result=invoke(state_exe,["--receipt",game,path,"originals"]);assert result.returncode==2 and b"receipt_invalid" in result.stdout

@pytest.mark.parametrize("field,value,code",[("owner_sid","S-1-5-999","wrong_account"),
    ("directory_identity","00000000:0000000000000001","installation_moved"),
    ("target_directory","F:\\Other","installation_moved"),("registration_key","Other","receipt_invalid"),
    ("installation_id","00000000-0000-0000-0000-000000000000","receipt_invalid"),
    ("target_executable_sha256","A"*64,"receipt_invalid"),("status","unknown","receipt_invalid"),
    ("target_directory","F:\\safe\0foreign","receipt_invalid")])
def test_c_receipt_rejects_foreign_or_malformed_binding(state_exe,tmp_path,receipt_fixture,field,value,code):
    game,receipt,_=receipt_fixture;receipt[field]=value;seal(receipt)
    path=tmp_path/"receipt.json";path.write_bytes(canonical(receipt))
    result=invoke(state_exe,["--receipt",game,path]);assert result.returncode==2 and result.stdout.strip()==f"ERROR {code}".encode(),result.stdout

def journal_for(receipt,identity):
    absent={"exists":False,"sha256":None,"length":0}
    actions=[]
    for name in PAYLOAD_NAMES:
        actions.append({"kind":"file","relative":name,"before":absent.copy(),"after":{"exists":True,"sha256":"A"*64,"length":100}})
    actions.extend([{"kind":"file","relative":UNINSTALL,"before":absent.copy(),"after":{"exists":True,"sha256":"B"*64,"length":99}},
        {"kind":"file","relative":STATE+"/install-manifest.json","before":absent.copy(),"after":{"exists":True,"sha256":"C"*64,"length":700}}])
    values=[{"name":name,"kind":"String","value":value} for name,value in {
        "ProductId":"unofficial-medieval-total-war-collection-patch","OwnerSid":identity["owner_sid"],
        "InstallationId":receipt["installation_id"],"InstallLocation":identity["target"],
        "UninstallString":'"'+identity["target"]+'\\'+UNINSTALL+'"'}.items()]
    actions.append({"kind":"registry","hive":"CurrentUser","view":"Registry32","name":identity["registration_key"],
        "before":{"exists":False,"values":[],"subkeys":0},"after":{"exists":True,"values":values,"subkeys":0}})
    return seal({"schema":"unofficial-medieval-patch-transaction-v2","target":identity["target"],
        "directory_identity":identity["directory_identity"],"owner_sid":identity["owner_sid"],"installation_id":receipt["installation_id"],
        "operation":"install","phase":"prepared","started":0,"created_utc":"2026-10-02T00:00:00Z","actions":actions,
        "removal_receipt":None,"install_receipt":receipt,"recovery_archive":"","state_existed":True})

def test_c_journal_validates_required_actions_and_retained_receipt(state_exe,tmp_path,receipt_fixture):
    game,receipt,identity=receipt_fixture;journal=journal_for(receipt,identity);path=tmp_path/"journal.json";path.write_bytes(canonical(journal))
    result=invoke(state_exe,["--journal",game,path]);assert result.returncode==0 and result.stdout==path.read_bytes(),result.stdout
    # Typed decoding must not silently add transient file IDs to the wire checksum.
    assert all(set(a["after"])=={"exists","sha256","length"} for a in json.loads(result.stdout)["actions"] if a["kind"]=="file")

@pytest.mark.parametrize("mutation",["checksum","missing","duplicate","relative_nul","foreign_key","bad_runtime","receipt_id","started","unknown_loss"])
def test_c_journal_corruption_fails_before_file_work(state_exe,tmp_path,receipt_fixture,mutation):
    game,receipt,identity=receipt_fixture;journal=journal_for(receipt,identity)
    if mutation=="missing":journal["actions"].pop(0)
    elif mutation=="duplicate":journal["actions"].append(journal["actions"][0])
    elif mutation=="relative_nul":journal["actions"][0]["relative"]+="\0foreign"
    elif mutation=="foreign_key":journal["actions"][-1]["name"]="Unrelated"
    elif mutation=="bad_runtime":journal["actions"][0]["after"]["sha256"]="F"*64
    elif mutation=="receipt_id":journal["installation_id"]=str(uuid.uuid4())
    elif mutation=="started":journal["started"]=1
    elif mutation=="unknown_loss":journal["actions"][-1]["before"]={"exists":True,"subkeys":0,"values":[{"name":"Personal","kind":"String","value":"keep"}]}
    seal(journal)
    if mutation=="checksum":journal["target"]="F:\\Foreign"
    path=tmp_path/"bad.json";path.write_bytes(canonical(journal));result=invoke(state_exe,["--journal",game,path])
    assert result.returncode==2 and result.stdout.strip()==b"ERROR receipt_invalid",result.stdout

def test_c_genuine_saved_cpp_journals_and_receipts(state_exe,tmp_path):
    bulk=os.environ.get("MTW_C_HISTORICAL_BULK")
    if not bulk:pytest.skip("Set MTW_C_HISTORICAL_BULK to preserved C++ interruption corpus")
    bulk=Path(bulk);proof=json.loads((bulk/"cpp-interruptions/proof.json").read_text(encoding="utf-8"))
    cases=proof["fixtures"] if "fixtures" in proof else proof["cases"]
    assert len(cases)==4
    for case in cases:
        game=Path(case["game"]);transaction=game/(STATE+"-transaction");path=transaction/"journal.json"
        original=path.read_bytes();assert hashlib.sha256(original).hexdigest().upper()==case["journal_sha256"]
        result=invoke(state_exe,["--journal",game,path,transaction]);assert result.returncode==0,result.stdout
        assert result.stdout==original and path.read_bytes()==original
    game=bulk/"full-games/GOG C++ upgrade";path=game/STATE/"install-manifest.json";original=path.read_bytes()
    result=invoke(state_exe,["--receipt",game,path,"originals"]);assert result.returncode==0,result.stdout
    assert result.stdout==original and path.read_bytes()==original

def test_c_migration_classification_and_wire_file_identity(state_exe):
    result=invoke(state_exe,["--migration-suite"]);assert result.returncode==0,result.stdout
    assert b"RESULT passed=9 failed=0" in result.stdout

def test_c_receipt_draft_repair_preserves_guid_baseline_and_member_order(state_exe,tmp_path,receipt_fixture):
    game,receipt,_=receipt_fixture;path=tmp_path/"receipt.json";path.write_bytes(canonical(receipt))
    result=invoke(state_exe,["--draft",game,path]);assert result.returncode==0,result.stdout
    draft=json.loads(result.stdout)
    assert draft["installation_id"]==receipt["installation_id"] and draft["files"]==receipt["files"]
    assert draft["repair_count"]==1 and list(draft)[:len(receipt)]==list(receipt)
    assert seal(draft)["integrity_sha256"]==json.loads(result.stdout)["integrity_sha256"]

def test_c_installed_registry_uses_complete_canonical_root(state_exe,tmp_path,receipt_fixture):
    game,receipt,identity=receipt_fixture;path=tmp_path/"receipt.json";path.write_bytes(canonical(receipt))
    result=invoke(state_exe,["--installed-registry",game,path]);assert result.returncode==0,result.stdout
    snapshot=json.loads(result.stdout);values={v["name"]:v["value"] for v in snapshot["values"]}
    assert len(values)==13 and values["InstallLocation"]==str(game)
    assert values["UninstallString"]==f'"{game}\\{UNINSTALL}"'
    assert values["DisplayName"]==f'Unofficial Medieval Patch [{identity["registration_key"][24:32]}] (GOG - {game})'
    assert values["QuietUninstallString"]==values["UninstallString"]+" /S"
    assert values["OwnerSid"]==identity["owner_sid"] and values["InstallationId"]==receipt["installation_id"]

@pytest.mark.parametrize("bad",[False,True])
def test_c_archive_record_validation(state_exe,tmp_path,receipt_fixture,bad):
    game,_,identity=receipt_fixture
    archive=seal({"schema":"unofficial-medieval-patch-recovery-v1","reason":"Changed managed files","created_utc":"2026-10-02T00:00:00Z",
        "target":identity["target"],"files":[{"file":"legacy-original-D3D9.dll","source_relative":STATE+"/originals/D3D9.dll","length":42,"sha256":"A"*64}]})
    if bad:archive["files"][0]["file"]="name\0foreign";seal(archive)
    path=tmp_path/"archive.json";path.write_bytes(canonical(archive));result=invoke(state_exe,["--archive",game,path])
    assert (result.returncode==0)==(not bad),result.stdout
    if not bad:assert result.stdout==path.read_bytes()

def test_c_state_allocation_failures_preserve_errors_and_release_handles(state_exe,tmp_path,receipt_fixture):
    game,receipt,_=receipt_fixture;path=tmp_path/"receipt.json";path.write_bytes(canonical(receipt))
    result=invoke(state_exe,["--oom-state",game,path]);(tmp_path/"allocation.log").write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,result.stdout
    assert b"failed=0" in result.stdout

def test_c_registry_distinct_unicode_names_are_preserved(state_exe,tmp_path,isolated_registry):
    # Windows ordinal comparison keeps dotless i distinct from the ASCII owned name.
    root,path,create=isolated_registry
    with create(path()) as key:winreg.SetValueEx(key,"NoModıfy",0,winreg.REG_DWORD,1)
    target=tmp_path/"absent.json";target.write_bytes(canonical({"exists":False,"values":[],"subkeys":0}))
    result=invoke(state_exe,["--live-set",root,"Registry32","modern",target])
    assert result.returncode==2 and result.stdout.strip()==b"ERROR registry_cleanup_conflict",result.stdout
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path()) as key:assert winreg.QueryValueEx(key,"NoModıfy")[0]==1

def test_c_state_pe32_import_contract(state_exe):
    import pefile
    with pefile.PE(str(state_exe)) as pe:
        assert pe.FILE_HEADER.Machine==0x14c and pe.OPTIONAL_HEADER.Magic==0x10b
        assert (pe.OPTIONAL_HEADER.MajorSubsystemVersion,pe.OPTIONAL_HEADER.MinorSubsystemVersion)==(5,1)
        assert not pe.OPTIONAL_HEADER.DATA_DIRECTORY[14].VirtualAddress
        imports={d.dll.decode().lower():[i.name.decode() for i in d.imports if i.name] for d in pe.DIRECTORY_ENTRY_IMPORT}
        assert set(imports)<={"kernel32.dll","advapi32.dll","msvcrt.dll"}
        assert "RegDeleteKeyExW" not in imports.get("advapi32.dll",[])
        assert not {"GetNativeSystemInfo","IsWow64Process","CompareStringOrdinal","LCMapStringEx"}.intersection(imports["kernel32.dll"])
    (state_exe.parent/"imports.json").write_text(json.dumps(imports,indent=2),encoding="utf-8")

def test_c_registry_write_codec_and_allocation_cleanup(state_exe,tmp_path,isolated_registry):
    root,path,create=isolated_registry
    with create(path()) as key:winreg.SetValueEx(key,"Personal",0,winreg.REG_SZ,"a\0z")
    snapshot={"exists":True,"subkeys":0,"values":[{"name":"Personal","kind":"String","value":"a\0z"},
        {"name":"NoModify","kind":"DWord","value":-2**31},{"name":"NoRepair","kind":"QWord","value":-2**63},
        {"name":"DisplayName","kind":"MultiString","value":["one","😀"]},{"name":"Publisher","kind":"String","value":"b\0y"}]}
    target=tmp_path/"typed.json";target.write_bytes(canonical(snapshot))
    result=invoke(state_exe,["--live-set",root,"Registry32","modern",target]);assert result.returncode==0,result.stdout
    result=invoke(state_exe,["--live-read",root,"Registry32","modern"]);assert result.returncode==0,result.stdout
    values={v["name"]:v for v in json.loads(result.stdout)["values"]}
    assert values=={v["name"]:v for v in snapshot["values"]}
    result=invoke(state_exe,["--oom-registry",root,target]);(tmp_path/"allocation.log").write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,result.stdout
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path()) as key:
        assert raw_registry_value(key,"Personal")== (winreg.REG_SZ,"a\0z\0".encode("utf-16-le"))

@pytest.mark.parametrize("native32",[False,True])
def test_c_legacy_registry_enumeration_does_not_adopt_native32_alias_twice(state_exe,tmp_path,receipt_fixture,isolated_registry,native32):
    root,path,create=isolated_registry;game,receipt,identity=receipt_fixture
    for view in ("32","64"):
        legacy=path(view).rsplit("\\",1)[0]+r"\Unofficial Medieval Total War Patch"
        with create(legacy) as key:
            for name,value in {"InstallLocation":str(game),"UninstallString":f'"{game}\\{STATE}\\Uninstall.exe"',
                "Publisher":"Louie Woolger","DisplayName":"Unofficial Medieval: Total War Patch","Personal":"keep"}.items():
                winreg.SetValueEx(key,name,0,winreg.REG_SZ,value)
    result=invoke(state_exe,["--legacy-enumerate",root,game,"native32" if native32 else "modern"])
    assert result.returncode==0,result.stdout
    found=json.loads(result.stdout);assert len(found)==(1 if native32 else 2)
    assert [v["view"] for v in found]==(["Registry32"] if native32 else ["Registry32","Registry64"])
    assert all(v["after"]["values"]==[{"name":"Personal","kind":"String","value":"keep"}] for v in found)

@pytest.mark.parametrize("duplicate",[False,True])
def test_c_historical_registry64_label_on_native32_is_safe(state_exe,tmp_path,receipt_fixture,duplicate):
    game,receipt,identity=receipt_fixture;journal=journal_for(receipt,identity)
    before={"exists":True,"subkeys":0,"values":[{"name":k,"kind":"String","value":v} for k,v in {
        "InstallLocation":str(game),"UninstallString":f'"{game}\\{STATE}\\Uninstall.exe"',"Publisher":"Louie Woolger",
        "DisplayName":"Unofficial Medieval: Total War Patch"}.items()]}
    action={"kind":"registry","hive":"LocalMachine","view":"Registry64","name":"Unofficial Medieval Total War Patch",
        "before":before,"after":{"exists":False,"values":[],"subkeys":0}}
    journal["actions"].append(action)
    if duplicate:journal["actions"].append(dict(action,view="Registry32"))
    seal(journal);path=tmp_path/"journal.json";path.write_bytes(canonical(journal))
    result=invoke(state_exe,["--journal",game,path,"native32-schema"])
    assert (result.returncode==0)==(not duplicate),result.stdout

@pytest.mark.parametrize("field,value,code",[("owner_sid","S-1-5-999","wrong_account"),
    ("directory_identity","00000000:0000000000000001","installation_moved")])
def test_c_journal_keeps_the_first_retained_receipt_error(state_exe,tmp_path,receipt_fixture,field,value,code):
    game,receipt,identity=receipt_fixture;journal=journal_for(receipt,identity)
    journal["install_receipt"][field]=value;seal(journal["install_receipt"]);seal(journal)
    path=tmp_path/"retained-error.json";path.write_bytes(canonical(journal))
    result=invoke(state_exe,["--journal",game,path])
    assert result.returncode==2 and result.stdout.strip()==f"ERROR {code}".encode(),result.stdout


@pytest.mark.parametrize("location_case",["exact","one_separator","many_separators","sibling","child","foreign"])
def test_c_legacy_discovery_and_journal_share_location_binding(state_exe,tmp_path,receipt_fixture,isolated_registry,location_case):
    root,path,create=isolated_registry;game,receipt,identity=receipt_fixture
    location={"exact":str(game),"one_separator":str(game)+"\\","many_separators":str(game)+"\\"*4,
        "sibling":str(game)+" sibling\\","child":str(game)+"\\child\\","foreign":"F:\\Other\\"}[location_case]
    accepted=location_case in ("exact","one_separator","many_separators")
    name="Unofficial Medieval Total War Patch";legacy=path().rsplit("\\",1)[0]+"\\"+name
    values={"InstallLocation":location,"UninstallString":f'"{game}\\{STATE}\\Uninstall.exe"',
        "Publisher":"Louie Woolger","DisplayName":"Unofficial Medieval: Total War Patch","Personal":"keep"}
    with create(legacy) as key:
        for key_name,value in values.items():winreg.SetValueEx(key,key_name,0,winreg.REG_SZ,value)
    result=invoke(state_exe,["--legacy-enumerate",root,game,"modern"])
    assert result.returncode==0,result.stdout
    (tmp_path/"discovery.json").write_bytes(result.stdout)
    found=json.loads(result.stdout);assert len(found)==int(accepted),found
    if accepted:
        # Feed the actual discovered snapshot into the persisted recovery format.
        action=dict(kind="registry",**found[0])
        assert next(v["value"] for v in action["before"]["values"] if v["name"]=="InstallLocation")==location
    else:
        action={"kind":"registry","hive":"LocalMachine","view":"Registry32","name":name,
            "before":{"exists":True,"subkeys":0,"values":[{"name":k,"kind":"String","value":v} for k,v in values.items()]},
            "after":{"exists":True,"subkeys":0,"values":[{"name":"Personal","kind":"String","value":"keep"}]}}
    journal=journal_for(receipt,identity);journal["actions"].append(action);seal(journal)
    journal_path=tmp_path/"legacy-journal.json";journal_path.write_bytes(canonical(journal))
    result=invoke(state_exe,["--journal",game,journal_path]);(tmp_path/"validation.log").write_bytes(result.stdout+result.stderr)
    assert (result.returncode==0)==accepted,result.stdout
    if accepted:assert result.stdout==journal_path.read_bytes()
    else:assert result.stdout.strip()==b"ERROR receipt_invalid"
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,legacy) as key:
        assert raw_registry_value(key,"InstallLocation")== (winreg.REG_SZ,(location+"\0").encode("utf-16-le"))


@pytest.mark.parametrize("missing",["empty","personal_only","ProductId","OwnerSid","InstallationId","InstallLocation","UninstallString"])
def test_c_journal_installed_registration_requires_identity(state_exe,tmp_path,receipt_fixture,missing):
    game,receipt,identity=receipt_fixture;journal=journal_for(receipt,identity);action=journal["actions"][-1]
    if missing=="empty":action["after"]["values"]=[]
    elif missing=="personal_only":
        personal={"name":"Personal","kind":"String","value":"keep"}
        action["before"]={"exists":True,"subkeys":0,"values":[personal.copy()]}
        action["after"]["values"]=[personal.copy()]
    else:action["after"]["values"]=[v for v in action["after"]["values"] if v["name"]!=missing]
    seal(journal);path=tmp_path/"missing-identity.json";path.write_bytes(canonical(journal))
    result=invoke(state_exe,["--journal",game,path]);(tmp_path/"validation.log").write_bytes(result.stdout+result.stderr)
    assert result.returncode==2 and result.stdout.strip()==b"ERROR registry_identity_conflict",result.stdout


@pytest.mark.parametrize("before_kind",["absent","empty","personal_only"])
def test_c_journal_current_registration_keeps_permissive_before_state(state_exe,tmp_path,receipt_fixture,before_kind):
    game,receipt,identity=receipt_fixture;journal=journal_for(receipt,identity);action=journal["actions"][-1]
    path=tmp_path/"receipt.json";path.write_bytes(canonical(receipt))
    result=invoke(state_exe,["--installed-registry",game,path]);assert result.returncode==0,result.stdout
    action["after"]=json.loads(result.stdout)
    if before_kind!="absent":action["before"]["exists"]=True
    if before_kind=="personal_only":
        personal={"name":"Personal","kind":"String","value":"keep"}
        action["before"]["values"]=[personal.copy()];action["before"]["subkeys"]=1
        action["after"]["values"].append(personal.copy());action["after"]["subkeys"]=1
    seal(journal);path=tmp_path/"installed-journal.json";path.write_bytes(canonical(journal))
    result=invoke(state_exe,["--journal",game,path]);assert result.returncode==0 and result.stdout==path.read_bytes(),result.stdout


@pytest.mark.parametrize("keep_personal",[False,True])
def test_c_journal_removed_registration_keeps_absent_and_personal_states(state_exe,tmp_path,receipt_fixture,keep_personal):
    game,receipt,identity=receipt_fixture;journal=journal_for(receipt,identity)
    journal["operation"]="restore";journal["removal_receipt"]=receipt;journal["install_receipt"]=None
    for action in journal["actions"]:
        if action["kind"]=="file":
            action["before"]=action["after"].copy()
            if action["relative"] in PAYLOAD_NAMES:
                record=receipt["files"][action["relative"]]
                action["after"]={"exists":record["existed"],"sha256":record["original_sha256"],"length":record["original_length"]}
    action=journal["actions"][-1];action["before"]=action["after"]
    action["after"]={"exists":keep_personal,"subkeys":int(keep_personal),"values":[]}
    if keep_personal:
        personal={"name":"Personal","kind":"String","value":"keep"}
        action["before"]["values"].append(personal.copy());action["before"]["subkeys"]=1
        action["after"]["values"].append(personal.copy())
    seal(journal);path=tmp_path/"removed-journal.json";path.write_bytes(canonical(journal))
    result=invoke(state_exe,["--journal",game,path]);assert result.returncode==0 and result.stdout==path.read_bytes(),result.stdout
