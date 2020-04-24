====================
quelling blade (wip)
====================

Arena allocatable user defined types in Python.

.. quote::

   The axe of a fallen gnome, it allows you to effectively maneuver the forest.

What?
=====

CPython Implementation Details
------------------------------

CPython allocates all objects on the heap and uses a reference counting garbage collector.
When an object hits 0 references, it is deallocated immediately.
CPython also has a mechanism for detecting cycles and freeing them.

Instances of types defined in Python (without ``__slots__``) are laid out memory as:

.. code-block:: c

   struct PyObject {
       uintptr_t _gc_next;  /* support for the cycle detector */
       uintptr_t _gc_prev;  /* support for the cycle detector */
       Py_ssize_t ob_refcnt;  /* number of references to this object */
       PyTypeObject* ob_type;  /* pointer to the Python type of this object */
       PyObject* dict;  /* the ``__dict__`` for this object */
       PyWeakReference** weaklist;  /* array of weak references to this object */
   };


A PyObject takes a total of 48 bytes of memory.
However, an empty ``dict`` in python takes up 64 bytes so an object requires a minimum of 112 bytes.


Note: A ``PyObject*`` points to the ``ob_refcnt`` field, the ``_gc_*`` fields live *in front* of the object, but the memory is still being used.


Potential Issues Caused By This Model
-------------------------------------

Lots of Heap Allocations
~~~~~~~~~~~~~~~~~~~~~~~~

A consequence of allocating all objects on the heap is that objects that are related are not close to each other in memory, increasing the amount of main memory reads that must happen.
The object's ``__dict__`` is a pointer to the dictionary instead of being embedded in the object directly.
Because the ``__dict__`` is a pointer, accessing any attribute of an object will require a second pointer dereference to the dictionary which is in an arbitrary location in the heap [1]_.
The Python ``dict`` object itself has an internal heap allocation to store the contents of the dictionary which must also be accessed.
This adds a lot of overhead on each attribute access.
Heap fragmentation is hard to observe in micro benchmarks because accessing the same attribute from the same object in a loop will force all of the relevant objects to stay in the processor's cache and the dereferences will appear very fast.
Real programs do not usually access objects in this way, and instead access an attribute only once or twice before moving on to another object which is not in the cache.

Slow Destruction
~~~~~~~~~~~~~~~~

A consequence of a reference counted garbage collector is that it makes deallocation of graphs expensive.
In this context, a graph is any Python object which contains more Python objects.
Once the root node reaches zero references, the graph must be unwound by decrementing the reference count of each of its children.
If the node is the sole owner of any of its children, then those that node must unwind itself by decrementing the references of its own children.
This can make deallocation of the graph linear in the number of nodes.

Another place where this garbage collection scheme can be an issue is when latency is more important than keeping the memory usage low.
An example of this is a webserver which takes in some input, does a small amount of processing, and returns some data to the user.
For each request, the programmer may know that the memory cannot exceed some reasonable bound and therefore it would be safe to keep all of the temporary objects alive until the request has been served.
Instead, CPython will spend time freeing each intermediate object along the way.
It

Arena Allocation
----------------

Arena allocation works by creating distinct regions, called arenas, to provide storage for a collection of objects.
The arena is not released until all of the objects in the arena are done being used.
Arenas can guarantee that data that is likely to be used together is physically close together.
Having related objects close to each other in memory is better for the CPU memory cache and reduces the number of trips to main memory.

Another advantage of arena allocation is that the entire arena may be released at once.
If the data structures being used inside the arena are designed correctly, each individual destructor need not be called.
This transforms and ``O(n)`` deallocation into an ``O(1)`` deallocation.

API and Usage
=============

Quelling blade provides two types: ``ArenaAllocatable`` and ``Arena``.

``ArenaAllocatable``
--------------------

``ArenaAllocatable`` is a type which can be used instead of ``object`` as a base class to create new types whose instances may be allocated in an arena.
``ArenaAllocatable`` subclasses behave like normal Python types with the following restrictions:

- cannot use ``__slots__``
- cannot access the ``__dict__``.

``Arena``
---------

The ``Arena`` type is meant to be used as a context manager which manages the scope of an arena.
The constructor for ``Arena`` takes either a single subclass of ``ArenaAllocatable`` or a list of subclasses of ``ArenaAllocatable``.
Inside the ``Arena`` context, all new instances of any of the provided types (or subclasses of any of the provided types) will be allocated inside the same arena.
None of the objects will be deallocated until the later of:

