from quelling_blade.arena_allocatable import ArenaAllocatable

arena = ArenaAllocatable.arena(2 ** 28)
ob = ArenaAllocatable()

for _ in range(60000000):
    ob.a = 1
