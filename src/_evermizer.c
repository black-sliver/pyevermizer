#include <Python.h>
#include <stdarg.h>
#include <stdio.h>

#if defined(__CLING__) /* hide evermizer definitions in cppyy */
namespace _evermizer {
#define WITH_ASSERT
#endif

/* NOTE: overwriting printf makes it impossible to run multiple mains
         concurrently without loading the module multiple times,
         but luckily it's fast. */
static PyObject *logger = NULL;
static PyObject *semaphore = NULL;
static char *stdoutbuf = NULL;
#define STDOUT_LOGGER_LEVEL "debug"
#define STDERR_LOGGER_LEVEL "error"

static int evermizer_fprintf(FILE *f, const char *fmt, ...)
{
    int res;
    char buf[1024];
    char *heap = NULL;
    va_list args;
    va_start(args, fmt);
    if (logger && (f == stdout || f == stderr)) {
        const char *level = (f==stdout) ? STDOUT_LOGGER_LEVEL : STDERR_LOGGER_LEVEL;
        /* try to print to buffer on stack */
        res = vsnprintf(buf, sizeof(buf), fmt, args);
        if (res > 0 && (size_t)res < sizeof(buf)) {
            /* buf valid */
            if (f == stdout) {
                size_t buflen = strlen(buf);
                size_t oldlen = stdoutbuf ? strlen(stdoutbuf) : 0;
                if (!oldlen && buf[buflen-1] == '\n') {
                    /* immediately print stdout if buf is empty and chunk ends in \n. optimized most-common case */
                    buf[buflen-1] = 0;
                    Py_XDECREF(PyObject_CallMethod(logger, level, "(s)", buf));
                } else {
                    /* append to stdoutbuf, see below for printing it */
                    stdoutbuf = (char*)realloc(stdoutbuf, buflen + oldlen + 1);
                    if (!stdoutbuf) {
                        res = -1;
                        goto cleanup;
                    }
                    memcpy(stdoutbuf+oldlen, buf, buflen+1);
                }
            } else {
                /* immediately print stderr chunk */
                Py_XDECREF(PyObject_CallMethod(logger, level, "(s)", buf));
            }
        } else if (res > 0) {
            /* allocate bigger buffer on heap */
            heap = (char*)malloc((size_t)res+1);
            if (heap) {
                va_end(args);
                va_start(args, fmt);
                res = vsnprintf(heap, (size_t)res+1, fmt, args);
                if (res > 0) {
                    /* heap valid */
                    if (f == stdout) {
                        if (!stdoutbuf) {
                            /* replace stdoutbuf by new chunk, see below for printing it */
                            stdoutbuf = heap;
                            heap = NULL;
                        } else {
                            /* append new chunk to stdoutbuf, see below for printing it */
                            size_t oldlen = strlen(stdoutbuf);
                            size_t heaplen = strlen(heap);
                            stdoutbuf = (char*)realloc(stdoutbuf, oldlen + heaplen + 1);
                            if (!stdoutbuf) {
                                res = -1;
                                goto cleanup;
                            }
                            memcpy(stdoutbuf + oldlen, heap, heaplen + 1);
                        }
                    } else {
                        /* immediately print stderr chunk */
                        Py_XDECREF(PyObject_CallMethod(logger, level, "(s)", heap));
                    }
                }
            }
        }
        if (f == stdout && stdoutbuf && *stdoutbuf) {
            /* print stdoutbuf if it ends in newline */
            size_t newlen = strlen(stdoutbuf);
            if (stdoutbuf[newlen-1] == '\n') {
                stdoutbuf[newlen-1] = 0;
                Py_XDECREF(PyObject_CallMethod(logger, level, "(s)", stdoutbuf));
                free(stdoutbuf);
                stdoutbuf = NULL;
            }
        }
    }
    else {
        res = vfprintf(f, fmt, args);
    }
cleanup:
    free(heap);
    va_end(args);
    if (PyErr_Occurred()) PyErr_Clear(); /* ignore errors for bad printf */
    return res;
}


#define NO_UI
#define exit(N) return N
#define die(...) do { fprintf(stderr, __VA_ARGS__); return 1; } while (0)
#define main evermizer_main
#define printf(...) fprintf(stdout, __VA_ARGS__)
#define fprintf evermizer_fprintf
#include "evermizer/main.c"
#undef printf
#undef fprintf
#undef main

#if defined(__CLING__) /* see above */
}
using namespace _evermizer;
#endif