1. The arena context closes (or the arena object is deallocated)
2. None of the objects in the arena are available Python anymore.

Escaped Instances
-----------------

Extension modules should never allow a Python programmer to crash the program or otherwise violate the memory safety of Python.
Normal C++ arena allocators would not go out of their way to detect objects escaping the arena.
Instead the documentation would advise programmers on how to use the tool safely.
Python programmers are not used to dealing with the details of object lifetimes while programming in Python.
Therefore, quelling blade must ensure that the state of the program is valid when objects escape an arena context.

When an object lives past the end of the ``Arena`` context manager where it was created, the object becomes an "owner" of its own arena.
None of the objects in the arena can be deallocated until there are no more escaped references
None of the attributes of any ``ArenaAllocatable`` object will be released until the entire arena can be safely destroyed.
When the last escaped reference is released, the entire arena will be torn down at once, freeing all memory and releasing all attributes.

When quelling blade detects that some objects have been released, a ``PerformanceWarning`` will be issued with the number of escaped references.
At this point, the programmer can attempt to debug their program to find where the objects are escaping to Python.

Example Usage
-------------

In the following example, a binary tree class named ``Node`` is defined.
``Node`` is a subclass of ``quelling_blade.ArenaAllocatable``.
The ``Node`` type holds three attributes: a value, a left child, and a right child.
The value may be any type of Python object.
The left and right children may be either ``Node`` objects or ``None``.
The ``do_work`` function creates a tree and then sorts the nodes to be used as a binary search tree.
This work load both creates nodes, access attributes from them, and then creates new nodes.
This is meant to simulate a real work load that uses trees.

.. code-block:: python

   import quelling_blade as qb


   class Node(qb.ArenaAllocatable):
       """A simple binary tree node.

       Parameters
       ----------
       value : any
           The value of the node.
       left : Node or None, optional
           The left side of the tree.
       right : Node or None, optional
           The right side of the tree.
       """
       def __init__(self, value, left=None, right=None):
           self.value = value
           self.left = left
           self.right = right

       def pretty(self, level=0):
           t_indent = '  ' * level
           v_indent = '  ' * (level + 1)
           if self.left is None:
               left = f'{v_indent}None'
           else:
               left = self.left.pretty(level + 1)
           if self.right is None:
               right = f'{v_indent}None'
           else:
               right = self.right.pretty(level + 1)

           return (
               f'{t_indent}{type(self).__name__}(\n'
               f'{v_indent}{self.value!r},\n'
               f'{left},\n'
               f'{right},\n'
               f'{t_indent})'
           )

       def pprint(self):
           print(self.pretty())

       def __iter__(self):
           yield self
           if self.left is not None:
               yield from self.left
           if self.right is not None:
               yield from self.right


   def create_tree():
       """Create a binary tree with letters.
       """
       return Node(
           'a',
           Node(
               'b',
               Node(
                   'c',
                   Node('d'),
                   Node('e'),
               ),
               Node(
                   'f',
                   Node('g'),
                   Node('h'),
               ),
           ),
           Node(
               'i',
               Node(
                   'j',
                   Node('k'),
                   Node('l'),
               ),
               Node(
                   'm',
                   Node('n'),
                   Node('o'),
               ),
           ),
       )


   def _sort_rec(vals):
       if len(vals) == 0:
           return None
       if len(vals) == 1:
           return Node(vals[0])

       pivot = len(vals) // 2
       return Node(
           vals[pivot],
           _sort_rec(vals[:pivot]),
           _sort_rec(vals[pivot + 1:]),
       )


   def sort(tree):
       """Sort a tree.
       """
       return _sort_rec(sorted((n.value for n in tree)))


   def do_work(msg, ret=False):
       """A function which creates a tree and processes it.
       """
       print(msg)

       # allocate some objects
       tree = create_tree()

       # process the objects and allocate some more
       sorted_tree = sort(tree)

       # Both ``tree`` and ``sorted_tree`` fall out of scope here. This should
       # recursively destroy all of the nodes created.

       if ret:
           return sorted_tree
       return None


   # do work like normal, objects are allocated whereever and there is nothing
   # special about how ``Node`` objects are allocated in ``do_work``.
   do_work('global scope')


   with qb.Arena(Node):
       # In this context, all ``Node`` instances, and instances of subclasses
       # of ``Node``,  are allocated in a shared arena.

       # Do work in an arena. This means that the ``Node`` objects in ``do_work``
       # is allocated in the same arena, which means that all the nodes will be
       # laid out in set of a contiguous buffers. When ``tree`` and
       # ``sorted_tree`` fall out of scope, the objects in the arena will be
       # marked as "dead", but no memory is deallocated.
       do_work('in context')
   # When the ``qb.Arena`` context is exited, check to make sure all the
   # objects are dead. If any objects are alive, make them an owner of the
   # entire arena and throw a ``PerformanceWarning``. Until the escaped objects
   # are deallocated, the entire arena will stay alive. If the context is used
   # correctly, all the objects will be dead already so the storage can
   # be released in one shot.


   with qb.Arena(Node):
       # Bind the result of ``do_work`` to a variable that will outlive the
       # ``qb.Arena`` context. Memory cannot be freed when the context is exited
       # because that would invalidate `the `escaped`` object. Instead, warn the
       # user that an object has escaped and make the object own *all* of the
       # memory. This means that none of the objects in the arena will be released
       # until ``escaped`` is destroyed.
       escaped = do_work('escape context', ret=True)


