#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <orbis/libkernel.h>
#include "bl3.h"

#define GOBJECTS_OFF   0xBA6C4D0
#define GNAMES_PTR_OFF 0xB964E10
#define O_FLAGS 0x08
#define O_CLASS 0x10
#define O_NAME  0x18
#define O_OUTER 0x20
#define S_SUPER 0x30
#define PE_SLOT (0x210 / 8)
#define PC_MYHUD 0x518
#define PC_PAWN  0x488
#define CHAR_INVENTORY 0xE98
#define INV_CAPACITY_USED 0x230
#define RF_TEMPLATES 0x30

#define FN_SERIAL_FROM_BASE64 0x549DB90
#define FN_INITDATA_FROM_SERIAL 0x5498B10
#define FN_FREE_TARRAY   0x81C20
#define FN_FREE_PARTS    0xC74DB0
#define FN_FREE_GENERIC  0xC33870
#define FN_FREE_CUSTOM   0xC74CD0

typedef struct { uintptr_t* chunks; void* pre; int32_t max, num, max_chunks, num_chunks; } ObjArray;
typedef struct { uintptr_t chunks[128]; int32_t num, num_chunks; } NameArray;
typedef struct { uint16_t* data; int32_t count, max; } FString;
typedef struct { uint8_t state; uint8_t pad[7]; uintptr_t data; int32_t count, max; int32_t extra, pad2; } GbxSerial;
typedef struct {
    int32_t game_stage, pad0;
    uintptr_t inventory_data, balance_data, manufacturer;
    uint8_t part_list[0x10], generic_parts[0x10], additional[0x10], customization[0x10];
    uint8_t reroll, dropped_from_ai, pad1[6];
} InitData;

typedef void (*ProcessEvent_t)(void* self, void* fn, void* parms);
typedef void (*Fn2)(void* a, const void* b);
typedef void (*Fn1)(void* a);

static uint64_t g_base;
static uintptr_t g_pc, g_pawn, g_hud;

void bl3_set_base(uint64_t base) { g_base = base; }

static ObjArray* objarray(void) { return (ObjArray*)(g_base + GOBJECTS_OFF); }
static NameArray* names(void) { return *(NameArray**)(g_base + GNAMES_PTR_OFF); }

static const char* name_str(int32_t i) {
    NameArray* n = names();
    if (!n || i < 0 || i >= n->num) return "";
    uintptr_t chunk = n->chunks[i / 16384];
    if (!chunk) return "";
    uintptr_t e = ((uintptr_t*)chunk)[i % 16384];
    return e ? (const char*)(e + 0x10) : "";
}
static int32_t name_index(const char* s) {
    NameArray* n = names();
    if (!n) return -1;
    for (int32_t i = 0; i < n->num; i++) {
        uintptr_t chunk = n->chunks[i / 16384];
        if (!chunk) { i += 16383; continue; }
        uintptr_t e = ((uintptr_t*)chunk)[i % 16384];
        if (e && strcmp((const char*)(e + 0x10), s) == 0) return i;
    }
    return -1;
}
static uintptr_t obj_at(int32_t i) {
    ObjArray* a = objarray();
    uintptr_t chunk = a->chunks[i / 65536];
    return chunk ? *(uintptr_t*)(chunk + (i % 65536) * 0x18) : 0;
}
static inline uintptr_t obj_class(uintptr_t o) { return *(uintptr_t*)(o + O_CLASS); }
static inline int32_t obj_name(uintptr_t o) { return *(int32_t*)(o + O_NAME); }
static inline uintptr_t obj_outer(uintptr_t o) { return *(uintptr_t*)(o + O_OUTER); }

static bool is_a(uintptr_t o, int32_t cls_name) {
    for (uintptr_t c = obj_class(o); c; c = *(uintptr_t*)(c + S_SUPER))
        if (obj_name(c) == cls_name) return true;
    return false;
}

uintptr_t bl3_find_object(const char* name) {
    int32_t nn = name_index(name);
    if (nn < 0) return 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num; i++) {
        uintptr_t o = obj_at(i);
        if (o && obj_name(o) == nn) return o;
    }
    return 0;
}

