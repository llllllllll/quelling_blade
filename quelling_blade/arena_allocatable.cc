#include <deque>
#include <memory>
#include <sstream>
#include <type_traits>
#include <vector>

#include <Python.h>
#include <absl/container/flat_hash_map.h>

namespace qb {
template<typename T>
class owned_ref;

/** A type that explicitly indicates that a Python object is a borrowed
    reference. This is implicitly convertible from a regular `PyObject*` or a
    `owned_ref`. This type may be used as a Python object parameter like:

    \code
    int f(borrowed_ref a, borrowed_ref b);
    \endcode

    This allows calling this function with either `owned_ref` or
    `PyObject*`.

    @note A `borrowed_ref` may still hold a value of `nullptr`.
 */
template<typename T = PyObject>
class borrowed_ref {
private:
    T* m_ref;

public:
    constexpr borrowed_ref() : m_ref(nullptr) {}
    constexpr borrowed_ref(std::nullptr_t) : m_ref(nullptr) {}
    constexpr borrowed_ref(T* ref) : m_ref(ref) {}
    constexpr borrowed_ref(const owned_ref<T>& ref) : m_ref(ref.get()) {}

    constexpr T* get() const {
        return m_ref;
    }

    explicit constexpr operator T*() const {
        return m_ref;
    }

    // use an enable_if to resolve the ambiguous dispatch when T is PyObject
    template<typename U = T,
             typename = std::enable_if_t<!std::is_same<U, PyObject>::value>>
    explicit operator PyObject*() const {
        return reinterpret_cast<PyObject*>(m_ref);
    }

    T& operator*() const {
        return *m_ref;
    }

    T* operator->() const {
        return m_ref;
    }

    explicit operator bool() const {
        return m_ref;
    }

    bool operator==(borrowed_ref<> other) const {
        return m_ref == other.get();
    }

    bool operator!=(borrowed_ref<> other) const {
        return m_ref != other.get();
    }
};

/** An RAII wrapper for ensuring an object is cleaned up in a given scope.
 */
template<typename T = PyObject>
class owned_ref {
private:
    T* m_ref;

public:
    /** The type of the underlying pointer.
     */
    using element_type = T;

    /** Default construct a scoped ref to a `nullptr`.
     */
    constexpr owned_ref() : m_ref(nullptr) {}

    constexpr owned_ref(std::nullptr_t) : m_ref(nullptr) {}

    /** Manage a new reference. `ref` should not be used outside of the
        `owned_ref`.

        @param ref The reference to manage
     */
    constexpr explicit owned_ref(T* ref) : m_ref(ref) {}

    constexpr owned_ref(const owned_ref& cpfrom) : m_ref(cpfrom.m_ref) {
        Py_XINCREF(m_ref);
    }

    constexpr owned_ref(owned_ref&& mvfrom) noexcept : m_ref(mvfrom.m_ref) {
        mvfrom.m_ref = nullptr;
    }

    constexpr owned_ref& operator=(const owned_ref& cpfrom) {
        // we need to incref before we decref to support self assignment
        Py_XINCREF(cpfrom.m_ref);
        Py_XDECREF(m_ref);
        m_ref = cpfrom.m_ref;
        return *this;
    }

    constexpr owned_ref& operator=(owned_ref&& mvfrom) noexcept {
        std::swap(m_ref, mvfrom.m_ref);
        return *this;
    }

    /** Create a scoped ref that is a new reference to `ref`.

        @param ref The Python object to create a new managed reference to.
     */
    constexpr static owned_ref new_reference(borrowed_ref<T> ref) {
        Py_INCREF(ref.get());
        return owned_ref{ref.get()};
    }

    /** Create a scoped ref that is a new reference to `ref` if `ref` is non-null.

        @param ref The Python object to create a new managed reference to. If `ref`
               is `nullptr`, then the resulting object just holds `nullptr` also.
     */
    constexpr static owned_ref xnew_reference(borrowed_ref<T> ref) {
        Py_XINCREF(ref.get());
        return owned_ref{ref.get()};
    }