/* types */
#include "location.h"
#include "item.h"

/* helpers */
static int
path2ansi(PyObject *stringOrPath, void* result)
{
    /* NOTE: PyUnicode_FSConverter does not work anymore on windows for fopen()
             see https://www.python.org/dev/peps/pep-0529/ */
    PyObject **out = (PyObject **) result;
    assert(stringOrPath); /* TODO: raise exception */
    assert(out); /* TODO: raise exception */
    if (Py_TYPE(stringOrPath) == &PyBytes_Type) {
        /* already bytes, assume ansi */
        *out = stringOrPath;
        Py_INCREF(stringOrPath);
    }
    else if (PyObject_HasAttrString(stringOrPath, "__fspath__")) {
        /* path */
        PyObject *str, *fspath;
        fspath = PyObject_GetAttrString(stringOrPath, "__fspath__");
        if (!fspath) return 0;
        str = PyObject_CallObject(fspath, NULL); /* to string */
        Py_DECREF(fspath);
        if (!str) return 0;
        *out = PyUnicode_EncodeLocale(str, "strict"); /* to ansi */
        Py_DECREF(str);
    } else {
        /* already string */
        *out = PyUnicode_EncodeLocale(stringOrPath, "strict"); /* to ansi */
    }
    if (!*out) return 0;
    return 1;
}

static const char hexchars[] = "0123456789ABCDEF";