uintptr_t bl3_find_function(const char* owner, const char* name) {
    int32_t no = name_index(owner), nn = name_index(name), nf = name_index("Function");
    if (no < 0 || nn < 0 || nf < 0) return 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num; i++) {
        uintptr_t o = obj_at(i);
        if (!o || obj_name(o) != nn || !obj_class(o) || obj_name(obj_class(o)) != nf) continue;
        uintptr_t out = obj_outer(o);
        if (out && obj_name(out) == no) return o;
    }
    return 0;
}

int bl3_fonts(uintptr_t* out, int max) {
    static int32_t n_font = -1;
    if (n_font < 0) n_font = name_index("Font");
    if (n_font < 0) return 0;
    ObjArray* a = objarray();
    int n = 0;
    for (int32_t i = 0; i < a->num && n < max; i++) {
        uintptr_t o = obj_at(i);
        if (!o || !obj_class(o) || (*(uint32_t*)(o + O_FLAGS) & RF_TEMPLATES)) continue;
        if (obj_name(obj_class(o)) == n_font) out[n++] = o;
    }
    return n;
}

void bl3_process_event(uintptr_t obj, uintptr_t fn, void* parms) {
    ProcessEvent_t f = (ProcessEvent_t)(*(void***)obj)[PE_SLOT];
    f((void*)obj, (void*)fn, parms);
}

int bl3_init(void) {
    if (!g_base || !names() || objarray()->num <= 0) return -1;
    static int32_t n_pc = -1;
    if (n_pc < 0) n_pc = name_index("PlayerController");
    if (n_pc < 0) return -1;
    if (g_pc && g_hud && *(uintptr_t*)(g_pc + PC_MYHUD) == g_hud && *(uintptr_t*)(g_pc + PC_PAWN)) {
        g_pawn = *(uintptr_t*)(g_pc + PC_PAWN);
        return 0;
    }
    g_pc = g_pawn = g_hud = 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num; i++) {
        uintptr_t o = obj_at(i);
        if (!o || !obj_class(o) || (*(uint32_t*)(o + O_FLAGS) & RF_TEMPLATES)) continue;
        if (!is_a(o, n_pc)) continue;
        uintptr_t hud = *(uintptr_t*)(o + PC_MYHUD), pawn = *(uintptr_t*)(o + PC_PAWN);
        if (hud && pawn) { g_pc = o; g_hud = hud; g_pawn = pawn; return 0; }
    }
    return -1;
}

uintptr_t bl3_hud(void) { return g_hud; }

static uintptr_t backpack(void) { return g_pawn ? *(uintptr_t*)(g_pawn + CHAR_INVENTORY) : 0; }

int bl3_backpack_count(void) {
    uintptr_t inv = backpack();
    return inv ? *(int32_t*)(inv + INV_CAPACITY_USED) : -1;
}

static uint16_t g_wcode[256];

static int decode(const char* code, InitData* out) {
    memset(out, 0, sizeof(*out));
    const char* p = strstr(code, "BL3(");
    if (!p) return -1;
    p += 4;
    int n = 0;
    while (p[n] && p[n] != ')' && n < 255) { g_wcode[n] = (uint8_t)p[n]; n++; }
    if (p[n] != ')' || n == 0 || (n % 4) != 0) return -1;
    g_wcode[n] = 0;
    FString b64 = {g_wcode, n + 1, n + 1};
    GbxSerial serial; memset(&serial, 0, sizeof(serial));
    ((Fn2)(g_base + FN_SERIAL_FROM_BASE64))(&serial, &b64);
    if (!serial.data || serial.count <= 0) { if (serial.data) ((Fn1)(g_base + FN_FREE_TARRAY))(&serial.data); return -2; }
    ((Fn2)(g_base + FN_INITDATA_FROM_SERIAL))(out, &serial);
    ((Fn1)(g_base + FN_FREE_TARRAY))(&serial.data);
    if (!out->balance_data || !out->inventory_data || out->game_stage <= 0) return -2;
    return 0;
}
static void free_init(InitData* d) {
    ((Fn1)(g_base + FN_FREE_PARTS))(d->part_list);
    ((Fn1)(g_base + FN_FREE_GENERIC))(d->generic_parts);
    ((Fn1)(g_base + FN_FREE_TARRAY))(d->additional);
    ((Fn1)(g_base + FN_FREE_CUSTOM))(d->customization);
}

