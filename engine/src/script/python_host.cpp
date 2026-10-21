#include "bge/script/python_host.h"

#include "bge/core/log.h"
#include "bge/ecs/components.h"
#include "bge/scene/scene.h"

#include <fstream>
#include <sstream>

#if defined(BGE_WITH_PYTHON)
#include <Python.h>
#endif

namespace bge {

#if defined(BGE_WITH_PYTHON)
namespace {

Scene* g_scene = nullptr;
float g_delta = 0.0f;
uint64_t g_frame = 0;
long g_current_controller = -1;

Entity resolve(long handle) {
    if (!g_scene || handle < 0) return Entity{};
    return g_scene->world().entity_at(static_cast<uint32_t>(handle));
}

Transform* transform_of(long handle) {
    Entity e = resolve(handle);
    if (!e) return nullptr;
    return g_scene->world().get<Transform>(e);
}

// ---------------------------------------------------------------- native ----

PyObject* native_find(PyObject*, PyObject* args) {
    const char* name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name)) return nullptr;
    if (!g_scene) return PyLong_FromLong(-1);
    Entity e = g_scene->find(name);
    if (!e) return PyLong_FromLong(-1);
    return PyLong_FromUnsignedLong(e.index);
}

PyObject* native_name(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Entity e = resolve(handle);
    if (!e) Py_RETURN_NONE;
    const Name* n = g_scene->world().get<Name>(e);
    if (!n) Py_RETURN_NONE;
    return PyUnicode_FromString(n->value.c_str());
}

PyObject* native_location(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Transform* t = transform_of(handle);
    if (!t) Py_RETURN_NONE;
    return Py_BuildValue("(fff)", t->position.x, t->position.y, t->position.z);
}

PyObject* native_set_location(PyObject*, PyObject* args) {
    long handle = 0;
    float x = 0, y = 0, z = 0;
    if (!PyArg_ParseTuple(args, "lfff", &handle, &x, &y, &z)) return nullptr;
    Transform* t = transform_of(handle);
    if (t) t->position = Vec3{x, y, z};
    Py_RETURN_NONE;
}

PyObject* native_world_position(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Transform* t = transform_of(handle);
    if (!t) Py_RETURN_NONE;
    Vec3 p = t->world.translation();
    return Py_BuildValue("(fff)", p.x, p.y, p.z);
}

PyObject* native_rotation(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Transform* t = transform_of(handle);
    if (!t) Py_RETURN_NONE;
    Vec3 e = t->rotation.to_euler();
    return Py_BuildValue("(fff)", e.x, e.y, e.z);
}

PyObject* native_set_rotation(PyObject*, PyObject* args) {
    long handle = 0;
    float x = 0, y = 0, z = 0;
    if (!PyArg_ParseTuple(args, "lfff", &handle, &x, &y, &z)) return nullptr;
    Transform* t = transform_of(handle);
    if (t) t->rotation = Quat::from_euler(Vec3{x, y, z});
    Py_RETURN_NONE;
}

PyObject* native_scale(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Transform* t = transform_of(handle);
    if (!t) Py_RETURN_NONE;
    return Py_BuildValue("(fff)", t->scale.x, t->scale.y, t->scale.z);
}

PyObject* native_set_scale(PyObject*, PyObject* args) {
    long handle = 0;
    float x = 1, y = 1, z = 1;
    if (!PyArg_ParseTuple(args, "lfff", &handle, &x, &y, &z)) return nullptr;
    Transform* t = transform_of(handle);
    if (t) t->scale = Vec3{x, y, z};
    Py_RETURN_NONE;
}

PyObject* native_apply_rotation(PyObject*, PyObject* args) {
    long handle = 0;
    float ax = 0, ay = 0, az = 0, angle = 0;
    int local = 0;
    if (!PyArg_ParseTuple(args, "lffffi", &handle, &ax, &ay, &az, &angle, &local)) return nullptr;
    Transform* t = transform_of(handle);
    if (t) {
        Quat delta = Quat::from_axis_angle(Vec3{ax, ay, az}, angle);
        t->rotation = (delta * t->rotation).normalized();
    }
    Py_RETURN_NONE;
}