produces:

.. code-block::

   global scope
   in context
   escape context
   examples/readme_example.py:152: RuntimeWarning: 1 object is still alive at arena exit
     escaped = do_work('escape context', ret=True)

Design
======

Quelling blade aims to make allocation, reads, writes, and destruction of objects faster than default Python objects.

``ArenaAllocatableMeta``
------------------------

Quelling blade uses a metaclass for types that subclass ``ArenaAllocatable``.
The metaclass is needed to store C++ data on the class objects themselves.
Each ``ArenaAllocatable`` type (instances of ``ArenaAllocatableMeta``) contains a regular Python type object's fields with the addition of a stack of C++ arenas.
The stack initially begins empty, meaning instances should be allocated globally and freed when their reference count hits zero.
The arena stack is implemented as a ``std::vector<std::shared_ptr<qb::arena>>``.
A shared pointer is used to implement reference counted lifetime for the arena.
The reference counting on the C++ arena will be discussed more when describing ``ArenaAllocatable`` instances.

To allocated a new ``ArenaAllocatable`` instance, the arena stack must be checked.
If the stack is empty, instances will be allocated globally and have normal Python object lifetime rules.
If the stack is non-empty, the instance will be allocated in the arena on the top of the stack [2]_.

``Arena``
---------

Slabs
~~~~~

An _arena_ is a collection of one or more fixed-size allocations.
Each fixed-size allocation in the arena is called a *slab*.
Each *slab* in an arena has the same capacity.
An arena may grow to contain an arbitrary number of slabs, but the number of slabs will never decrease.
The last slab added to the arena is known as the *active slab*.
Each slab contains a size which indicates how many bytes have been allocated out of the slab.

To allocate a new object in an arena:

- If the allocation size is greater than the arena's slab capacity: fail.
- If there is room, increment the size of the slab by the number of bytes requested plus any alignment padding bytes.
- If there is not room in the active slab, create a slab and mark it as the active slab.
  Increment the size of the new active slab by the number of bytes requested plus any alignment padding bytes.

External Objects
~~~~~~~~~~~~~~~~

In addition to slabs, each arena contains a multiset of Python object references called the *external references*.
The entries in the external references multiset are pointers to objects that are owned by the objects that are allocated in the arena.
For example: if a there is a Python object allocated in the arena with two attributes
``a = 'attr`` and ``b = None``, then there will be four entries in the external references:

- ``'attr'``
- ``None``
- ``'a'`` (attribute name)
- ``'b'`` (attribute name)

The attributes are not stored as Python objects because Python already requires that attribute names be ``str`` objects.

When the arena entire arena is destroyed, each reference in the external references will be released.

The memory for this multiset is allocated out of the arena itself so that all of the operations on objects in the arena stay within the arena.

Arena Stack
~~~~~~~~~~~

When the ``Arena`` Python context manager is entered, a new C++ arena is allocated behind a ``std::shared_ptr<qb::arena>``.
For each type that is going to participate in this arena, the new C++ arena is pushed onto the type's arena stack.
The full set of types is not just the explicitly referenced types, but also all of the subclasses of these types.