int bl3_code_level(const char* code) {
    InitData d;
    int r = decode(code, &d);
    int lv = d.game_stage;
    free_init(&d);
    return r == 0 ? lv : r;
}

int bl3_spawn_code(const char* code, int level) {
    if (bl3_init() != 0) return -5;
    static uintptr_t fn_create, fn_add, fn_give, fn_loc, cdo_lib;
    if (!fn_create) {
        fn_create = bl3_find_function("InventoryBlueprintLibrary", "CreateInventory");
        fn_add    = bl3_find_function("InventoryListComponent", "AddItemFromPickup");
        fn_give   = bl3_find_function("InventoryItemPickup", "GiveInventoryToUser");
        fn_loc    = bl3_find_function("Actor", "K2_GetActorLocation");
        cdo_lib   = bl3_find_object("Default__InventoryBlueprintLibrary");
    }
    if (!fn_create || !fn_add || !fn_loc || !cdo_lib) return -6;

    InitData d;
    int r = decode(code, &d);
    if (r != 0) { free_init(&d); return r == -1 ? -1 : -3; }
    if (level > 0) d.game_stage = level;

    struct { float x, y, z; } loc = {0, 0, 0};
    bl3_process_event(g_pawn, fn_loc, &loc);

    struct {
        uintptr_t world_context; uint8_t create_pickup, pad[3];
        float x, y, z;
        InitData init;
        uintptr_t ret;
    } cp;
    _Static_assert(sizeof(InitData) == 0x68, "InitData size");
    _Static_assert(__builtin_offsetof(__typeof__(cp), init) == 0x18 && __builtin_offsetof(__typeof__(cp), ret) == 0x80, "CreateInventory layout");
    memset(&cp, 0, sizeof(cp));
    cp.world_context = g_pawn; cp.create_pickup = 1;
    cp.x = loc.x; cp.y = loc.y; cp.z = loc.z + 60.0f;
    memcpy(&cp.init, &d, sizeof(d));
    bl3_process_event(cdo_lib, fn_create, &cp);
    free_init(&d);
    uintptr_t pickup = cp.ret;
    if (!pickup) return -4;

    uintptr_t inv = backpack();
    int before = bl3_backpack_count();
    if (inv) {
        struct { uintptr_t pickup; uint32_t ret; uint32_t pad; } ap = {pickup, 0, 0};
        bl3_process_event(inv, fn_add, &ap);
    }
    if (bl3_backpack_count() > before) return 1;
    if (fn_give) {
        struct { uintptr_t other; uint8_t auto_equip, pad[7]; } gp = {g_pawn, 0, {0}};
        bl3_process_event(pickup, fn_give, &gp);
        if (bl3_backpack_count() > before) return 1;
    }
    return 2;
}

int bl3_sdu_find(uintptr_t* out, int max) {
    static int32_t n_sdu = -1;
    if (n_sdu < 0) n_sdu = name_index("OakSDUData");
    if (n_sdu < 0) return 0;
    int n = 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num && n < max; i++) {
        uintptr_t o = obj_at(i);
        if (!o || !obj_class(o) || (*(uint32_t*)(o + O_FLAGS) & RF_TEMPLATES)) continue;
        if (is_a(o, n_sdu)) out[n++] = o;
    }
    return n;
}

const char* bl3_object_name(uintptr_t o) { return o ? name_str(obj_name(o)) : ""; }

int bl3_sdu_level(uintptr_t sdu) {
    static uintptr_t fn;
    if (!fn) fn = bl3_find_function("OakCharacter_Player", "GetSDULevel");
    if (!fn || !sdu || bl3_init() != 0) return -1;
    struct { uintptr_t sdu; int32_t ret, pad; } p = {sdu, -1, 0};
    bl3_process_event(g_pawn, fn, &p);
    return p.ret;
}

int bl3_sdu_max(uintptr_t sdu) {
    static uintptr_t fn;
    if (!fn) fn = bl3_find_function("OakSDUData", "GetSDULevelInfo");
    if (!fn || !sdu) return -1;
    int lv = 0;
    for (int l = 1; l <= 100; l++) {
        uint8_t p[0x60]; memset(p, 0, sizeof(p));
        *(int32_t*)p = l;
        bl3_process_event(sdu, fn, p);
        if (!p[0x58]) break;
        lv = l;
    }
    return lv;
}