PyObject* native_spawn(PyObject*, PyObject* args) {
    const char* name = "Object";
    if (!PyArg_ParseTuple(args, "s", &name)) return nullptr;
    if (!g_scene) return PyLong_FromLong(-1);
    Entity e = g_scene->create_entity(name);
    return PyLong_FromUnsignedLong(e.index);
}

PyObject* native_remove(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Entity e = resolve(handle);
    if (e && g_scene) g_scene->world().destroy(e);
    Py_RETURN_NONE;
}

PyObject* native_set_visible(PyObject*, PyObject* args) {
    long handle = 0;
    int visible = 1;
    if (!PyArg_ParseTuple(args, "li", &handle, &visible)) return nullptr;
    Entity e = resolve(handle);
    if (e && g_scene) {
        if (MeshRenderer* r = g_scene->world().get<MeshRenderer>(e)) r->visible = visible != 0;
    }
    Py_RETURN_NONE;
}

PyObject* native_get_visible(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Entity e = resolve(handle);
    if (!e) Py_RETURN_FALSE;
    MeshRenderer* r = g_scene ? g_scene->world().get<MeshRenderer>(e) : nullptr;
    if (!r) Py_RETURN_TRUE;
    return PyBool_FromLong(r->visible ? 1 : 0);
}

PyObject* native_object_names(PyObject*, PyObject* args) {
    (void)args;
    PyObject* list = PyList_New(0);
    if (!list || !g_scene) return list;
    g_scene->world().each<Name>([&](Entity, Name& n) {
        PyObject* s = PyUnicode_FromString(n.value.c_str());
        if (s) PyList_Append(list, s);
    });
    return list;
}

PyObject* native_entity_count(PyObject*, PyObject* args) {
    (void)args;
    if (!g_scene) return PyLong_FromLong(0);
    return PyLong_FromSize_t(g_scene->world().alive_count());
}

PyObject* native_delta(PyObject*, PyObject* args) {
    (void)args;
    return PyFloat_FromDouble(static_cast<double>(g_delta));
}

PyObject* native_frame(PyObject*, PyObject* args) {
    (void)args;
    return PyLong_FromUnsignedLongLong(g_frame);
}

PyObject* native_log(PyObject*, PyObject* args) {
    const char* message = nullptr;
    if (!PyArg_ParseTuple(args, "s", &message)) return nullptr;
    BGE_INFO("[script] %s", message);
    Py_RETURN_NONE;
}

// ---------------------------------------------------------- properties ----

PyObject* native_props_get(PyObject*, PyObject* args) {
    long handle = 0;
    const char* key = nullptr;
    if (!PyArg_ParseTuple(args, "ls", &handle, &key)) return nullptr;
    Entity e = resolve(handle);
    if (!e) Py_RETURN_NONE;
    Properties* properties = g_scene ? g_scene->world().get<Properties>(e) : nullptr;
    if (!properties) Py_RETURN_NONE;
    auto it = properties->values.find(key);
    if (it == properties->values.end()) Py_RETURN_NONE;
    const Property& value = it->second;
    switch (value.kind) {
        case Property::Kind::Number: return PyFloat_FromDouble(value.number);
        case Property::Kind::Flag: return PyBool_FromLong(value.number != 0.0 ? 1 : 0);
        case Property::Kind::Text: return PyUnicode_FromString(value.text.c_str());
    }
    Py_RETURN_NONE;
}