/* methods */
static PyObject *
_evermizer_main(PyObject *self, PyObject *py_args)
{
    /* _evermizer.main call signature:
        src: Path, dst: Path, placement: Path, apseed: str, apslot: str, seed: int, flags: str, money: int, exp: int, switches: list[string]
    */

    /* original main signature:
          int argc, char** argv: { <exe> [flags ...] <src.sfc> [settings [seed]] }
       mapped main signature:
          15, { "evermizer", '-b", "-o", "<dst.sfc>", "--money", "<money%>", "--exp', "<exp%>",
               "--id", "<hex(32B ap seed)>[:]<hex(32B ap slot)>", "--placement", "<placement.txt>",
               [switches...,]
               "<src.sfc>", "<flags>", "<seed>" }
       TODO: split UI/argument parsing from generation in evermizer, so we don't need to call the C main
    */

    PyObject *pyres = NULL;
    PyObject *osrc, *odst, *oplacement;
    const char *ap_seed, *ap_slot;
    PyObject *oseed; /* any integer -> PyObject */
    PyObject *switches;
    const char* flags;
    uint64_t seed;
    int money, exp;
    const char *src;
    const char *dst;
    const char *placement;
    char sseed[21];
    char sexp[5];
    char smoney[5];
    char id_buf[130]; /* hex(32B):hex(32B)\0 */
    char *id_bufp = id_buf;
    PyObject *logging;

    if (!PyArg_ParseTuple(py_args, "O&O&O&ssOsiiO", path2ansi, &osrc, path2ansi, &odst, path2ansi, &oplacement,
                          &ap_seed, &ap_slot, &oseed, &flags, &money, &exp, &switches)) {
        goto error;
    }

    seed = (uint64_t)PyLong_AsUnsignedLongLong(oseed);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        pyres = PyErr_Format(PyExc_TypeError, "6th parameter 'seed' must be unsigned integer type, but got %s",
                            Py_TYPE(oseed)->tp_name);
        goto cleanup;
    }

    src = PyBytes_AS_STRING(osrc);
    dst = PyBytes_AS_STRING(odst);
    placement = PyBytes_AS_STRING(oplacement);

    snprintf(sseed, sizeof(sseed), "%" PRIx64, seed);

    if (exp > 9999) exp = 9999;
    if (exp < 0) exp = 0;
    snprintf(sexp, sizeof(sexp), "%d", exp);

    if (money > 9999) money = 9999;
    if (money < 0) money = 0;
    snprintf(smoney, sizeof(smoney), "%d", money);

    memset(id_buf, 0, 130);
    for (uint8_t i=0; i<32; i++) {
        if (!ap_seed[i]) break;
        *id_bufp++ = hexchars[((uint8_t)ap_seed[i]>>4)&0x0f];
        *id_bufp++ = hexchars[((uint8_t)ap_seed[i]>>0)&0x0f];
    }
    *id_bufp++ = ':';
    for (uint8_t i=0; i<32; i++) {
        if (!ap_slot[i]) break;
        *id_bufp++ = hexchars[((uint8_t)ap_slot[i]>>4)&0x0f];
        *id_bufp++ = hexchars[((uint8_t)ap_slot[i]>>0)&0x0f];
    }

    /* if multithreading is enabled, wait for the previous thread to finish
       before touching any globals */
    if (semaphore) {
        PyObject *lock = PyObject_CallMethod(semaphore, "acquire", NULL);
        if (!lock) goto cleanup; // exception
        Py_DECREF(lock);
    }

    /* setup printf redirection */
    assert(!logger);
    logging = PyImport_AddModule("logging");
    if (!logging) goto release_lock;
    logger = PyObject_CallMethod(logging, "getLogger", "(s)", "SoE");
    if (!logger) goto release_lock;

    do {
        Py_ssize_t switches_len = PyList_Size(switches);
        size_t argc = 15 + switches_len;
        const char *argv[25] = {
            "main", "-b", "-o", dst, "--money", smoney, "--exp", sexp,
            "--id", id_buf, "--placement", placement
        };
        if (switches_len < 0 || argc >= ARRAY_SIZE(argv)) {
            PyErr_SetString(PyExc_RuntimeError, "Too many switches to main!");
            break;
        }
        for (Py_ssize_t i=0; i<switches_len; i++) {
            PyObject* sw = PyList_GetItem(switches, i);
            argv[12+i] = PyUnicode_AsUTF8(sw);
        }
        argv[argc-3] = src;
        argv[argc-2] = flags;
        argv[argc-1] = sseed;

        /* TODO: verify ap_seed is <= 32 bytes */

        int res = evermizer_main((int)argc, argv);
        pyres = PyLong_FromLong(res);
    } while (false);

    /* flush and free stdout redirection buffer */
    if (!PyErr_Occurred() && stdoutbuf && *stdoutbuf) {
        Py_XDECREF(PyObject_CallMethod(logger, STDOUT_LOGGER_LEVEL, "(s)", stdoutbuf));
        if (PyErr_Occurred()) PyErr_Clear(); // ignore errors for bad printf
    }
    free(stdoutbuf);
    stdoutbuf = NULL;

    /* cleanup */
    Py_DECREF(logger);
    logger = NULL;

release_lock:
    if (semaphore) {
        PyObject *release = PyObject_CallMethod(semaphore, "release", NULL);
        if (!release) {
            Py_DECREF(pyres);
            pyres = NULL; // exception
            goto cleanup;
        }
        Py_DECREF(release);
    }

cleanup:
    Py_DECREF(osrc);
    Py_DECREF(odst);
    Py_DECREF(oplacement);
error:
    return pyres;
}