int bl3_sdu_add(uintptr_t sdu) {
    static uintptr_t fn;
    if (!fn) fn = bl3_find_function("OakCharacter_Player", "AddSDU");
    if (!fn || !sdu || bl3_init() != 0) return -1;
    struct { uintptr_t sdu; } p = {sdu};
    bl3_process_event(g_pawn, fn, &p);
    return bl3_sdu_level(sdu);
}

#define CUST_TYPE 0xF0
static uintptr_t g_fn_cust_add, g_fn_cust_has, g_fn_part_add, g_fn_part_has;
static int32_t g_n_custdata = -1, g_n_partdata = -1;

static bool cos_init(void) {
    if (!g_fn_cust_add) {
        g_fn_cust_add = bl3_find_function("OakPlayerController", "AddUnlockedCustomization");
        g_fn_cust_has = bl3_find_function("OakPlayerController", "IsCustomizationUnlocked");
        g_fn_part_add = bl3_find_function("OakPlayerController", "AddUnlockedInventoryCustomizationPart");
        g_fn_part_has = bl3_find_function("OakPlayerController", "IsInventoryCustomizationPartUnlocked");
        g_n_custdata = name_index("GbxCustomizationData");
        g_n_partdata = name_index("OakInventoryCustomizationPartData");
    }
    return g_fn_cust_add && g_fn_cust_has && g_fn_part_add && g_fn_part_has && g_n_custdata >= 0 && bl3_init() == 0;
}
static bool cos_unlocked(uintptr_t o, bool part) {
    struct { uintptr_t obj; uint8_t ret, pad[7]; } p = {o, 0, {0}};
    bl3_process_event(g_pc, part ? g_fn_part_has : g_fn_cust_has, &p);
    return p.ret != 0;
}

int bl3_cos_scan(Bl3CosType* out, int max) {
    if (!cos_init()) return -1;
    int n = 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num; i++) {
        uintptr_t o = obj_at(i);
        if (!o || !obj_class(o) || (*(uint32_t*)(o + O_FLAGS) & RF_TEMPLATES) || !is_a(o, g_n_custdata)) continue;
        uintptr_t type = *(uintptr_t*)(o + CUST_TYPE);
        bool part = g_n_partdata >= 0 && is_a(o, g_n_partdata);
        int k = 0;
        while (k < n && out[k].type != type) k++;
        if (k == n) {
            if (n == max) continue;
            out[n].type = type; out[n].total = out[n].unlocked = 0;
            const char* nm = type ? name_str(obj_name(type)) : "Other";
            if (strncmp(nm, "CustomizationType_", 18) == 0) nm += 18;
            int j = 0;
            for (; nm[j] && j < (int)sizeof(out[n].name) - 1; j++) out[n].name[j] = nm[j] == '_' ? ' ' : nm[j];
            out[n].name[j] = 0;
            n++;
        }
        out[k].total++;
        if (cos_unlocked(o, part)) out[k].unlocked++;
    }
    return n;
}

int bl3_cos_unlock(uintptr_t type, int all) {
    if (!cos_init()) return -1;
    int done = 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num; i++) {
        uintptr_t o = obj_at(i);
        if (!o || !obj_class(o) || (*(uint32_t*)(o + O_FLAGS) & RF_TEMPLATES) || !is_a(o, g_n_custdata)) continue;
        if (!all && *(uintptr_t*)(o + CUST_TYPE) != type) continue;
        bool part = g_n_partdata >= 0 && is_a(o, g_n_partdata);
        if (cos_unlocked(o, part)) continue;
        struct { uintptr_t obj; } p = {o};
        bl3_process_event(g_pc, part ? g_fn_part_add : g_fn_cust_add, &p);
        done++;
    }
    return done;
}