PyObject* native_props_set(PyObject*, PyObject* args) {
    long handle = 0;
    const char* key = nullptr;
    PyObject* value = nullptr;
    if (!PyArg_ParseTuple(args, "lsO", &handle, &key, &value)) return nullptr;
    Entity e = resolve(handle);
    if (!e) Py_RETURN_NONE;
    Properties* properties = g_scene ? g_scene->world().get<Properties>(e) : nullptr;
    if (!properties) {
        if (!g_scene) Py_RETURN_NONE;
        // Custom properties are created on demand.
        g_scene->world().add(e, Properties{});
        properties = g_scene->world().get<Properties>(e);
        if (!properties) Py_RETURN_NONE;
    }

    Property property;
    if (PyBool_Check(value)) {
        property.kind = Property::Kind::Flag;
        property.number = value == Py_True ? 1.0 : 0.0;
    } else if (PyLong_Check(value)) {
        property.kind = Property::Kind::Number;
        property.number = static_cast<double>(PyLong_AsLongLong(value));
    } else if (PyFloat_Check(value)) {
        property.kind = Property::Kind::Number;
        property.number = PyFloat_AsDouble(value);
    } else if (PyUnicode_Check(value)) {
        property.kind = Property::Kind::Text;
        const char* utf8 = PyUnicode_AsUTF8(value);
        property.text = utf8 ? utf8 : "";
    } else {
        PyErr_SetString(PyExc_TypeError, "property values must be numbers, booleans or strings");
        return nullptr;
    }
    properties->values[key] = std::move(property);
    Py_RETURN_NONE;
}

PyObject* native_props_remove(PyObject*, PyObject* args) {
    long handle = 0;
    const char* key = nullptr;
    if (!PyArg_ParseTuple(args, "ls", &handle, &key)) return nullptr;
    Entity e = resolve(handle);
    Properties* properties = (e && g_scene) ? g_scene->world().get<Properties>(e) : nullptr;
    if (properties) properties->values.erase(key);
    Py_RETURN_NONE;
}

PyObject* native_props_keys(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Entity e = resolve(handle);
    PyObject* list = PyList_New(0);
    Properties* properties = (e && g_scene) ? g_scene->world().get<Properties>(e) : nullptr;
    if (!properties) return list;
    for (const auto& [key, value] : properties->values) {
        PyObject* s = PyUnicode_FromString(key.c_str());
        if (s) PyList_Append(list, s);
    }
    return list;
}

// ---------------------------------------------------------- controllers ----

PyObject* native_controller_names(PyObject*, PyObject* args) {
    (void)args;
    PyObject* list = PyList_New(0);
    if (!list || !g_scene) return list;
    g_scene->world().each<Controller>([&](Entity e, Controller& c) {
        const Name* n = g_scene->world().get<Name>(e);
        PyObject* s = PyUnicode_FromString(n ? n->value.c_str() : c.name.c_str());
        if (s) PyList_Append(list, s);
    });
    return list;
}

PyObject* native_current_controller(PyObject*, PyObject* args) {
    (void)args;
    return PyLong_FromLong(g_current_controller);
}

PyObject* native_controller_script(PyObject*, PyObject* args) {
    long handle = 0;
    if (!PyArg_ParseTuple(args, "l", &handle)) return nullptr;
    Entity e = resolve(handle);
    if (!e) Py_RETURN_NONE;
    const Controller* c = g_scene ? g_scene->world().get<Controller>(e) : nullptr;
    if (!c) Py_RETURN_NONE;
    return PyUnicode_FromString(c->script.c_str());
}

