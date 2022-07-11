#pragma once
#include <Python.h>
#include <structmember.h>

/*** _evermizer.Item type ***/

typedef struct {
    PyObject_HEAD
    PyObject *name;
    char progression;
    char useful;
    enum check_tree_item_type type;
    unsigned short index; 
    PyObject *provides;
} ItemObject;

static void
Item_dealloc(ItemObject *self)
{
    Py_XDECREF(self->name);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *
Item_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    ItemObject *self;
    self = (ItemObject *) type->tp_alloc(type, 0);
    if (self == NULL) return NULL;
    
    // FIXME: can we leave this at NULL until init?
    self->name = PyUnicode_FromString("");
    if (self->name == NULL) {
        Py_DECREF(self);
        return NULL;
    }

    self->progression = 0;
    self->useful = 0;

    self->provides = PyList_New(0);
    if (self->provides == NULL) {
        // FIXME: do we need to decref name?
        Py_DECREF(self);
        return NULL;
    }
    
    return (PyObject *) self;
}

static int
Item_init(ItemObject *self, PyObject *args, PyObject *kwds)
{
    static const char *kwlist[] = {"name", "progression", NULL};
    PyObject *name = NULL;
    bool progression = false;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Op", (char**)kwlist, &name, &progression))
        return -1;
    
    if (name) {
        PyObject *tmp = self->name;
        Py_INCREF(name);
        self->name = name;
        Py_XDECREF(tmp);
    }
    self->progression = progression?1:0;
    return 0;
}

static PyMemberDef Item_members[] = {
    {"name", T_OBJECT_EX, offsetof(ItemObject, name), 1, "Item name"},
    {"progression", T_BOOL, offsetof(ItemObject, progression), 1, "Item is a progression item"},
    {"useful", T_BOOL, offsetof(ItemObject, useful), 1, "Item is a useful item"},
    {"type", T_INT, offsetof(ItemObject, type), 1, "Location type of vanilla item"},
    {"index", T_USHORT, offsetof(ItemObject, index), 1, "Nth location of type"},
    {"provides", T_OBJECT_EX, offsetof(LocationObject, provides), 0, "List of tuples (amount, progression) providers"},
    {NULL}
};

static PyTypeObject ItemType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_evermizer.Item",
    .tp_doc = "",
    .tp_basicsize = sizeof(ItemObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Item_new,
    .tp_init = (initproc) Item_init,
    .tp_dealloc = (destructor) Item_dealloc,
    .tp_members = Item_members,
};