#define PBS_EXPERIENCE 0x1C4
static const int32_t g_xp_table[80] = {0, 357, 1240, 2850, 5375, 8996, 13885, 20207, 28125, 37797, 49376, 63015, 78861, 97060, 117756, 141092, 167206, 196237, 228321, 263594, 302189, 344238, 389872, 439221, 492413, 549577, 610839, 676324, 746157, 820463, 899363, 982979, 1071435, 1164849, 1263342, 1367033, 1476041, 1590482, 1710475, 1836136, 1967581, 2104926, 2248284, 2397771, 2553501, 2715586, 2884139, 3059272, 3241098, 3429727, 3625271, 3827840, 4037543, 4254491, 4478792, 4710556, 4949890, 5196903, 5451701, 5714394, 5985086, 6263885, 6550897, 6846228, 7149982, 7462266, 7783184, 8112841, 8451340, 8798786, 9155283, 9520932, 9895839, 10280104, 10673831, 11077122, 11490078, 11912802, 12345395, 12787957};
static uintptr_t g_pbs;

static uintptr_t player_balance(void) {
    if (bl3_init() != 0) return 0;
    if (g_pbs && obj_outer(g_pbs) == g_pawn) return g_pbs;
    static int32_t n_pbs = -1;
    if (n_pbs < 0) n_pbs = name_index("PlayerBalanceStateComponent");
    g_pbs = 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num && n_pbs >= 0; i++) {
        uintptr_t o = obj_at(i);
        if (!o || !obj_class(o) || (*(uint32_t*)(o + O_FLAGS) & RF_TEMPLATES) || obj_outer(o) != g_pawn) continue;
        if (is_a(o, n_pbs)) { g_pbs = o; break; }
    }
    return g_pbs;
}

int bl3_get_xp(int64_t* out) {
    uintptr_t c = player_balance();
    if (!c) return -1;
    *out = *(int32_t*)(c + PBS_EXPERIENCE);
    return 0;
}

int bl3_get_level(int64_t* out) {
    static uintptr_t fn;
    if (!fn) fn = bl3_find_function("BalanceStateComponent", "GetExperienceLevel");
    uintptr_t c = player_balance();
    if (!c || !fn) return -1;
    int32_t lv = 0;
    bl3_process_event(c, fn, &lv);
    *out = lv;
    return 0;
}

int bl3_set_level(int64_t v) {
    static uintptr_t fn;
    if (!fn) fn = bl3_find_function("PlayerBalanceStateComponent", "AddExperience");
    uintptr_t c = player_balance();
    if (!c || !fn || v < 1 || v > 80) return -1;
    int32_t target = g_xp_table[v - 1];
    for (int tries = 0; tries < 6; tries++) {
        int32_t xp = *(int32_t*)(c + PBS_EXPERIENCE);
        int64_t lv = 0; bl3_get_level(&lv);
        if (xp >= target || lv >= v) return 0;
        struct { int32_t amount; uint8_t source, type, pad[2]; } p = {target - xp, 0, 0, {0}};
        bl3_process_event(c, fn, &p);
        if (*(int32_t*)(c + PBS_EXPERIENCE) == xp) return -2;
    }
    return 0;
}

uintptr_t bl3_currency(const char* name) {
    static int32_t n_cat = -1;
    if (n_cat < 0) n_cat = name_index("InventoryCategoryData");
    if (n_cat < 0) return 0;
    ObjArray* a = objarray();
    for (int32_t i = 0; i < a->num; i++) {
        uintptr_t o = obj_at(i);
        if (!o || !obj_class(o) || (*(uint32_t*)(o + O_FLAGS) & RF_TEMPLATES) || !is_a(o, n_cat)) continue;
        if (strstr(name_str(obj_name(o)), name)) return o;
    }
    return 0;
}

int bl3_currency_get(uintptr_t cat, int64_t* out) {
    static uintptr_t fn;
    if (!fn) fn = bl3_find_function("InventoryListComponent", "GetCategoryItemQuantity");
    uintptr_t inv = backpack();
    if (!cat || !inv || !fn) return -1;
    struct { uintptr_t cat; int32_t ret, pad; } p = {cat, 0, 0};
    bl3_process_event(inv, fn, &p);
    *out = p.ret;
    return 0;
}

