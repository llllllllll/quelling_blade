quelling blade (wip)
====================

Arena allocator for Python objects, designed to quickly cut down trees.

What?
-----

TODO


Example
-------

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
   # When we exit the ``qb.Arena`` context we check to make sure all the
   # objects are dead. If any objects are alive, we make them an owner of the
   # entire arena and throw a ``PerformanceWarning``. Until the escaped objects
   # are deallocated, the entire arena will stay alive. If we have used the
   # context correctly, all the objects will be dead already so we can release
   # the storage in one shot.


   with qb.Arena(Node):
       # Bind the result of ``do_work`` to a variable that will outlive the
       # ``qb.Arena`` context. Memory cannot be freed when we exit the context
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