static PyObject *PyList_from_requirements(const struct progression_requirement *first, size_t len)
{
    PyObject *list = PyList_New(0);
    if (list == NULL) return NULL;

    for (size_t i = 0; i < len; i++) {
        if (first[i].progress != P_NONE && first[i].pieces > 0) {
            PyObject *pair = Py_BuildValue("ii", first[i].pieces,
                                                 first[i].progress);
            PyList_Append(list, pair);
            Py_DECREF(pair);
        } else {
            break;
        }
    }

    return list;
}

static PyObject *PyList_from_providers(const struct progression_provider *first, size_t len)
{
    PyObject *list = PyList_New(0);
    if (list == NULL) return NULL;

    for (size_t i = 0; i < len; i++) {
        if (first[i].progress != P_NONE && first[i].pieces > 0) {
            PyObject *pair = Py_BuildValue("ii", first[i].pieces,
                                                 first[i].progress);
            PyList_Append(list, pair);
            Py_DECREF(pair);
        } else {
            break;
        }
    }

    return list;
}

static PyObject *
_evermizer_get_locations(PyObject *self, PyObject *args)
{
    const size_t ng = ARRAY_SIZE(gourd_data);
    const size_t nb = ARRAY_SIZE(boss_names);
    const size_t na = ARRAY_SIZE(alchemy_locations);
    const size_t location_count = ng + nb + na;
    PyObject *result = PyList_New(location_count);

    for (size_t i = 0; i < ng; i++) {
        PyObject *args = Py_BuildValue("(s)", gourd_data[i].name);
        PyObject *loc = PyObject_CallObject((PyObject *) &LocationType, args);
        if (!loc) goto error;
        ((LocationObject*) loc)->type = CHECK_GOURD;
        ((LocationObject*) loc)->index = (unsigned short)i;
        Py_DECREF(args);
        PyList_SET_ITEM(result, 0+i, loc);
    }

    for (size_t i = 0; i < nb; i++) {
        PyObject *args = Py_BuildValue("(s)", boss_names[i]);
        PyObject *loc = PyObject_CallObject((PyObject *) &LocationType, args);
        if (!loc) goto error;
        ((LocationObject*) loc)->type = CHECK_BOSS;
        ((LocationObject*) loc)->index = (unsigned short)i;
        Py_DECREF(args);
        PyList_SET_ITEM(result, ng+i, loc);
    }

    for (size_t i = 0; i < na; i++) {
        PyObject *args = Py_BuildValue("(s)", alchemy_locations[i].name);
        PyObject *loc = PyObject_CallObject((PyObject *) &LocationType, args);
        if (!loc) goto error;
        ((LocationObject*) loc)->type = CHECK_ALCHEMY;
        ((LocationObject*) loc)->index = (unsigned short)i;
        Py_DECREF(args);
        PyList_SET_ITEM(result, ng+nb+i, loc);
    }

    for (size_t i = 0; i < ARRAY_SIZE(blank_check_tree); i++) {
        const struct check_tree_item *check = blank_check_tree + i;
        for (size_t j = 0; j < location_count; j++) {
            PyObject *o = PyList_GetItem(result, j);
            if (!o) goto error;
            if (((LocationObject*) o)->type != check->type) continue;
            if (((LocationObject*) o)->index != check->index) continue;
            /* fill in requirements */
            if (check->requires[0].progress != P_NONE) {
                PyObject *requirements = PyList_from_requirements(check->requires, ARRAY_SIZE(check->requires));
                PyObject_SetAttrString(o, "requires", requirements);
                assert(requirements->ob_refcnt == 2);
                Py_DECREF(requirements);
            }
            /* fill in progression */
            if (check->provides[0].progress != P_NONE) {
                PyObject *provides = PyList_from_providers(check->provides, ARRAY_SIZE(check->provides));
                PyObject_SetAttrString(o, "provides", provides);
                assert(provides->ob_refcnt == 2);
                Py_DECREF(provides);
            }
        }
    }
    return result;
error:
    Py_DECREF(result);
    return NULL;
}