int bl3_currency_set(uintptr_t cat, int64_t v, int premium) {
    static uintptr_t fn_upd, fn_prem;
    if (!fn_upd) {
        fn_upd = bl3_find_function("InventoryListComponent", "AddOrUpdateCategoryOnly");
        fn_prem = bl3_find_function("OakPlayerController", "ServerUpdatePremiumCurrency");
    }
    uintptr_t inv = backpack();
    int64_t cur;
    if (!cat || !inv || !fn_upd || bl3_currency_get(cat, &cur) != 0) return -1;
    if (v > 2147483647) v = 2147483647;
    if (v < 0) v = 0;
    if (cur == v) return 0;
    struct { uintptr_t cat; int32_t qty, pad; } p = {cat, (int32_t)(v - cur), 0};
    bl3_process_event(inv, fn_upd, &p);
    int64_t now = 0; bl3_currency_get(cat, &now);
    if (now != v) { p.qty = (int32_t)v; bl3_process_event(inv, fn_upd, &p); bl3_currency_get(cat, &now); }
    if (premium && fn_prem && g_pc) {
        struct { uintptr_t cat; int32_t qty, pad; } pp = {cat, (int32_t)v, 0};
        bl3_process_event(g_pc, fn_prem, &pp);
        bl3_currency_get(cat, &now);
    }
    return now == v ? 0 : -2;
}

int bl3_unlock_vehicles(void) {
    if (bl3_init() != 0) return -1;
    void** vt = *(void***)g_pc;
    ((void (*)(void*))vt[0x1288 / 8])((void*)g_pc);
    ((void (*)(void*))vt[0x1290 / 8])((void*)g_pc);
    return 0;
}

typedef int (*AvailDmem_t)(int64_t, int64_t, size_t, int64_t*, size_t*);
typedef int (*AvailFmem_t)(size_t*);

static int cvar_int(const char* name) {
    static uintptr_t fn, lib;
    if (!fn) { fn = bl3_find_function("KismetSystemLibrary", "GetConsoleVariableIntValue"); lib = bl3_find_object("Default__KismetSystemLibrary"); }
    if (!fn || !lib || !g_pawn) return -1;
    static uint16_t w[96];
    int n = 0; while (name[n] && n < 95) { w[n] = (uint8_t)name[n]; n++; } w[n] = 0;
    struct { uintptr_t ctx; FString name; int32_t ret, pad; } p = {g_pawn, {w, n + 1, n + 1}, -1, 0};
    bl3_process_event(lib, fn, &p);
    return p.ret;
}

int bl3_mem_report(char* line, size_t n) {
    size_t total = sceKernelGetDirectMemorySize();
    int64_t phys = 0; size_t largest = 0, flex = 0;
    ((AvailDmem_t)sceKernelAvailableDirectMemorySize)(0, (int64_t)total, 0, &phys, &largest);
    ((AvailFmem_t)sceKernelAvailableFlexibleMemorySize)(&flex);
    int pool = bl3_init() == 0 ? cvar_int("r.Streaming.PoolSize") : -1;
    const int MB = 1024 * 1024;
    snprintf(line, n, "Game RAM %ld MB (largest free block %ld MB)   flexible free %ld MB   texture pool %d MB",
             (long)(total / MB), (long)(largest / MB), (long)(flex / MB), pool);
    return 0;
}

int bl3_pool_get(int64_t* out) {
    if (bl3_init() != 0) return -1;
    int v = cvar_int("r.Streaming.PoolSize");
    if (v <= 0) return -1;
    *out = v;
    return 0;
}

int bl3_pool_set(int64_t mb) {
    static uintptr_t fn, lib;
    if (!fn) { fn = bl3_find_function("KismetSystemLibrary", "ExecuteConsoleCommand"); lib = bl3_find_object("Default__KismetSystemLibrary"); }
    if (!fn || !lib || bl3_init() != 0) return -1;
    char cmd[64]; snprintf(cmd, sizeof(cmd), "r.Streaming.PoolSize %d", (int)mb);
    static uint16_t w[64];
    int n = 0; while (cmd[n] && n < 63) { w[n] = (uint8_t)cmd[n]; n++; } w[n] = 0;
    struct { uintptr_t ctx; FString cmd; uintptr_t player; } p = {g_pawn, {w, n + 1, n + 1}, g_pc};
    bl3_process_event(lib, fn, &p);
    int64_t now = 0;
    return bl3_pool_get(&now) == 0 && now == mb ? 0 : -2;
}