When the context is exited, the top entry is popped from each type's arena [2]_.
This may not free the underlying C++ arena yet.
The C++ arena is allocated behind a reference counted pointer, and there may still be references that exist at this point.
If there are more references to the arena when the context is closed, it means that instances have escaped the arena.

``ArenaAllocatable``
--------------------

Arena allocatable instances are laid out in memory differently from regular Python objects.
Arena allocatable instances are laid out in memory like:

.. code-block:: c++

struct arena_allocatable {
    Py_ssize_t ob_refcnt;
    PyTypeObject* ob_type;
    std::shared_ptr<arena> owning_arena;
    absl::flat_hash_map<PyObject*, PyObject*>;
};


   struct PyObject {
       uintptr_t _gc_next;  /* support for the cycle detector */
       uintptr_t _gc_prev;  /* support for the cycle detector */
       Py_ssize_t ob_refcnt;  /* number of references to this object */
       PyTypeObject* ob_type;  /* pointer to the Python type of this object */
       PyObject* dict;  /* the ``__dict__`` for this object */
       PyWeakReference** weaklist;  /* array of weak references to this object */
   };

Like regular Python objects, they contain a pointer to their Python type object and a reference count.
Unlike regular Python objects, the attributes are not stored in an out-of-band Python dictionary.
Instead, ``ArenaAllocatable`` objects embed a C++ dictionary in the same allocation as the object itself.
This reduces the number of dereferences required to find an attribute.

Detecting Escaped Objects
~~~~~~~~~~~~~~~~~~~~~~~~~

``ArenaAllocatable`` instances use the ``ob_refcnt`` field slightly differently from regular Python objects.
Instead of representing the total number of references, it represents only the references that are not owned by objects in the arena.
When an object has a non-zero reference count, meaning it has escaped the arena, the ``owning_arena`` field is set to be an owning reference to the arena in which the object was allocated.
When an ``ArenaAllocatable`` object is stored as an attribute of another ``ArenaAllocatable`` object which was allocated from the same arena, the reference count is *not* incremented.
``ArenaAllocatable.tp_dealloc``, the function called when an object's reference count reaches 0, is a nop when the instance was allocated in an arena.
``ArenaAllocatable.tp_dealloc`` will leave the object in a usable state and all external references are preserved.

The following methods have extra functionality to support this arena lifetime management and escape detection:

``tp_new``
``````````

When a new instance is allocated, the ``owning_arena`` is set to be an owning reference to the arena the object was allocated in.
If the object is being allocated globally, this is set to ``nullptr``.
New instances start with a reference count of 1 because they begin in an "escaped" state.

``tp_setter``
`````````````

If the object being stored on the arena is also allocated within the same arena, the reference count is not incremented.

``tp_getattr``
``````````````

If the attribute being returned has a reference count of 0, we assert that it was allocated in the same arena as ``self``.
After the assertion, we set the ``owning_arena`` field to a new owning reference to the owning arena.
Then, the reference count is incremented back to 1 and the object is returned to Python.

``tp_dealloc``
``````````````

If the object was allocated in an arena, reset the ``owning_arena`` pointer to drop a reference to the arena.


To Do
=====

- ENH: support weakrefs
- ENH: make ``Arena`` allocator stack thread  or context local
- BUG: implement ``tp_traverse`` on the ``qb.Arena`` object
- BUG: implement ``tp_traverse`` for escaped arena allocatable instances
- BUG: fix arena context teardown in non-stack order, e.g.: enter a, enter b, exit a, exit b.
  See [2]_.


Notes
=====

.. [1] Actually, two more pointers must be dereferenced to do an attribute lookup.
   When an attribute is looked up, first the ``ob_type``\'s ``__dict__`` is checked to see if there is an object that implements both ``tp_descr_get`` and ``tp_descr_set`` with the name being looked up.
   If so, that object's ``tp_descr_get`` is called to return the attribute.
   This is to support the descriptor protocol.

.. [2] This currently a bug.
   The ``Arena`` object should hold onto the smart pointer and remove it from the vector by search from the right.
   This provide more reasonable semantics for:

   - enter arena A
   - enter arena B
   - close arena A
   - close arena B

   Currently, the closing of any ``Arena`` context just closes the most recently opened context.
   Instead, it should close the same arena that it opened.