PyMethodDef kNativeMethods[] = {
    {"find", native_find, METH_VARARGS, "Find an object by name and return its handle."},
    {"name", native_name, METH_VARARGS, "Return the name of an object."},
    {"location", native_location, METH_VARARGS, "Return the local location of an object."},
    {"set_location", native_set_location, METH_VARARGS, "Set the local location of an object."},
    {"world_position", native_world_position, METH_VARARGS, "Return the world position of an object."},
    {"rotation", native_rotation, METH_VARARGS, "Return the rotation of an object as Euler angles."},
    {"set_rotation", native_set_rotation, METH_VARARGS, "Set the rotation of an object from Euler angles."},
    {"scale", native_scale, METH_VARARGS, "Return the scale of an object."},
    {"set_scale", native_set_scale, METH_VARARGS, "Set the scale of an object."},
    {"apply_rotation", native_apply_rotation, METH_VARARGS, "Rotate an object around an axis."},
    {"spawn", native_spawn, METH_VARARGS, "Create an empty object and return its handle."},
    {"remove", native_remove, METH_VARARGS, "Remove an object."},
    {"set_visible", native_set_visible, METH_VARARGS, "Show or hide an object."},
    {"get_visible", native_get_visible, METH_VARARGS, "Return whether an object is visible."},
    {"object_names", native_object_names, METH_VARARGS, "Return the names of all objects."},
    {"entity_count", native_entity_count, METH_VARARGS, "Return the number of live entities."},
    {"delta", native_delta, METH_VARARGS, "Return the last frame delta in seconds."},
    {"frame", native_frame, METH_VARARGS, "Return the current frame counter."},
    {"log", native_log, METH_VARARGS, "Write a message to the engine log."},
    {"props_get", native_props_get, METH_VARARGS, "Read a custom property."},
    {"props_set", native_props_set, METH_VARARGS, "Write a custom property."},
    {"props_remove", native_props_remove, METH_VARARGS, "Remove a custom property."},
    {"props_keys", native_props_keys, METH_VARARGS, "List custom property names."},
    {"controller_names", native_controller_names, METH_VARARGS, "List the objects that carry a controller."},
    {"current_controller", native_current_controller, METH_VARARGS, "Return the controller that is running."},
    {"controller_script", native_controller_script, METH_VARARGS, "Return the source of a controller."},
    {nullptr, nullptr, 0, nullptr},
};

// The user facing API. Everything here is plain Python on top of `bge._native`,
// so the surface can grow without touching the C++ layer.
const char* kApiSource = R"PY(
import math as _math
import types as _types

__version__ = "0.1.0"


class Vector:
    """3D vector, matching the classic bge.types.Vector behaviour."""

    __slots__ = ("x", "y", "z")

    def __init__(self, x=0.0, y=0.0, z=0.0):
        if isinstance(x, Vector):
            self.x, self.y, self.z = x.x, x.y, x.z
        elif isinstance(x, (tuple, list)):
            self.x = float(x[0])
            self.y = float(x[1])
            self.z = float(x[2]) if len(x) > 2 else 0.0
        else:
            self.x = float(x)
            self.y = float(y)
            self.z = float(z)

    def __repr__(self):
        return "Vector((%g, %g, %g))" % (self.x, self.y, self.z)

    def __len__(self):
        return 3

    def __getitem__(self, index):
        return (self.x, self.y, self.z)[index]

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    def __eq__(self, other):
        try:
            other = Vector(other)
        except Exception:
            return NotImplemented
        return (self.x, self.y, self.z) == (other.x, other.y, other.z)

    def __add__(self, other):
        other = Vector(other)
        return Vector(self.x + other.x, self.y + other.y, self.z + other.z)

    def __sub__(self, other):
        other = Vector(other)
        return Vector(self.x - other.x, self.y - other.y, self.z - other.z)

    def __mul__(self, scalar):
        if isinstance(scalar, (int, float)):
            return Vector(self.x * scalar, self.y * scalar, self.z * scalar)
        other = Vector(scalar)
        return self.x * other.x + self.y * other.y + self.z * other.z

    __rmul__ = __mul__

    def __truediv__(self, scalar):
        return Vector(self.x / scalar, self.y / scalar, self.z / scalar)

    def __neg__(self):
        return Vector(-self.x, -self.y, -self.z)

    def __hash__(self):
        return hash((self.x, self.y, self.z))

    @property
    def length(self):
        return _math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)

    def normalized(self):
        length = self.length
        if length < 1e-9:
            return Vector()
        return Vector(self.x / length, self.y / length, self.z / length)

    def dot(self, other):
        other = Vector(other)
        return self.x * other.x + self.y * other.y + self.z * other.z

    def cross(self, other):
        other = Vector(other)
        return Vector(self.y * other.z - self.z * other.y,
                      self.z * other.x - self.x * other.z,
                      self.x * other.y - self.y * other.x)

    def to_tuple(self):
        return (self.x, self.y, self.z)

    def copy(self):
        return Vector(self)


