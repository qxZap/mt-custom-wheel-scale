// Custom Wheel Scale -- wheel radius and width tuning, and any wheel on any vehicle, as
// plugins\mt_plugin_CustomWheelScale.dll.
//
// Range: the game keeps one table of tuning params, {default, min, max, step}, 0.80 - 1.20
// for both. The garage spin box is built from it and every clamp reads it, so raising
// the rows once widens the control and keeps the game from pulling a value back. Vanilla
// checks the numbers first, so a table that moved is left alone.
//
// Width: the game has WheelWidthScale (param 1, per axle), but the screen only lists
// radius lines. Its one call to the line builder is redirected, so each radius line is
// followed by a width line; the builder takes the param as an argument and the game
// supplies the label, range and saving.
//
// Any wheel: every row with a wheel mesh gets its vehicle gates cleared in the loaded
// tables, whichever mod's they are, and its empty mesh slots filled from the ones it has
// (an axle is offered only rows that fill the slot it asks for). An override channel would sit above Wheel Works and
// put vanilla meshes back on its rows. RequiredGarageFlags stays: that is which shop sells
// a wheel, not what it fits.
//
// Tunings of every vehicle are logged on change, so a clamp shows up.
//
// Settings: Binaries\Win64\custom_wheel_scale.ini, [WheelScale] Min / Max (0.1 / 4.0, monster trucks),
// WidthMin / WidthMax (0.1 / 3.0), Step (0.01, as vanilla).
//
// shortcut: single player / host -- on a client the server owns Net_Tunings and its own
// table, so a vanilla server keeps 0.80 - 1.20.

#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "../include/mt_plugin.hpp"

using namespace mt;

struct Ref { int index; uintptr_t ptr; };
static bool alive(const Ref& r) { return r.ptr && api->object(r.index) == r.ptr; }

static uintptr_t cVeh;
static int oId = -1, oTunings = -1, tParam = -1, tTarget = -1, tValue = -1, tSize = 0;

static uintptr_t cTable, sPart;
static int oGates[6], oWheelMesh[2];  // VehicleRowGameplayTagQuery's two arrays are in oGates
// Wheel mesh slots: Left, Right, DRWLeft, DRWRight, RearLeft, RearRight, Quad. Each axle asks for one kind (a dual rear
// for DRW, a quad for Quad) and the list offers only rows that fill it -- vanilla fills Left/Right alone, so a vehicle
// asking for another slot saw only rows that fill every slot (the pack wheels, 2026-11-08). WheelWorkshop's
// make_universal (8d47b3206) did this in the tables up to 0.7.19; 0.7.20 ships vanilla Wheels as it is.
static int oSlot[7];
static const int STAND_IN[5][4] = {{0, 2, 4, 6}, {1, 3, 5, 6}, {2, 0, 4, 6}, {3, 1, 5, 6}, {6, 0, 2, 4}};  // target, donors in order

static std::mutex lock;
static std::vector<Ref> vehicles;
static std::map<uintptr_t, std::string> lastLog;

static std::string tunings(uintptr_t v) {
    uintptr_t d = get<uintptr_t>(v + oTunings); int n = get<int>(v + oTunings + 8);
    std::string out;
    char b[64];
    for (int i = 0; i < n && i < 32; i++) {
        uintptr_t e = d + (uintptr_t)i * tSize;
        sprintf_s(b, "%s%s#%d=%.3f", i ? " " : "", get<uint8_t>(e + tParam) == 0 ? "Radius" : "Width",
                  get<int>(e + tTarget), get<float>(e + tValue));
        out += b;
    }
    return out;
}

static void tick(void*) {
    std::lock_guard<std::mutex> g(lock);
    for (auto& r : vehicles) {
        if (!alive(r) || get<int>(r.ptr + oTunings + 8) == 0) continue;
        std::string t = tunings(r.ptr);
        if (lastLog[r.ptr] == t) continue;
        lastLog[r.ptr] = t;
        MT_LOG("tunings %s: %s", objName(classOf(r.ptr)).c_str(), t.c_str());
        // The log restarts with the game. This one keeps going, to compare a car across sessions.
        if (FILE* f = loggingOn() ? _wfopen((gameDir() + L"plugins\\logs\\CustomWheelScale\\wheel_history.log").c_str(), L"a") : nullptr) {
            SYSTEMTIME now;
            GetLocalTime(&now);
            long long id = oId >= 0 ? get<long long>(r.ptr + oId) : 0;
            fprintf(f, "%02d-%02d %02d:%02d:%02d %s #%lld %s\n", now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, objName(classOf(r.ptr)).c_str(), id, t.c_str());
            fclose(f);
        }
    }
}

// The table is a TMap<param, row>, 0xE0 a slot, built on first use by its getter.
static const int ROW_SIZE = 0xE0, ROW_MIN = 0x28, ROW_MAX = 0x2C, ROW_STEP = 0x30;
static uintptr_t (*paramTable)();
static float range[2][2], step;