    /** Decref the managed pointer if it is not `nullptr`.
     */
    ~owned_ref() {
        Py_XDECREF(m_ref);
    }

    /** Return the underlying pointer and invalidate the `owned_ref`.

        This allows the reference to "escape" the current scope.

        @return The underlying pointer.
        @see get
     */
    T* escape() && {
        T* ret = m_ref;
        m_ref = nullptr;
        return ret;
    }

    /** Get the underlying managed pointer.

        @return The pointer managed by this `owned_ref`.
        @see escape
     */
    constexpr T* get() const {
        return m_ref;
    }

    explicit operator T*() const {
        return m_ref;
    }

    // use an enable_if to resolve the ambiguous dispatch when T is PyObject
    template<typename U = T,
             typename = std::enable_if_t<!std::is_same<U, PyObject>::value>>
    explicit operator PyObject*() const {
        return reinterpret_cast<PyObject*>(m_ref);
    }

    T& operator*() const {
        return *m_ref;
    }

    T* operator->() const {
        return m_ref;
    }

    explicit operator bool() const {
        return m_ref;
    }

    bool operator==(borrowed_ref<> other) const {
        return m_ref == other.get();
    }

    bool operator!=(borrowed_ref<> other) const {
        return m_ref != other.get();
    }
};

class object_map_key {
private:
    borrowed_ref<> m_ob;

public:
    object_map_key(borrowed_ref<> ob) : m_ob(owned_ref<>::new_reference(ob)) {}
    object_map_key(const owned_ref<>& ob) : m_ob(ob) {}

    object_map_key() = default;
    object_map_key(const object_map_key&) = default;
    object_map_key(object_map_key&&) = default;

    object_map_key& operator=(const object_map_key&) = default;
    object_map_key& operator=(object_map_key&&) = default;

    PyObject* get() const {
        return m_ob.get();
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(m_ob);
    }

    operator const borrowed_ref<>&() const noexcept {
        return m_ob;
    }

    bool operator==(const object_map_key& other) const {
        if (m_ob == other.m_ob) {
            return true;
        }
        if (!m_ob) {
            return !static_cast<bool>(other.m_ob);
        }
        if (!other.m_ob) {
            return false;
        }

        int r = PyObject_RichCompareBool(m_ob.get(), other.get(), Py_EQ);
        if (r < 0) {
            throw std::runtime_error{"failed to compare"};
        }

        return r;
    }

    bool operator!=(const object_map_key& other) const {
        if (m_ob != other.m_ob) {
            return true;
        }

        if (!m_ob) {
            return static_cast<bool>(other.m_ob);
        }
        if (!other.m_ob) {
            return true;
        }

        int r = PyObject_RichCompareBool(m_ob.get(), other.get(), Py_NE);
        if (r < 0) {
            throw std::runtime_error{"failed to compare"};
        }

        return r;
    }
};
}  // namespace qb

namespace std {
template<>
struct hash<qb::object_map_key> {
    auto operator()(const qb::object_map_key& ob) const {
        // this returns a different type in Python 2 and Python 3
        using out_type = decltype(PyObject_Hash(ob.get()));

        if (!ob.get()) {
            return out_type{0};
        }

        out_type r = PyObject_Hash(ob.get());
        if (r == -1) {
            throw std::runtime_error{"python hash failed"};
        }

        return r;
    }
};
}

namespace qb {
class slab {
private:
    struct free_deleter {
        void operator()(std::byte* p) {
            std::free(p);
        }
    };

    std::unique_ptr<std::byte, free_deleter> m_data;
    std::size_t m_size;
    std::size_t m_cap;

public:
    slab(slab&&) = default;

private:
    static std::unique_ptr<std::byte, free_deleter> allocate_slab(std::size_t cap) {
        void* p = malloc(cap);
        if (!p) {
            throw std::bad_alloc{};
        }

        return std::unique_ptr<std::byte, free_deleter>{reinterpret_cast<std::byte*>(p)};
    }

public:
    explicit slab(std::size_t cap) : m_data(allocate_slab(cap)), m_size(0), m_cap(cap) {}

