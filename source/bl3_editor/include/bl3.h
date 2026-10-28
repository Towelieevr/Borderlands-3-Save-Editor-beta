#pragma once
#include <stdint.h>

void      bl3_set_base(uint64_t base);
int       bl3_init(void);
uintptr_t bl3_hud(void);
uintptr_t bl3_find_object(const char* name);
uintptr_t bl3_find_function(const char* owner, const char* name);
int       bl3_fonts(uintptr_t* out, int max);
void      bl3_process_event(uintptr_t obj, uintptr_t fn, void* parms);
int       bl3_code_level(const char* code);
int       bl3_spawn_code(const char* code, int level);
int       bl3_backpack_count(void);
int         bl3_sdu_find(uintptr_t* out, int max);
const char* bl3_object_name(uintptr_t o);
int         bl3_sdu_level(uintptr_t sdu);
int         bl3_sdu_max(uintptr_t sdu);
int         bl3_sdu_add(uintptr_t sdu);
typedef struct { uintptr_t type; char name[40]; int total, unlocked; } Bl3CosType;
int         bl3_cos_scan(Bl3CosType* out, int max);
int         bl3_cos_unlock(uintptr_t type, int all);
int         bl3_get_level(int64_t* out);
int         bl3_get_xp(int64_t* out);
int         bl3_set_level(int64_t v);
uintptr_t   bl3_currency(const char* category_name);
int         bl3_currency_get(uintptr_t cat, int64_t* out);
int         bl3_currency_set(uintptr_t cat, int64_t v, int premium);
int         bl3_unlock_vehicles(void);
#include <stdio.h>
int         bl3_mem_report(char* line, size_t n);
int         bl3_pool_get(int64_t* out);
int         bl3_pool_set(int64_t mb);