static bool
is_actual_progress(enum progression p)
{
    /* work around some items being tracked for difficulty that are not actual progression */
    /* NOTE: we could resolve the tree to see if it has progression impact */
    if (p == P_NONE) return false;
    if (p == P_ARMOR) return false;
    if (p == P_OFFENSIVE_FORMULA) return false; /* until they are put in logic */
    if (p == P_PASSIVE_FORMULA) return false;
    if (p == P_AMMO) return false;
    if (p == P_GLITCHED_AMMO) return false;
    if (p == P_CALLBEAD) return false;
    if (p == P_WINGS) return false;
    if (p == P_ATLAS) return false;
    if (p == P_BAZOOKA) return false;
    return true;
}

static bool
is_drop_actual_progress(const drop_tree_item *drop)
{
    for (size_t i=0; i<ARRAY_SIZE(drop->provides); i++) {
        if (drop->provides[i].progress == P_NONE) break;
        if (is_actual_progress(drop->provides[i].progress)) return true;
    }
    return false;
}

static bool
is_extra_actual_progress(const extra_item *extra)
{
    for (size_t i=0; i<ARRAY_SIZE(extra->provides); i++) {
        if (extra->provides[i].progress == P_NONE) break;
        if (is_actual_progress(extra->provides[i].progress)) return true;
    }
    return false;
}

static PyObject *
_evermizer_get_items(PyObject *self, PyObject *args)
{
    /* return list of items that are part of the default pool */
    enum boss_drop_indices boss_drops[] = BOSS_DROPS;
    const size_t ng = ARRAY_SIZE(gourd_drops_data);
    const size_t nb = ARRAY_SIZE(boss_drops);
    const size_t na = ARRAY_SIZE(alchemy_locations);
    const size_t item_count = ng + nb + na;
    PyObject *result = PyList_New(item_count);

    for (size_t i = 0; i < ng; i++) {
        PyObject *args = Py_BuildValue("(s)", gourd_drops_data[i].name);
        PyObject *item = PyObject_CallObject((PyObject *) &ItemType, args);
        if (!item) goto error;
        ((ItemObject*) item)->type = CHECK_GOURD;
        ((ItemObject*) item)->index = (unsigned short)i;
        Py_DECREF(args);
        PyList_SET_ITEM(result, 0+i, item);
    }

    for (size_t i = 0; i < nb; i++) {
        PyObject *args = Py_BuildValue("(s)", boss_drop_names[boss_drops[i]]);
        PyObject *item = PyObject_CallObject((PyObject *) &ItemType, args);
        if (!item) goto error;
        ((ItemObject*) item)->type = CHECK_BOSS;
        ((ItemObject*) item)->index = (unsigned short)boss_drops[i];
        Py_DECREF(args);
        PyList_SET_ITEM(result, ng+i, item);
    }

    for (size_t i = 0; i < na; i++) {
        PyObject *args = Py_BuildValue("(s)", alchemy_locations[i].name);
        PyObject *item = PyObject_CallObject((PyObject *) &ItemType, args);
        if (!item) goto error;
        ((ItemObject*) item)->type = CHECK_ALCHEMY;
        ((ItemObject*) item)->index = (unsigned short)i;
        Py_DECREF(args);
        PyList_SET_ITEM(result, ng+nb+i, item);
    }

    for (size_t i = 0; i < ARRAY_SIZE(drops); i++) {
        const struct drop_tree_item *drop = drops + i;
        for (size_t j = 0; j < item_count; j++) {
            PyObject* o = PyList_GetItem(result, j);
            if (!o) goto error;
            if (((ItemObject*) o)->type != drop->type) continue;
            if (((ItemObject*) o)->index != drop->index) continue;
            /* mark as progression item and fill in progression */
            if (drop->provides[0].progress != P_NONE) {
                ((ItemObject*) o)->progression = is_drop_actual_progress(drop);
                ((ItemObject*) o)->useful = true;
                PyObject *provides = PyList_from_providers(drop->provides, ARRAY_SIZE(drop->provides));
                PyObject_SetAttrString(o, "provides", provides);
                assert(provides->ob_refcnt == 2);
                Py_DECREF(provides);
            }
        }
    }
    return result;
error:
    Py_DECREF(result);
    return NULL;
}