    std::size_t capacity() const {
        return m_cap;
    }

    bool contains(std::byte* p) const {
        return p >= m_data.get() && p <= m_data.get() + capacity();
    }

    std::byte* try_allocate(std::size_t size, std::size_t align) {
        std::size_t align_padding = (align - (m_size % align)) % align;
        if (m_size + align_padding + size > capacity()) {
            return nullptr;
        }
        m_size += align_padding;
        std::byte* out = m_data.get() + m_size;
        m_size += size;
        return out;
    }
};

class arena;
class arena_allocatable_object;

class arena : public std::enable_shared_from_this<arena> {
public:
    template<typename T>
    class allocator {
    private:
        arena* m_arena;

    public:
        using value_type = T;

        explicit allocator(arena* arena) : m_arena(arena) {}

        template<typename U>
        allocator(const allocator<U>& cpfrom) : m_arena(cpfrom.get_arena()) {}

        T* allocate(std::size_t count) {
            if (!m_arena) {
                return new T[count];
            }
            return reinterpret_cast<T*>(m_arena->allocate(count * sizeof(T), alignof(T)));
        }

        void deallocate(T* ptr, std::size_t) {
            if (!m_arena) {
                delete[] ptr;
            }
        }

        arena* get_arena() const {
            return m_arena;
        }
    };

private:
    std::vector<slab> m_slabs;
    std::deque<owned_ref<>, allocator<owned_ref<>>> m_external_references;

    static std::vector<slab> initialize_slabs(std::size_t slab_size) {
        std::vector<slab> out;
        out.emplace_back(slab_size);
        return out;
    }

public:
    arena(arena&&) = delete;

    explicit arena(std::size_t slab_size)
        : m_slabs(initialize_slabs(slab_size)),
          m_external_references(allocator<owned_ref<>>{this}) {}

    bool contains(std::byte* p) const {
        for (const slab& s : m_slabs) {
            if (s.contains(p)) {
                return true;
            }
        }
        return false;
    }

    std::byte* allocate(std::size_t size, std::size_t align) {
        std::size_t capacity = m_slabs.back().capacity();
        if (size > capacity) {
            std::stringstream ss;
            ss << "cannot allocate objects larger than the slab size: " << size << " > "
               << capacity;
            throw std::runtime_error{ss.str()};
        }

        std::byte* out = m_slabs.back().try_allocate(size, align);
        if (!out) {
            m_slabs.emplace_back(capacity);
            out = m_slabs.back().try_allocate(size, align);
            assert(out);
        }
        return out;
    }

    void add_external_reference(borrowed_ref<> ob) {
        m_external_references.emplace_back(owned_ref<>::new_reference(ob));
    }
};

struct arena_allocatable_meta_object : public PyHeapTypeObject {
    std::vector<std::shared_ptr<arena>> arena_stack;
};

namespace arena_allocatable_methods {
void dealloc(PyObject*);
}

namespace arena_allocatable_meta_methods{
PyObject* new_(PyTypeObject* cls, PyObject* args, PyObject* kwargs) {
    owned_ref out{PyType_Type.tp_new(cls, args, kwargs)};
    if (!out) {
        return nullptr;
    }
    int res = PyObject_HasAttrString(out.get(), "__slots__");
    if (res < 0) {
        return nullptr;
    }
    if (res) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot add __slots__ to an ArenaAllocatable type");
        return nullptr;
    }

    auto* as_type = reinterpret_cast<PyTypeObject*>(out.get());
    as_type->tp_flags &= ~Py_TPFLAGS_HAVE_GC;
    as_type->tp_dealloc = arena_allocatable_methods::dealloc;

    try {
        new (&reinterpret_cast<arena_allocatable_meta_object*>(out.get())->arena_stack)
            std::vector<std::shared_ptr<arena>>{};
    }
    catch (const std::exception& e) {
        PyErr_Format(PyExc_RuntimeError, "a C++ error was raised: %s", e.what());
    }
    return std::move(out).escape();
}

void dealloc(PyObject* untyped_self) {
    auto* typed_self = reinterpret_cast<arena_allocatable_meta_object*>(untyped_self);
    typed_self->arena_stack.~vector();
    PyType_Type.tp_dealloc(untyped_self);
}
}  // namespace arena_allocatable_meta_methods

