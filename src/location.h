#pragma once
#include <Python.h>
#include <structmember.h>

/*** _evermizer.Location type ***/

typedef struct {
    PyObject_HEAD
    PyObject *name;
    enum check_tree_item_type type;
    unsigned short index;
    char difficulty;
    PyObject *requires;
    PyObject *provides;
} LocationObject;

static void
Location_dealloc(LocationObject *self)
{
    Py_XDECREF(self->name);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *
Location_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    LocationObject *self;
    self = (LocationObject *) type->tp_alloc(type, 0);
    if (self == NULL) return NULL;
    
    // FIXME: can we leave this at NULL until init?
    self->name = PyUnicode_FromString("");
    if (self->name == NULL) {
        Py_DECREF(self);
        return NULL;
    }
    
    self->requires = PyList_New(0);
    if (self->requires == NULL) {
        // FIXME: do we need to decref name?
        Py_DECREF(self);
        return NULL;
    }
    
    self->provides = PyList_New(0);
    if (self->provides == NULL) {
        // FIXME: do we need to decref name and requires?
        Py_DECREF(self);
        return NULL;
    }
    
    return (PyObject *) self;
}

static int
Location_init(LocationObject *self, PyObject *args, PyObject *kwds)
{
    static const char *kwlist[] = {"name", NULL};
    PyObject *name = NULL;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", (char**)kwlist, &name))
        return -1;
    
    if (name) {
        PyObject *tmp = self->name;
        Py_INCREF(name);
        self->name = name;
        Py_XDECREF(tmp);
    }
    return 0;
}

static PyMemberDef Location_members[] = {
    {"name", T_OBJECT_EX, offsetof(LocationObject, name), 0, "Location name"},
    {"type", T_INT, offsetof(LocationObject, type), 1, "Location type of vanilla item"},
    {"index", T_USHORT, offsetof(LocationObject, index), 1, "Nth location of type"},
    {"difficulty", T_BYTE, offsetof(LocationObject, difficulty), 1, "Difficulty 0..2 for bad/hidden checks"},
    {"requires", T_OBJECT_EX, offsetof(LocationObject, requires), 0, "List of tuples (amount, progression) requirements"},
    {"provides", T_OBJECT_EX, offsetof(LocationObject, provides), 0, "Lift of tuples (amount, progression) providers"},
    {NULL}
};

static PyTypeObject LocationType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_evermizer.Location",
    .tp_doc = "",
    .tp_basicsize = sizeof(LocationObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Location_new,
    .tp_init = (initproc) Location_init,
    .tp_dealloc = (destructor) Location_dealloc,
    .tp_members = Location_members,
};
