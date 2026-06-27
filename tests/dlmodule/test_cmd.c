/*
 * MSH commands: real dlmodule_load → dlsym → call test
 *
 * Usage:
 *   dlmod_load <file.so>              — load module, list symbols
 *   dlmod_call <file.so> <func> <arg> — load, dlsym, call function with arg
 *   dlmod_unload <name>               — unload module by name
 */

#include <rtthread.h>
#include <dlmodule.h>
#include <dlfcn.h>

/* ── dlmod_load: dlopen + list symbols ── */
static int cmd_load(int argc, char **argv)
{
    if (argc < 2) { rt_kprintf("Usage: dlmod_load <file.so>\n"); return 0; }

    struct rt_dlmodule *mod = dlmodule_load(argv[1]);
    if (!mod) { rt_kprintf("FAILED\n"); return -1; }

    rt_kprintf("OK: %u bytes, %u symbols:\n", mod->mem_size, mod->nsym);
    for (int i = 0; i < mod->nsym; i++)
        rt_kprintf("  [%d] %-24s → 0x%p\n", i, mod->symtab[i].name, mod->symtab[i].addr);
    return 0;
}
MSH_CMD_EXPORT(cmd_load, dlmodule load + list symbols);

/* ── dlmod_call: dlopen → dlsym → call with 1 arg → show result ── */
static int cmd_call(int argc, char **argv)
{
    if (argc < 3) { rt_kprintf("Usage: dlmod_call <file.so> <func> [arg]\n"); return 0; }

    /* Load module */
    struct rt_dlmodule *mod = dlmodule_load(argv[1]);
    if (!mod) { rt_kprintf("dlmodule_load FAILED\n"); return -1; }

    /* dlsym */
    void *fn = dlsym(mod, argv[2]);
    if (!fn) {
        rt_kprintf("dlsym('%s') NOT FOUND. Available:\n", argv[2]);
        for (int i = 0; i < mod->nsym; i++)
            rt_kprintf("  %s\n", mod->symtab[i].name);
        return -1;
    }
    rt_kprintf("dlsym('%s') = 0x%p\n", argv[2], fn);

    /* Call with 1 integer arg */
    int arg = 42;
    if (argc >= 4) { arg = 0; for (char *p = argv[3]; *p; p++) arg = arg * 10 + (*p - '0'); }
    typedef int (*fn_int_t)(int);
    fn_int_t func = (fn_int_t)fn;
    int result = func(arg);

    rt_kprintf("call(%d) => %d\n", arg, result);
    return 0;
}
MSH_CMD_EXPORT(cmd_call, dlopen → dlsym → call);

/* ── dlmod_call2: dlopen → dlsym → call with 2 args ── */
static int cmd_call2(int argc, char **argv)
{
    if (argc < 4) { rt_kprintf("Usage: dlmod_call2 <file.so> <func> <arg1> <arg2>\n"); return 0; }

    struct rt_dlmodule *mod = dlmodule_load(argv[1]);
    if (!mod) { rt_kprintf("dlmodule_load FAILED\n"); return -1; }

    void *fn = dlsym(mod, argv[2]);
    if (!fn) { rt_kprintf("dlsym('%s') NOT FOUND\n", argv[2]); return -1; }
    rt_kprintf("dlsym('%s') = 0x%p\n", argv[2], fn);

    int a1 = 0, a2 = 0;
    for (char *p = argv[3]; *p; p++) a1 = a1 * 10 + (*p - '0');
    for (char *p = argv[4]; *p; p++) a2 = a2 * 10 + (*p - '0');
    typedef int (*fn_2int_t)(int, int);
    fn_2int_t func = (fn_2int_t)fn;
    int result = func(a1, a2);

    rt_kprintf("call(%d, %d) => %d\n", a1, a2, result);
    return 0;
}
MSH_CMD_EXPORT(cmd_call2, dlopen → dlsym → call(2 args));

/* ── dlmod_unload ── */
static int cmd_unload(int argc, char **argv)
{
    if (argc < 2) { rt_kprintf("Usage: dlmod_unload <name>\n"); return 0; }
    struct rt_dlmodule *mod = dlmodule_find(argv[1]);
    if (!mod) { rt_kprintf("module '%s' not found\n", argv[1]); return -1; }
    dlmodule_destroy(mod);
    rt_kprintf("unloaded '%s'\n", argv[1]);
    return 0;
}
MSH_CMD_EXPORT(cmd_unload, dlmodule unload);