static PyObject *
_evermizer_get_extra_items(PyObject *sef, PyObject *args)
{
    /* return list of supported items that are not placed by default */
    const size_t extra_count = ARRAY_SIZE(extra_data);
    PyObject *result = PyList_New(extra_count);

    for (size_t i = 0; i < extra_count; i++) {
        const struct extra_item *extra = extra_data + i;
        PyObject *args = Py_BuildValue("(s)", extra->name);
        PyObject *item = PyObject_CallObject((PyObject *) &ItemType, args);
        if (!item) goto error;
        ((ItemObject*) item)->type = CHECK_EXTRA;
        ((ItemObject*) item)->index = (unsigned short)i;
        if (extra->provides[0].progress != P_NONE) {
            ((ItemObject*) item)->progression = is_extra_actual_progress(extra);
            ((ItemObject*) item)->useful = true;
            PyObject *provides = PyList_from_providers(extra->provides, ARRAY_SIZE(extra->provides));
            PyObject_SetAttrString(item, "provides", provides);
            assert(provides->ob_refcnt == 2);
            Py_DECREF(provides);
        }
        Py_DECREF(args);
        PyList_SET_ITEM(result, i, item);
    }
    return result;
error:
    Py_DECREF(result);
    return NULL;
}

static PyObject *
_evermizer_get_traps(PyObject *sef, PyObject *args)
{
    /* return list of traps that are not placed by default */
    const size_t trap_count = ARRAY_SIZE(trap_data);
    PyObject *result = PyList_New(trap_count);

    for (size_t i = 0; i < trap_count; i++) {
        PyObject *args = Py_BuildValue("(s)", trap_data[i].name);
        PyObject *item = PyObject_CallObject((PyObject *) &ItemType, args);
        if (!item) goto error;
        ((ItemObject*) item)->type = CHECK_TRAP;
        ((ItemObject*) item)->index = (unsigned short)i;
        Py_DECREF(args);
        PyList_SET_ITEM(result, i, item);
    }
    return result;
error:
    Py_DECREF(result);
    return NULL;
}

static PyObject *
_evermizer_get_logic(PyObject *self, PyObject *args)
{
    const size_t n = ARRAY_SIZE(blank_check_tree);
    PyObject *result = PyList_New(n);
    for (size_t i = 0; i < n; i++) {
        const struct check_tree_item *check = blank_check_tree + i;
        PyObject *args = Py_BuildValue("(s)", "");
        PyObject *loc = PyObject_CallObject((PyObject *) &LocationType, args);
        if (!loc) goto error;
        ((LocationObject*) loc)->type = check->type;
        ((LocationObject*) loc)->index = check->index;
        Py_DECREF(args);
        /* fill in requirements */
        if (check->requires[0].progress != P_NONE) {
            PyObject *requirements = PyList_from_requirements(check->requires, ARRAY_SIZE(check->requires));
            PyObject_SetAttrString(loc, "requires", requirements);
            assert(requirements->ob_refcnt == 2);
            Py_DECREF(requirements);
        }
        /* fill in progression */
        if (check->provides[0].progress != P_NONE) {
            PyObject *provides = PyList_from_providers(check->provides, ARRAY_SIZE(check->provides));
            PyObject_SetAttrString(loc, "provides", provides);
            assert(provides->ob_refcnt == 2);
            Py_DECREF(provides);
        }
        PyList_SET_ITEM(result, i, loc);
    }
    return result;
error:
    Py_DECREF(result);
    return NULL;
}