class Euler:
    """Euler angles in radians (Blender convention)."""

    __slots__ = ("x", "y", "z")

    def __init__(self, x=0.0, y=0.0, z=0.0):
        if isinstance(x, Euler):
            self.x, self.y, self.z = x.x, x.y, x.z
        elif isinstance(x, (tuple, list)):
            self.x = float(x[0])
            self.y = float(x[1])
            self.z = float(x[2]) if len(x) > 2 else 0.0
        else:
            self.x = float(x)
            self.y = float(y)
            self.z = float(z)

    def __repr__(self):
        return "Euler((%g, %g, %g))" % (self.x, self.y, self.z)

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    def __getitem__(self, index):
        return (self.x, self.y, self.z)[index]

    def __len__(self):
        return 3

    def to_tuple(self):
        return (self.x, self.y, self.z)

    def copy(self):
        return Euler(self)


class Quaternion:
    """Quaternion with w last, matching bge.types.Quaternion."""

    __slots__ = ("w", "x", "y", "z")

    def __init__(self, w=1.0, x=0.0, y=0.0, z=0.0):
        if isinstance(w, Quaternion):
            self.w, self.x, self.y, self.z = w.w, w.x, w.y, w.z
        elif isinstance(w, (tuple, list)):
            self.w = float(w[0])
            self.x = float(w[1])
            self.y = float(w[2])
            self.z = float(w[3]) if len(w) > 3 else 0.0
        else:
            self.w = float(w)
            self.x = float(x)
            self.y = float(y)
            self.z = float(z)

    def __repr__(self):
        return "Quaternion((%g, %g, %g, %g))" % (self.w, self.x, self.y, self.z)

    def __iter__(self):
        return iter((self.w, self.x, self.y, self.z))

    def __len__(self):
        return 4

    def __getitem__(self, index):
        return (self.w, self.x, self.y, self.z)[index]

    def to_tuple(self):
        return (self.w, self.x, self.y, self.z)

    @staticmethod
    def from_axis_angle(axis, angle):
        axis = Vector(axis).normalized()
        s = _math.sin(angle * 0.5)
        return Quaternion(_math.cos(angle * 0.5), axis.x * s, axis.y * s, axis.z * s)


types = _types.ModuleType("bge.types")
types.Vector = Vector
types.Euler = Euler
types.Quaternion = Quaternion
types.Matrix = None  # reserved


class _HandlerList(list):
    """A list of callables that can also be used as a decorator holder."""

    def append(self, function):
        list.append(self, function)
        return function

    def remove(self, function):
        if function in self:
            list.remove(self, function)

    def clear(self):
        del self[:]


class Timer:
    """A repeating callback, the equivalent of bge.app.timers."""

    def __init__(self, interval, function, repeat=True):
        self.interval = float(interval)
        self.function = function
        self.repeat = bool(repeat)
        self.elapsed = 0.0
        self.active = True

    def unregister(self):
        self.active = False

    def update(self, delta):
        if not self.active:
            return
        self.elapsed += delta
        while self.elapsed >= self.interval:
            self.elapsed -= self.interval
            self.function()
            if not self.repeat:
                self.active = False
                return


class _Timers:
    def __init__(self):
        self._timers = []

    def new(self, interval, function, repeat=True):
        timer = Timer(interval, function, repeat)
        self._timers.append(timer)
        return timer

    def remove(self, timer):
        if timer in self._timers:
            self._timers.remove(timer)

    def clear(self):
        del self._timers[:]

    def update(self, delta):
        for timer in list(self._timers):
            timer.update(delta)


class _Handlers:
    def __init__(self):
        self.frame_change = _HandlerList()
        self.post_update = _HandlerList()
        self.load_post = _HandlerList()


class _App:
    def __init__(self):
        self.version = (0, 1, 0)
        self.version_string = __version__
        self.background = True
        self.handlers = _Handlers()
        self.timers = _Timers()

    def get_time(self):
        return _native.frame() / 60.0

    def print(self, *args):
        _native.log(" ".join(str(a) for a in args))


app = _App()


