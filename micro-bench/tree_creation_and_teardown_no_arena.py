from quelling_blade.arena_allocatable import ArenaAllocatable


def f():
    for _ in range(10000):
        root = ob = ArenaAllocatable()
        for _ in range(20000):
            new = ArenaAllocatable()
            ob.a = new
            ob = new
        del new
        del ob
        del root  # actually release the tree


f()
