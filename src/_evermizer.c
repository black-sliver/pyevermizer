#include <Python.h>

#if defined(__CLING__) /* hide evermizer definitions in cppyy */
namespace _evermizer {
#define WITH_ASSERT
#endif

#define main evermizer_main
#include "evermizer/main.c"
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
        str = PyObject_CallNoArgs(fspath); /* to string */
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

/* methods */
static PyObject *
_evermizer_main(PyObject *self, PyObject *py_args)
{
    /* _evermizer.main call signature:
        src: Path, dst: Path, placement: Path, apseed: str, apslot: str, seed: int, flags: str, money: int, exp: int
    */

    /* original main signature:
          int argc, char** argv: { <exe> [flags ...] <src.sfc> [settings [seed]] }
       mapped main signature:
          15, { "evermizer", '-b", "-o", "<dst.sfc>", "--money", "<money%>", "--exp', "<exp%>",
               "--id", "<hex(32B ap seed)>[:]<hex(32B ap slot)>", "--placement", "<placement.txt>",
               "<src.sfc>", "<flags>", "<seed>" }
       TODO: split UI/argument parsing from generation in evermizer, so we don't need to call the C main
    */

    PyObject *osrc, *odst, *oplacement;
    const char *ap_seed, *ap_slot;
    PyObject *oseed; /* any integer -> PyObject */
    const char* flags;
    int money, exp;
    if (!PyArg_ParseTuple(py_args, "O&O&O&ssOsii", path2ansi, &osrc, path2ansi, &odst, path2ansi, &oplacement,
                          &ap_seed, &ap_slot, &oseed, &flags, &money, &exp))
        return NULL;

    uint64_t seed = (uint64_t)PyLong_AsUnsignedLongLong(oseed);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        Py_DECREF(osrc);
        Py_DECREF(odst);
        Py_DECREF(oplacement);
        return PyErr_Format(PyExc_TypeError, "6th parameter 'seed' must be unsigned integer type, but got %s",
                            Py_TYPE(oseed)->tp_name);
    }
    char sseed[21]; snprintf(sseed, sizeof(sseed), "%" PRIx64, seed);

    const char *src = PyBytes_AS_STRING(osrc);
    const char *dst = PyBytes_AS_STRING(odst);
    const char *placement = PyBytes_AS_STRING(oplacement);

    if (exp > 9999) exp = 9999;
    if (exp < 0) exp = 0;
    char sexp[5]; snprintf(sexp, sizeof(sexp), "%d", exp);

    if (money > 9999) money = 9999;
    if (money < 0) money = 0;
    char smoney[5]; snprintf(smoney, sizeof(smoney), "%d", money);

    char hexchars[] = "0123456789ABCDEF";
    char id_buf[130]; /* hex(32B):hex(32B)\0 */
    memset(id_buf, 0, 130);
    char *id_bufp = id_buf;
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

    const char *main_args[] = {
        "main", "-b", "-o", dst, "--money", smoney, "--exp", sexp,
        "--id", id_buf, "--placement", placement, src, flags, sseed
    };

    // TODO: verify ap_seed is <= 32 bytes
    // TODO: capture stderr?
    int res = evermizer_main(ARRAY_SIZE(main_args), main_args);

    Py_DECREF(osrc);
    Py_DECREF(odst);
    Py_DECREF(oplacement);

    return PyLong_FromLong(res);
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
_evermizer_get_locations(PyObject *self, PyObject *)
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

static PyObject *
_evermizer_get_items(PyObject *self, PyObject *args)
{
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
                /* TODO: exclude wings, callbeads, armor and ammo? */
                ((ItemObject*) o)->progression = true;
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
    {"get_items", _evermizer_get_items, METH_NOARGS, "Returns list of items"},
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
        PyModule_AddIntConstant(m, "P_FINAL_BOSS", P_FINAL_BOSS) ||
        PyModule_AddIntConstant(m, "P_JAGUAR_RING", P_JAGUAR_RING) ||
        PyModule_AddIntConstant(m, "CHECK_NONE", CHECK_NONE) ||
        PyModule_AddIntConstant(m, "CHECK_ALCHEMY", CHECK_ALCHEMY) ||
        PyModule_AddIntConstant(m, "CHECK_BOSS", CHECK_BOSS) ||
        PyModule_AddIntConstant(m, "CHECK_GOURD", CHECK_GOURD) ||
        PyModule_AddIntConstant(m, "CHECK_NPC", CHECK_NPC) ||
        PyModule_AddIntConstant(m, "CHECK_RULE", CHECK_RULE))
    {
        goto const_error;
    }
    
    int slow_burn = 0;
    (void)slow_burn;
    
    return m;
const_error:
    /* FIXME: do we need to decref the types? */
type_error:
    Py_XDECREF(m);
    return NULL;
}