class Object:
    """Script proxy for a scene object."""

    def __init__(self, handle):
        self._handle = handle

    def __repr__(self):
        return "<bge object %r>" % (self.name,)

    @property
    def name(self):
        return _native.name(self._handle)

    @property
    def location(self):
        return Vector(_native.location(self._handle))

    @location.setter
    def location(self, value):
        value = Vector(value)
        _native.set_location(self._handle, value.x, value.y, value.z)

    @property
    def worldPosition(self):
        return Vector(_native.world_position(self._handle))

    @property
    def rotation_euler(self):
        return Euler(_native.rotation(self._handle))

    @rotation_euler.setter
    def rotation_euler(self, value):
        value = Euler(value)
        _native.set_rotation(self._handle, value.x, value.y, value.z)

    @property
    def scale(self):
        return Vector(_native.scale(self._handle))

    @scale.setter
    def scale(self, value):
        value = Vector(value)
        _native.set_scale(self._handle, value.x, value.y, value.z)

    @property
    def visible(self):
        return bool(_native.get_visible(self._handle))

    @visible.setter
    def visible(self, value):
        _native.set_visible(self._handle, 1 if value else 0)

    def applyRotation(self, axis, angle, local=False):
        axis = Vector(axis)
        _native.apply_rotation(self._handle, axis.x, axis.y, axis.z, float(angle), 1 if local else 0)

    def remove(self):
        _native.remove(self._handle)

    # Custom properties, the equivalent of Blender custom properties.
    def __getitem__(self, key):
        return _native.props_get(self._handle, key)

    def __setitem__(self, key, value):
        _native.props_set(self._handle, key, value)

    def __delitem__(self, key):
        _native.props_remove(self._handle, key)

    def __contains__(self, key):
        return _native.props_get(self._handle, key) is not None

    def get(self, key, default=None):
        value = _native.props_get(self._handle, key)
        return default if value is None else value

    def keys(self):
        return _native.props_keys(self._handle)


class ObjectRegistry:
    """Name indexed view over the scene objects."""

    def __getitem__(self, key):
        handle = _native.find(key)
        if handle < 0:
            raise KeyError(key)
        return Object(handle)

    def get(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            return default

    def __contains__(self, key):
        return _native.find(key) >= 0

    def __len__(self):
        return len(self.keys())

    def __iter__(self):
        return iter(self.keys())

    def keys(self):
        return _native.object_names()

    def values(self):
        return [Object(_native.find(name)) for name in self.keys()]

    def items(self):
        return [(name, Object(_native.find(name))) for name in self.keys()]


class Controller:
    """A script attached to an object (the classic BGE controller)."""

    def __init__(self, handle):
        self._handle = handle

    def __repr__(self):
        return "<bge controller %r>" % (self.name,)

    @property
    def name(self):
        return _native.name(self._handle)

    @property
    def owner(self):
        return Object(self._handle)

    @property
    def script(self):
        return _native.controller_script(self._handle)

    @property
    def enabled(self):
        return bool(_native.props_get(self._handle, "__enabled__"))


class _Scene:
    def __init__(self):
        self.objects = ObjectRegistry()

    def addObject(self, name):
        return Object(_native.spawn(name))

    def __getitem__(self, key):
        return self.objects[key]

    def __contains__(self, key):
        return key in self.objects


class _Logic:
    def __init__(self):
        self.scene = _Scene()

    def getCurrentController(self):
        handle = _native.current_controller()
        if handle < 0:
            return None
        return Controller(handle)

    def getCurrentScene(self):
        return self.scene

    @property
    def controllers(self):
        return [Controller(_native.find(name)) for name in _native.controller_names()]


logic = _Logic()
objects = logic.scene.objects
app.background = bool(_native.frame() >= 0)


def _dispatch(delta, frame):
    app.timers.update(delta)
    for function in list(app.handlers.frame_change):
        function()
    for function in list(app.handlers.post_update):
        function()


def _describe():
    return "bge %s - %d objects" % (__version__, _native.entity_count())
)PY";