static void raiseRange(void*) {
    uintptr_t rows = get<uintptr_t>(paramTable());
    for (int i = 0; i < 2; i++) {
        uintptr_t r = rows + (uintptr_t)i * ROW_SIZE;
        float lo = get<float>(r + ROW_MIN), hi = get<float>(r + ROW_MAX);
        if (get<uint8_t>(r) != i || fabsf(lo - 0.8f) > 1e-3f || fabsf(hi - 1.2f) > 1e-3f) {
            MT_LOG("range: param %d is %g - %g, not vanilla -- left alone", i, lo, hi);
            continue;
        }
        wr(r + ROW_MIN, &range[i][0], 4); wr(r + ROW_MAX, &range[i][1], 4); wr(r + ROW_STEP, &step, 4);
        MT_LOG("range: param %d %g - %g, step %g", i, range[i][0], range[i][1], step);
    }
}

static bool findTable() {
    // push rbx; sub rsp,30h; movaps [rsp+20h],xmm6; movaps xmm6,xmm1; movzx ebx,cl; call table -- the clamp
    auto hits = find_sig("40 53 48 83 EC 30 0F 29 74 24 20 0F 28 F1 0F B6 D9 E8");
    if (hits.size() != 1) { MT_LOG("range: %d matches -- tuning stays 0.80 - 1.20", (int)hits.size()); return false; }
    uintptr_t call = hits[0] + 17;
    paramTable = (uintptr_t (*)())(call + 5 + get<int>(call + 1));
    return true;
}

using LineFn = void (*)(uintptr_t screen, uint8_t param, int index, int target);
static LineFn buildLine;

static void bothLines(uintptr_t screen, uint8_t param, int index, int target) {
    buildLine(screen, param, index, target);
    if (param == 0) buildLine(screen, 1, index, target);
}

// A call reaches +-2 GB and this dll loads further away, so it goes through a stub by the exe.
static void* stubNear(uintptr_t base, void* to) {
    for (uintptr_t p = (base & ~(uintptr_t)0xFFFF) - 0x10000; base - p < 0x70000000; p -= 0x10000) {
        auto s = (uint8_t*)VirtualAlloc((void*)p, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!s) continue;
        const uint8_t jmp[] = {0xFF, 0x25, 0, 0, 0, 0};  // jmp [rip]
        memcpy(s, jmp, sizeof jmp);
        memcpy(s + sizeof jmp, &to, sizeof to);
        return s;
    }
    return nullptr;
}

static void addWidthLines() {
    // mov r9d,[rbx+8]; mov rcx,rsi; mov r8d,[rbx+4]; movzx edx,byte [rbx]; call builder; add rbx,0Ch
    auto hits = find_sig("44 8B 4B 08 48 8B CE 44 8B 43 04 0F B6 13 E8 ?? ?? ?? ?? 48 83 C3 0C");
    if (hits.size() != 1) { MT_LOG("width: %d matches -- no width line", (int)hits.size()); return; }
    uintptr_t call = hits[0] + 14;
    buildLine = (LineFn)(call + 5 + get<int>(call + 1));
    void* stub = stubNear(api->module_base(), (void*)bothLines);
    if (!stub) { MT_LOG("width: no memory near the exe -- no width line"); return; }
    int rel = (int)((uintptr_t)stub - (call + 5));
    bool ok = api->patch_bytes(call + 1, &rel, 4);
    MT_LOG("width: %s at %p", ok ? "line added" : "FAILED to write", (void*)call);
}

// Emptying an array in place: Num goes to 0 and the memory stays with the row.
static int unlockTable(uintptr_t t) {
    int rs = -1;
    for (int x = 0x28; x <= 0x48 && rs < 0; x += 8)
        if (get<uintptr_t>(t + x) == sPart) rs = x;
    if (rs < 0) return 0;
    uintptr_t el = get<uintptr_t>(t + rs + 8); int num = get<int>(t + rs + 16), done = 0, zero = 0;
    for (int j = 0; j < num; j++) {  // TSetElement: FName, uint8*, hash links = 24 bytes
        uintptr_t row = get<uintptr_t>(el + 24ull * j + 8);
        if (!row) continue;
        bool any = false;
        for (int o : oSlot) any = any || get<uintptr_t>(row + o);
        if (!any) continue;
        bool hit = false;
        for (int o : oGates)
            if (get<int>(row + o + 8) > 0) { wr(row + o + 8, &zero, 4); hit = true; }
        for (auto& si : STAND_IN) {  // Rear stays alone: on the rows that use it it is a real position, not a gap
            if (get<uintptr_t>(row + oSlot[si[0]])) continue;
            for (int k = 1; k < 4; k++) {
                uintptr_t m = get<uintptr_t>(row + oSlot[si[k]]);
                if (m) { wr(row + oSlot[si[0]], &m, 8); hit = true; break; }  // the row already holds m: GC-safe
            }
        }
        done += hit;
    }
    return done;
}