PyTypeObject arena_allocatable_meta_type = {
    // clang-format disable
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    // clang-format enable
    "quelling_blade.arena_allocatable._ArenaAllocatableMeta",  // tp_name
    sizeof(arena_allocatable_meta_object),                     // tp_basicsize
    0,                                                         // tp_itemsize
    arena_allocatable_meta_methods::dealloc,                   // tp_dealloc
    0,                                                         // tp_print
    0,                                                         // tp_getattr
    0,                                                         // tp_setattr
    0,                                                         // tp_reserved
    0,                                                         // tp_repr
    0,                                                         // tp_as_number
    0,                                                         // tp_as_sequence
    0,                                                         // tp_as_mapping
    0,                                                         // tp_hash
    0,                                                         // tp_call
    0,                                                         // tp_str
    0,                                                         // tp_getattro
    0,                                                         // tp_setattro
    0,                                                         // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                                        // tp_flags
    0,                                                         // tp_doc
    0,                                                         // tp_traverse
    0,                                                         // tp_clear
    0,                                                         // tp_richcompare
    0,                                                         // tp_weaklistoffset
    0,                                                         // tp_iter
    0,                                                         // tp_iternext
    0,                                                         // tp_methods
    0,                                                         // tp_members
    0,                                                         // tp_getset
    &PyType_Type,                                              // tp_base
    0,                                                         // tp_dict
    0,                                                         // tp_descr_get
    0,                                                         // tp_descr_set
    0,                                                         // tp_dictoffset
    0,                                                         // tp_init
    0,                                                         // tp_alloc
    arena_allocatable_meta_methods::new_,                      // tp_new
};

struct arena_context_object {
    PyObject head;
    bool popped;
    std::vector<owned_ref<arena_allocatable_meta_object>> cls;
    std::size_t size;
};

namespace arena_context_methods {
PyObject* new_(PyTypeObject*, PyObject*, PyObject*);


PyObject* enter(PyObject* untyped_self, PyObject*) {
    Py_INCREF(untyped_self);
    return untyped_self;
}

int close_impl(borrowed_ref<arena_context_object> self) {
    if (self->popped || !self->cls.size()) {
        return 0;
    }
    long use_count = self->cls.front()->arena_stack.back().use_count();
    long alive = use_count - self->cls.size();
    if (alive) {
        return PyErr_WarnFormat(PyExc_RuntimeWarning,
                                1,
                                "%ld object%s still alive at arena exit",
                                alive,
                                (alive != 1) ? "s are" : " is");
    }
    for (borrowed_ref<arena_allocatable_meta_object> cls : self->cls) {
        cls->arena_stack.pop_back();
    }
    self->popped = true;
    return 0;
}

PyObject* close(PyObject* untyped_self, PyObject*) {
    borrowed_ref self{reinterpret_cast<arena_context_object*>(untyped_self)};
    if (self->popped) {
        PyErr_SetString(PyExc_RuntimeError, "arena context was already closed");
        return nullptr;
    }
    if (close_impl(self)) {
        return nullptr;
    }
    Py_RETURN_NONE;
}

PyObject* exit(PyObject* untyped_self, PyObject*) {
    return close(untyped_self, nullptr);
}

void dealloc(PyObject* untyped_self) {
    borrowed_ref self{reinterpret_cast<arena_context_object*>(untyped_self)};
    if (close_impl(self)) {
        PyErr_WriteUnraisable(untyped_self);
    }
    PyObject_Del(untyped_self);
}

PyMethodDef methods[] = {
    {"close", close, METH_NOARGS, nullptr},
    {"__enter__", enter, METH_NOARGS, nullptr},
    {"__exit__", exit, METH_VARARGS, nullptr},
    {nullptr},
};
}  // namespace arena_context_methods