PyObject* build_bge_module() {
    // PyModule_Create requires a definition; PyModule_New gives an empty module.
    // The name must not be null: CPython dereferences it while building the module.
    PyObject* module = PyModule_New("bge");
    if (!module) return nullptr;

    PyObject* native = PyModule_New("bge._native");
    if (!native) {
        Py_DECREF(module);
        return nullptr;
    }
    for (PyMethodDef* def = kNativeMethods; def->ml_name; ++def) {
        PyObject* fn = PyCFunction_New(def, nullptr);
        if (!fn) continue;
        PyModule_AddObject(native, def->ml_name, fn);
    }
    PyModule_AddObject(module, "_native", native);

    PyObject* globals = PyModule_GetDict(module);
    // Turn the module into a package so `from bge.types import Vector` works.
    if (PyObject* path = PyList_New(0)) {
        PyDict_SetItemString(globals, "__path__", path);
        Py_DECREF(path);
    }
    PyObject* code = Py_CompileString(kApiSource, "<bge>", Py_file_input);
    if (!code) {
        PyErr_Print();
        Py_DECREF(module);
        return nullptr;
    }
    PyObject* result = PyEval_EvalCode(code, globals, globals);
    Py_DECREF(code);
    if (!result) {
        PyErr_Print();
        Py_DECREF(module);
        return nullptr;
    }
    Py_DECREF(result);

    // Publish the submodules so they can be imported directly.
    if (PyObject* modules = PyImport_GetModuleDict()) {
        if (PyObject* types_module = PyDict_GetItemString(globals, "types")) {
            Py_INCREF(types_module);
            PyDict_SetItemString(modules, "bge.types", types_module);
        }
    }
    return module;
}

// Formats the pending Python exception (with traceback) into a string.
std::string describe_python_error() {
    if (!PyErr_Occurred()) return "no exception set";
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);

    std::string text;
    if (PyObject* module = PyImport_ImportModule("traceback")) {
        if (PyObject* lines = PyObject_CallMethod(module, "format_exception", "OOO", type ? type : Py_None,
                                                  value ? value : Py_None, traceback ? traceback : Py_None)) {
            if (PyObject* joined = PyUnicode_Join(PyUnicode_FromString(""), lines)) {
                const char* utf8 = PyUnicode_AsUTF8(joined);
                if (utf8) text = utf8;
                Py_DECREF(joined);
            }
        }
        Py_DECREF(module);
    }
    if (text.empty() && value) {
        PyObject* str = PyObject_Str(value);
        if (str) {
            const char* utf8 = PyUnicode_AsUTF8(str);
            if (utf8) text = utf8;
            Py_DECREF(str);
        }
    }
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
    PyErr_Clear();
    return text;
}

} // namespace

#endif // BGE_WITH_PYTHON

struct PythonHost::Impl {
    bool initialized = false;
#if defined(BGE_WITH_PYTHON)
    PyObject* module = nullptr;
#else
    void* module = nullptr;
#endif
};

PythonHost::PythonHost() : impl_(new Impl()) {}

PythonHost::~PythonHost() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool PythonHost::init(const Desc& desc) {
#if defined(BGE_WITH_PYTHON)
    if (impl_->initialized) return true;
    if (desc.scene) g_scene = desc.scene;

    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    config.install_signal_handlers = 0;
    config.user_site_directory = 0;
    config.buffered_stdio = 0;
    BGE_INFO("python: initialising the interpreter");    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        last_error_ = status.err_msg ? status.err_msg : "failed to initialise the Python interpreter";
        BGE_ERROR("%s", last_error_.c_str());
        return false;
    }

    // Build the module by hand and publish it in sys.modules. Registering it
    // through the import table would make the interpreter call the init
    // function again, which is not what an embedded host wants.
    PyObject* module = build_bge_module();
    if (!module) {
        last_error_ = describe_python_error();
        BGE_ERROR("failed to build the bge module: %s", last_error_.c_str());
        return false;
    }
    Py_INCREF(module);
    if (PyObject* modules = PyImport_GetModuleDict()) {
        PyDict_SetItemString(modules, "bge", module);
    }
    impl_->module = module;
    impl_->initialized = true;

    if (PyObject* set_background = PyObject_GetAttrString(impl_->module, "app")) {
        if (PyObject_SetAttrString(set_background, "background", desc.headless ? Py_True : Py_False) < 0) {
            PyErr_Clear();
        }
        Py_DECREF(set_background);
    }

    if (!desc.startup_source.empty()) {
        run_string(desc.startup_source, "<startup>");
    }
    if (!desc.startup_script.empty()) {
        run_file(desc.startup_script);
    }
    BGE_INFO("Python host ready (bge module registered)");
    return true;
