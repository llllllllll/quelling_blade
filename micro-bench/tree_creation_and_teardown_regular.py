class Node:
    pass


def f():
    for _ in range(10000):
        root = ob = Node()
        for _ in range(20000):
            new = Node()
            ob.a = new
            ob = new
        del new
        del ob
        del root  # actually release the tree


f()