/* ── cjson_demo: dlopen cJSON .so → dlsym API → call each function ── */
static int cmd_cjson_demo(int argc, char **argv)
{
    struct rt_dlmodule *mod = dlmodule_load("/mod_cjson_pure.so");
    if (!mod) { rt_kprintf("FAILED to load cjson\n"); return -1; }

    rt_kprintf("=== cJSON dlopen OK, %u symbols ===\n", mod->nsym);

    /* Step 1: dlsym cJSON_Parse */
    typedef void* (*parse_t)(const char*);
    parse_t fn_parse = dlsym(mod, "cJSON_Parse");
    if (!fn_parse) { rt_kprintf("dlsym(cJSON_Parse) FAILED\n"); return -1; }
    rt_kprintf("dlsym(cJSON_Parse) = 0x%p\n", fn_parse);

    /* Step 2: Parse a JSON string */
    const char *json = "{\"sensor\":\"temp\",\"value\":36.5,\"unit\":\"C\"}";
    void *root = fn_parse(json);
    if (!root) { rt_kprintf("cJSON_Parse FAILED\n"); return -1; }
    rt_kprintf("cJSON_Parse(\"%s\") => 0x%p\n", json, root);

    /* Step 3: dlsym cJSON_GetObjectItem */
    typedef void* (*get_t)(void*, const char*);
    get_t fn_get = dlsym(mod, "cJSON_GetObjectItem");
    if (!fn_get) { rt_kprintf("dlsym(cJSON_GetObjectItem) FAILED\n"); return -1; }
    rt_kprintf("dlsym(cJSON_GetObjectItem) = 0x%p\n", fn_get);

    /* Step 4: Extract each field */
    void *item_sensor = fn_get(root, "sensor");
    void *item_value  = fn_get(root, "value");
    void *item_unit   = fn_get(root, "unit");
    rt_kprintf("  GetObjectItem(\"sensor\") => 0x%p\n", item_sensor);
    rt_kprintf("  GetObjectItem(\"value\")  => 0x%p\n", item_value);
    rt_kprintf("  GetObjectItem(\"unit\")   => 0x%p\n", item_unit);

    /* Read string values via cJSON_IsString + ->valuestring  */
    typedef int  (*isstr_t)(void*);
    isstr_t fn_isstr = dlsym(mod, "cJSON_IsString");
    typedef double (*num_t)(void*);
    num_t fn_num = dlsym(mod, "cJSON_GetNumberValue");

    if (fn_isstr && item_sensor && fn_isstr(item_sensor))
        rt_kprintf("  sensor.string = \"%s\"\n",
            /* cJSON layout: next(8) prev(8) child(8) type(4) +pad(4) valuestring(8) = at offset 32 */
            *(char**)((char*)item_sensor + 32));
    if (fn_num && item_value)
        rt_kprintf("  value.number = %.1f\n", fn_num(item_value));
    if (fn_isstr && item_unit && fn_isstr(item_unit))
        rt_kprintf("  unit.string = \"%s\"\n",
            *(char**)((char*)item_unit + 32));

    /* Step 5: dlsym cJSON_PrintUnformatted */
    typedef char* (*print_t)(void*);
    print_t fn_print = dlsym(mod, "cJSON_PrintUnformatted");
    if (fn_print) {
        rt_kprintf("dlsym(cJSON_PrintUnformatted) = 0x%p\n", fn_print);
        char *out = fn_print(root);
        rt_kprintf("cJSON_PrintUnformatted => \"%s\"\n", out);
        rt_free(out);
    }

    /* Step 6: dlsym cJSON_Delete */
    typedef void (*del_t)(void*);
    del_t fn_del = dlsym(mod, "cJSON_Delete");
    if (fn_del) {
        rt_kprintf("dlsym(cJSON_Delete) = 0x%p\n", fn_del);
        fn_del(root);
        rt_kprintf("cJSON_Delete done\n");
    }

    rt_kprintf("=== cJSON demo PASSED ===\n");
    return 0;
}
MSH_CMD_EXPORT(cmd_cjson_demo, dlopen cJSON → dlsym API → call each function);

/* ── dlmod_full: complete test for a module ── */
static int cmd_full(int argc, char **argv)
{
    if (argc < 2) { rt_kprintf("Usage: dlmod_full <file.so>\n"); return 0; }

    struct rt_dlmodule *mod = dlmodule_load(argv[1]);
    if (!mod) { rt_kprintf("FAILED\n"); return -1; }

    rt_kprintf("╔══════════════════════════════════════╗\n");
    rt_kprintf("║  Module: %-28s ║\n", mod->parent.name);
    rt_kprintf("╠══════════════════════════════════════╣\n");
    rt_kprintf("║  base=0x%p  size=%u  syms=%u     ║\n", mod->mem_space, mod->mem_size, mod->nsym);
    rt_kprintf("╚══════════════════════════════════════╝\n");

    /* Try common function names */
    struct { const char *name; int arg1; int arg2; int is_2arg; } tests[] = {
        {"process_sample", 500, 1, 1},       /* mod_sensor2: filter index 1 = lowpass */
        {"get_filter_count", 0, 0, 0},        /* mod_sensor2: no arg, but returns count */
        {"copy_string", 0, 0, 0},             /* mod_string_util: needs string arg */
        {"get_stats", 0, 0, 0},               /* mod_string_util: returns count */
    };

    for (int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        void *fn = dlsym(mod, tests[i].name);
        if (!fn) continue;

        rt_kprintf("\n── dlsym('%s') → 0x%p ──\n", tests[i].name, fn);

        int result;
        if (tests[i].is_2arg) {
            typedef int (*f2_t)(int, int);
            result = ((f2_t)fn)(tests[i].arg1, tests[i].arg2);
            rt_kprintf("  call(%d, %d) => %d\n", tests[i].arg1, tests[i].arg2, result);
        } else if (tests[i].arg1 == 0 && tests[i].arg2 == 0) {
            typedef int (*f0_t)(void);
            result = ((f0_t)fn)();
            rt_kprintf("  call() => %d\n", result);
        } else {
            typedef int (*f1_t)(int);
            result = ((f1_t)fn)(tests[i].arg1);
            rt_kprintf("  call(%d) => %d\n", tests[i].arg1, result);
        }
    }

    rt_kprintf("\n── dlsym tests complete ──\n");
    return 0;
}
MSH_CMD_EXPORT(cmd_full, dlopen → full dlsym test);