PyTypeObject arena_context_type = {
    // clang-format disable
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    // clang-format enable
    "quelling_blade.arena_allocatable.Arena",  // tp_name
    sizeof(arena_context_object),              // tp_basicsize
    0,                                         // tp_itemsize
    arena_context_methods::dealloc,            // tp_dealloc
    0,                                         // tp_print
    0,                                         // tp_getattr
    0,                                         // tp_setattr
    0,                                         // tp_reserved
    0,                                         // tp_repr
    0,                                         // tp_as_number
    0,                                         // tp_as_sequence
    0,                                         // tp_as_mapping
    0,                                         // tp_hash
    0,                                         // tp_call
    0,                                         // tp_str
    0,                                         // tp_getattro
    0,                                         // tp_setattro
    0,                                         // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                        // tp_flags
    0,                                         // tp_doc
    0,                                         // tp_traverse
    0,                                         // tp_clear
    0,                                         // tp_richcompare
    0,                                         // tp_weaklistoffset
    0,                                         // tp_iter
    0,                                         // tp_iternext
    arena_context_methods::methods,            // tp_methods
    0,                                         // tp_members
    0,                                         // tp_getset
    0,                                         // tp_base
    0,                                         // tp_dict
    0,                                         // tp_descr_get
    0,                                         // tp_descr_set
    0,                                         // tp_dictoffset
    0,                                         // tp_init
    0,                                         // tp_alloc
    arena_context_methods::new_,               // tp_new
};


namespace arena_context_methods {
PyObject* new_(PyTypeObject*, PyObject* args, PyObject* kwargs) {
    static const char* const keywords[] = {"types", "slab_size", nullptr};
    PyObject* borrowed_types;
    Py_ssize_t slab_size = 1 << 16;
    if (!PyArg_ParseTupleAndKeywords(args,
                                     kwargs,
                                     "O|n:Arena",
                                     const_cast<char**>(keywords),
                                     &borrowed_types,
                                     &slab_size)) {
        return nullptr;
    }

    owned_ref<> types = owned_ref<>::new_reference(borrowed_types);

    int res = PyObject_IsInstance(types.get(), reinterpret_cast<PyObject*>(&PyType_Type));
    if (res < 0) {
        return nullptr;
    }
    if (res) {
        if (!(types = owned_ref{PyTuple_Pack(1, types.get())})) {
            return nullptr;
        }
    }

    owned_ref types_iter{PyObject_GetIter(types.get())};
    if (!types_iter) {
        return nullptr;
    }

    owned_ref out{PyObject_New(arena_context_object, &arena_context_type)};
    if (!out) {
        return nullptr;
    }

    std::shared_ptr<qb::arena> arena;
    try {
        new (&out.get()->popped) bool{false};
        new (&out.get()->cls) std::vector<owned_ref<arena_allocatable_meta_object>>{};
        new (&out.get()->size) std::size_t{static_cast<std::size_t>(slab_size)};


        arena = std::make_shared<qb::arena>(slab_size);
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }

    while (owned_ref type{PyIter_Next(types_iter.get())}) {
        int res = PyObject_IsInstance(type.get(),
                                      reinterpret_cast<PyObject*>(
                                          &arena_allocatable_meta_type));
        if (res < 0) {
            return nullptr;
        }
        if (!res) {
            PyErr_Format(PyExc_TypeError,
                         "%R is not a subclass of ArenaAllocatable",
                         type.get());
            return nullptr;
        }

        borrowed_ref typed_type{
            reinterpret_cast<arena_allocatable_meta_object*>(type.get())};
        try {
            typed_type->arena_stack.emplace_back(arena);
            out->cls.emplace_back(
                owned_ref<arena_allocatable_meta_object>::new_reference(typed_type));
        }
        catch (const std::exception& e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
            return nullptr;
        }
    }
    if (PyErr_Occurred()) {
        return nullptr;
    }

    return reinterpret_cast<PyObject*>(std::move(out).escape());
}
}  // namespace arena_context_methods