/* module */
static PyMethodDef _evermizer_methods[] = {
    {"main", _evermizer_main, METH_VARARGS, "Run ROM generation"},
    {"get_locations", _evermizer_get_locations, METH_NOARGS, "Returns list of locations"},
    {"get_items", _evermizer_get_items, METH_NOARGS, "Returns list of default items"},
    {"get_extra_items", _evermizer_get_extra_items, METH_NOARGS, "Returns list of items not placed by default"},
    {"get_traps", _evermizer_get_traps, METH_NOARGS, "Returns trap items"},
    {"get_logic", _evermizer_get_logic, METH_NOARGS, "Returns the check tree, which is list of all logic rules, including locations"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef _evermizer_module = {
    PyModuleDef_HEAD_INIT,
    "_evermizer", /* name of module */
    NULL,         /* module documentation, may be NULL */
    -1,           /* size of per-interpreter state of the module,
                     or -1 if the module keeps state in global variables. */
    _evermizer_methods
};

PyMODINIT_FUNC
PyInit__evermizer(void)
{
    PyObject *m;
    PyObject *threading;

    if (PyType_Ready(&LocationType) < 0) return NULL;
    if (PyType_Ready(&ItemType) < 0) return NULL;

    m = PyModule_Create(&_evermizer_module);
    if (!m) return NULL;

    Py_INCREF(&LocationType);
    if (PyModule_AddObject(m, "Location", (PyObject *) &LocationType) < 0)
    {
        Py_DECREF(&LocationType);
        goto type_error;
    }
    Py_INCREF(&ItemType);
    if (PyModule_AddObject(m, "Item", (PyObject *) &ItemType) < 0)
    {
        Py_DECREF(&ItemType);
        goto type_error;
    }

    /* add required constants/enum values to module */
    if (PyModule_AddIntConstant(m, "P_NONE", P_NONE) ||
        PyModule_AddIntConstant(m, "P_WEAPON", P_WEAPON) ||
        PyModule_AddIntConstant(m, "P_ROCK_SKIP", P_ROCK_SKIP) ||
        PyModule_AddIntConstant(m, "P_ROCKET", P_ROCKET) ||
        PyModule_AddIntConstant(m, "P_ENERGY_CORE", P_ENERGY_CORE) ||
        PyModule_AddIntConstant(m, "P_CORE_FRAGMENT", P_CORE_FRAGMENT) ||
        PyModule_AddIntConstant(m, "P_FINAL_BOSS", P_FINAL_BOSS) ||
        PyModule_AddIntConstant(m, "P_JAGUAR_RING", P_JAGUAR_RING) ||
        PyModule_AddIntConstant(m, "CHECK_NONE", CHECK_NONE) ||
        PyModule_AddIntConstant(m, "CHECK_ALCHEMY", CHECK_ALCHEMY) ||
        PyModule_AddIntConstant(m, "CHECK_BOSS", CHECK_BOSS) ||
        PyModule_AddIntConstant(m, "CHECK_GOURD", CHECK_GOURD) ||
        PyModule_AddIntConstant(m, "CHECK_NPC", CHECK_NPC) ||
        PyModule_AddIntConstant(m, "CHECK_RULE", CHECK_RULE) ||
        PyModule_AddIntConstant(m, "CHECK_EXTRA", CHECK_EXTRA) ||
        PyModule_AddIntConstant(m, "CHECK_TRAP", CHECK_TRAP)
    ) {
        goto const_error;
    }

    /* initialize global semaphore. we leak this memory */
    threading = PyImport_AddModule("threading");
    if (threading) {
        semaphore = PyObject_CallMethod(threading, "BoundedSemaphore", NULL);
        if (!semaphore) goto const_error;
    } else {
        /* threading not built in */
        PyErr_Clear();
    }

    return m;
const_error:
    /* FIXME: do we need to decref the types? */
type_error:
    Py_XDECREF(m);
    return NULL;
}
