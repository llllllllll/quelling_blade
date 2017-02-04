from quelling_blade.arena_allocatable import ArenaAllocatable, Arena


def f():
    for _ in range(10000):
        with Arena(ArenaAllocatable, 2 ** 32):
            root = ob = ArenaAllocatable()
            for _ in range(20000):
                new = ArenaAllocatable()
                ob.a = new
                ob = new
            del new
            del ob
            del root  # actually release the tree


f()