static bool findGates() {
    cTable = api->find_object("Class", "DataTable", "/Script/Engine");
    sPart = api->find_object("ScriptStruct", "VehiclePartRow", "/Script/MotorTown");
    uintptr_t sQuery = api->find_object("ScriptStruct", "GameplayTagQuery", "/Script/GameplayTags");
    uintptr_t sWheel = api->find_object("ScriptStruct", "MTVehiclePartWheel", "/Script/MotorTown");
    int query = off(sPart, "VehicleRowGameplayTagQuery"), wheel = off(sPart, "Wheel");
    int all[] = {off(sPart, "VehicleTypes"), off(sPart, "TruckClasses"), off(sPart, "VehicleKeys"), off(sPart, "OverrideAllowedVehicleKeys"),
                 off(sQuery, "TagDictionary"), off(sQuery, "QueryTokenStream"), off(sWheel, "LeftWheelMesh"), off(sWheel, "QuadWheelMesh")};
    bool ok = cTable && sPart && query >= 0 && wheel >= 0;
    for (int o : all) ok = ok && o >= 0;
    const char* slots[] = {"LeftWheelMesh", "RightWheelMesh", "DRWLeftWheelMesh", "DRWRightWheelMesh", "RearLeftWheelMesh", "RearRightWheelMesh", "QuadWheelMesh"};
    for (int i = 0; i < 7; i++) { oSlot[i] = off(sWheel, slots[i]); ok = ok && oSlot[i] >= 0; oSlot[i] += wheel; }
    if (!ok) { MT_LOG("wheels: row layout not found -- gates stay"); return false; }
    for (int i = 0; i < 6; i++) oGates[i] = all[i] + (i >= 4 ? query : 0);
    oWheelMesh[0] = wheel + all[6]; oWheelMesh[1] = wheel + all[7];
    return true;
}

static DWORD WINAPI run(LPVOID) {
    MT_LOG("Custom Wheel Scale loaded");
    addWidthLines();
    struct { const wchar_t* key; double v; } cfg[] = {{L"Min", 0.1}, {L"Max", 4.0}, {L"WidthMin", 0.1}, {L"WidthMax", 3.0}, {L"Step", 0.01}};
    for (auto& c : cfg) {
        c.v = iniNum(L"custom_wheel_scale.ini", L"WheelScale", c.key, c.v);
        MT_LOG("setting %ls = %g", c.key, c.v);
    }
    for (int i = 0; i < 2; i++) { range[i][0] = (float)cfg[i * 2].v; range[i][1] = (float)cfg[i * 2 + 1].v; }
    step = (float)cfg[4].v;
    bool sane = step > 0;
    for (auto& r : range) sane = sane && r[0] > 0 && r[0] < 1 && r[1] > 1;
    if (!sane) { MT_LOG("FAILED: need 0 < Min < 1 < Max and Step > 0 -- tuning stays 0.80 - 1.20"); return 0; }

    if (!api->wait_engine(INFINITE)) { MT_LOG("FAILED: no engine"); return 0; }
    if (findTable()) api->run_on_game_thread(raiseRange, nullptr);

    cVeh = api->find_object("Class", "MTVehicle", "/Script/MotorTown");
    uintptr_t sEntry = api->find_object("ScriptStruct", "MTTuningEntry", "/Script/MotorTown");
    oTunings = off(cVeh, "Net_Tunings");
    oId = off(cVeh, "Net_VehicleId");
    tParam = off(sEntry, "Param"); tTarget = off(sEntry, "TargetId"); tValue = off(sEntry, "Value");
    tSize = sEntry ? get<int>(sEntry + L().struct_child_props + 8) : 0;  // PropertiesSize follows ChildProperties
    if (!cVeh || oTunings < 0 || tSize <= 0 || tValue < 0) { MT_LOG("no tuning layout -- tunings not logged"); return 0; }
    bool gates = findGates();
    for (int beat = 0;; beat++) {
        bool sweep = gates && beat % 30 == 0;  // a table can load later than the last pass
        if (!sweep && !loggingOn()) { Sleep(1000); continue; }
        int unlocked = 0;
        std::vector<Ref> v;
        for (int i = 0, n = api->num_objects(); i < n; i++) {
            uintptr_t o = api->object(i);
            if (!o || isCDO(o)) continue;
            if (api->is_a(classOf(o), cVeh)) v.push_back({i, o});
            else if (sweep && api->is_a(classOf(o), cTable)) unlocked += unlockTable(o);
        }
        if (unlocked) MT_LOG("wheels: gates cleared / empty mesh slots filled on %d rows", unlocked);
        { std::lock_guard<std::mutex> g(lock); vehicles.swap(v); }
        if (api->game_thread_ready()) api->run_on_game_thread(tick, nullptr);
        Sleep(1000);
    }
}

extern "C" __declspec(dllexport) int PluginInit(const MtApi* a) {
    if (!attach(a)) return 1;
    MoveFileW((gameDir() + L"wheel_scale.ini").c_str(), (gameDir() + L"custom_wheel_scale.ini").c_str());  // settings from before the rename
    if (iniDisabled(L"custom_wheel_scale.ini")) return 0;
    CloseHandle(CreateThread(nullptr, 0, run, nullptr, 0, nullptr));
    return 0;
}