#else
    (void)desc;
    last_error_ = "the engine was built without CPython support";
    return false;
#endif
}

void PythonHost::shutdown() {
#if defined(BGE_WITH_PYTHON)
    if (!impl_ || !impl_->initialized) return;
    if (impl_->module) {
        Py_DECREF(impl_->module);
        impl_->module = nullptr;
    }
    if (Py_IsInitialized()) {
        Py_FinalizeEx();
    }
    impl_->initialized = false;
    g_scene = nullptr;
#endif
}

bool PythonHost::available() const {
#if defined(BGE_WITH_PYTHON)
    return impl_ && impl_->initialized;
#else
    return false;
#endif
}

void PythonHost::set_scene(Scene* scene) {
#if defined(BGE_WITH_PYTHON)
    g_scene = scene;
#else
    (void)scene;
#endif
}

bool PythonHost::run_string(const std::string& source, const std::string& name) {
#if defined(BGE_WITH_PYTHON)
    if (!impl_ || !impl_->initialized) {
        last_error_ = "the Python host is not running";
        return false;
    }
    PyObject* globals = PyModule_GetDict(impl_->module);
    PyObject* result = PyRun_String(source.c_str(), Py_file_input, globals, globals);
    if (!result) {
        last_error_ = describe_python_error();
        BGE_ERROR("script error: %s", last_error_.c_str());
        return false;
    }
    Py_DECREF(result);
    last_error_.clear();
    return true;
#else
    (void)source;
    (void)name;
    last_error_ = "the engine was built without CPython support";
    return false;
#endif
}

int PythonHost::run_controllers() {
#if defined(BGE_WITH_PYTHON)
    if (!impl_ || !impl_->initialized || !g_scene) return 0;
    PyObject* globals = PyModule_GetDict(impl_->module);

    struct Pending {
        long handle;
        std::string script;
    };
    std::vector<Pending> pending;
    g_scene->world().each<Controller>([&](Entity e, Controller& controller) {
        if (!controller.enabled || controller.script.empty()) return;
        pending.push_back({static_cast<long>(e.index), controller.script});
    });

    int executed = 0;
    for (const Pending& item : pending) {
        long previous = g_current_controller;
        g_current_controller = item.handle;
        PyObject* result = PyRun_String(item.script.c_str(), Py_file_input, globals, globals);
        g_current_controller = previous;
        if (!result) {
            last_error_ = describe_python_error();
            BGE_ERROR("controller error: %s", last_error_.c_str());
            continue;
        }
        Py_DECREF(result);
        ++executed;
    }
    return executed;
#else
    return 0;
#endif
}

bool PythonHost::run_file(const std::string& path) {
#if defined(BGE_WITH_PYTHON)
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        last_error_ = "script not found: " + path;
        BGE_ERROR("%s", last_error_.c_str());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return run_string(buffer.str(), path);
#else
    (void)path;
    last_error_ = "the engine was built without CPython support";
    return false;
#endif
}

void PythonHost::tick(float delta_seconds) {
#if defined(BGE_WITH_PYTHON)
    if (!impl_ || !impl_->initialized) return;
    g_delta = delta_seconds;
    ++frame_;

    PyObject* dispatch = PyObject_GetAttrString(impl_->module, "_dispatch");
    if (!dispatch) {
        PyErr_Clear();
        return;
    }
    PyObject* result = PyObject_CallFunction(dispatch, "ff", static_cast<float>(delta_seconds),
                                             static_cast<float>(frame_));
    Py_DECREF(dispatch);
    if (!result) {
        PyErr_Print();
        return;
    }
    Py_DECREF(result);
#else
    (void)delta_seconds;
    ++frame_;
#endif
}

} // namespace bge