struct arena_allocatable_object : public PyObject {
    using members_type =
        absl::flat_hash_map<object_map_key,
                            PyObject*,
                            absl::container_internal::hash_default_hash<object_map_key>,
                            absl::container_internal::hash_default_eq<object_map_key>,
                            arena::allocator<std::pair<const object_map_key, PyObject*>>>;

    std::shared_ptr<arena> owning_arena;
    members_type members;

    arena_allocatable_object(const std::shared_ptr<arena>& arena,
                             borrowed_ref<PyTypeObject> type)
        : PyObject({_PyObject_EXTRA_INIT 1, type.get()}),
          owning_arena(arena),
          // use a "weak" pointer here to break a ref cycle
          members(members_type::allocator_type{arena.get()}) {
    }
};

namespace arena_allocatable_methods {
PyObject* new_(PyTypeObject* cls, PyObject*, PyObject*) {
    try {
        auto& arena_stack =
            reinterpret_cast<arena_allocatable_meta_object*>(cls)->arena_stack;
        if (!arena_stack.size()) {
            Py_INCREF(cls);
            auto* allocation = PyMem_New(arena_allocatable_object, 1);
            new(allocation) arena_allocatable_object(std::shared_ptr<arena>{}, cls);
            return allocation;
        }

        std::byte* allocation =
            arena_stack.back()->allocate(cls->tp_basicsize,
                                         alignof(arena_allocatable_object));
        new (allocation) arena_allocatable_object(arena_stack.back(), cls);
        return reinterpret_cast<PyObject*>(allocation);
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

int setattr(PyObject* untyped_self, PyObject* key_ptr, PyObject* value) {
    // search for a descriptor on the type before looking on the instance
    borrowed_ref<> descr = _PyType_Lookup(Py_TYPE(untyped_self), key_ptr);
    descrsetfunc descrset = descr ? Py_TYPE(descr)->tp_descr_set : nullptr;
    if (descrset) {
        Py_INCREF(descr);
        int res = descrset(descr.get(), untyped_self, value);
        Py_DECREF(descr);
        return res;
    }

    try {
        borrowed_ref self{static_cast<arena_allocatable_object*>(untyped_self)};

        borrowed_ref key{key_ptr};
        auto arena = self->owning_arena;
        if (arena) {
            if (value) {
                arena->add_external_reference(key);
                if (!arena->contains(reinterpret_cast<std::byte*>(value))) {
                    arena->add_external_reference(value);
                }
                self->members.insert_or_assign(key, value);
            }
            else {
                if (self->members.erase(key) == 0) {
                    PyErr_SetObject(PyExc_AttributeError, key.get());
                    return -1;
                }
            }
        }
        else {
            if (value) {
                Py_INCREF(value);  // inc before when overwriting an attr with itself
                auto [it, inserted] = self->members.try_emplace(key, value);
                if (inserted) {
                    Py_INCREF(key.get());
                }
                else {
                    Py_DECREF(it->second);
                    it->second = value;
                }
            }
            else {
                auto search = self->members.find(key);
                if (search == self->members.end()) {
                    PyErr_SetObject(PyExc_AttributeError, key.get());
                    return -1;
                }
                Py_DECREF(search->first.get());
                Py_DECREF(search->second);
                self->members.erase(search);
            }
        }
        return 0;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return -1;
    }
}

PyObject* getattr(PyObject* untyped_self, PyObject* key) {
    borrowed_ref<PyTypeObject> tp = Py_TYPE(untyped_self);
    // search for a descriptor on the type before looking on the instance
    borrowed_ref<> descr = _PyType_Lookup(tp.get(), key);
    descrgetfunc descrget = descr ? Py_TYPE(descr)->tp_descr_get : nullptr;
    if (descrget && PyDescr_IsData(descr)) {
        // data descriptors take precedence over instance data, call the descriptor
        Py_INCREF(descr);
        PyObject* res = descrget(descr.get(), untyped_self, static_cast<PyObject*>(tp));
        Py_DECREF(descr);
        return res;
    }

    try {
        borrowed_ref self{reinterpret_cast<arena_allocatable_object*>(untyped_self)};

        auto it = self->members.find(borrowed_ref{key});
        if (it == self->members.end()) {
            if (descrget) {
                // use the descriptor if available
                Py_INCREF(descr);
                PyObject* res =
                    descrget(descr.get(), untyped_self, static_cast<PyObject*>(tp));
                Py_DECREF(descr);
                return res;
            }
            PyErr_SetObject(PyExc_AttributeError, key);
            return nullptr;
        }
        PyObject* out = it->second;
        if (out->ob_refcnt == 0) {
            assert(self->owning_arena->contains(reinterpret_cast<std::byte*>(out)));
            // add a reference to the arena
            reinterpret_cast<arena_allocatable_object*>(out)->owning_arena =
                self->owning_arena;
        }
        Py_INCREF(out);
        return out;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

void dealloc(PyObject* untyped_self) {
    borrowed_ref self{reinterpret_cast<arena_allocatable_object*>(untyped_self)};

    if (self->owning_arena) {
        // we are in an arena, just drop the ref
        self->owning_arena.reset();
    }
    else {
        // we have no arena, we need to actually clear out the instance and die
        for (auto [k, v] : self->members) {
            Py_DECREF(k.get());
            Py_DECREF(v);
        }
        Py_DECREF(Py_TYPE(untyped_self));
        self->~arena_allocatable_object();
        PyMem_Free(self.get());
    }
}
}  // namespace arena_allocatable_methods

arena_allocatable_meta_object arena_allocatable_type = {PyHeapTypeObject{{
    // clang-format off
    PyVarObject_HEAD_INIT(&arena_allocatable_meta_type, 0)
    // clang-format on
    "quelling_blade.arena_allocatable.ArenaAllocatable",
    sizeof(arena_allocatable_object),
    0,                                         // tp_itemsize
    arena_allocatable_methods::dealloc,        // tp_dealloc
    0,                                         // tp_print
    0,                                         // tp_getattr
    0,                                         // tp_setattr
    0,                                         // tp_reserved
    0,                                         // tp_repr
    0,                                         // tp_as_number
    0,                                         // tp_as_sequence
    0,                                         // tp_as_mapping
    0,                                         // tp_hash
    0,                                         // tp_call
    0,                                         // tp_str
    arena_allocatable_methods::getattr,        // tp_getattro
    arena_allocatable_methods::setattr,        // tp_setattro
    0,                                         // tp_as_buffer
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  // tp_flags
    0,                                         // tp_doc
    0,                                         // tp_traverse
    0,                                         // tp_clear
    0,                                         // tp_richcompare
    0,                                         // tp_weaklistoffset
    0,                                         // tp_iter
    0,                                         // tp_iternext
    0,                                         // tp_methods
    0,                                         // tp_members
    0,                                         // tp_getset
    0,                                         // tp_base
    0,                                         // tp_dict
    0,                                         // tp_descr_get
    0,                                         // tp_descr_set
    0,                                         // tp_dictoffset
    0,                                         // tp_init
    0,                                         // tp_alloc
    arena_allocatable_methods::new_,           // tp_new
}}};

PyModuleDef module = {PyModuleDef_HEAD_INIT,
                      "quelling_blade.arena_allocatable",
                      nullptr,
                      -1,
                      nullptr,
                      nullptr,
                      nullptr,
                      nullptr};

PyMODINIT_FUNC PyInit_arena_allocatable() {
    for (PyTypeObject* tp : {&arena_allocatable_meta_type,
                             &arena_allocatable_type.ht_type,
                             &arena_context_type}) {
        if (PyType_Ready(tp) < 0) {
            return nullptr;
        }
    }

    owned_ref mod{PyModule_Create(&module)};
    if (!mod) {
        return nullptr;
    }

    if (PyObject_SetAttrString(mod.get(),
                               "ArenaAllocatable",
                               reinterpret_cast<PyObject*>(&arena_allocatable_type))) {
        return nullptr;
    }
    if (PyObject_SetAttrString(mod.get(),
                               "Arena",
                               reinterpret_cast<PyObject*>(&arena_context_type))) {
        return nullptr;
    }

    return std::move(mod).escape();
}
}  // namespace qb
