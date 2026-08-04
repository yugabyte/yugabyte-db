import re
import os
import sys

NODE_RE = re.compile(R'^\s*"(\w+)" of type.*$')
EDGE_RE = re.compile(R'^\s*depends on "(\w+)".*$')

nodes = {}
queue = []
stack = []


class Node:
    def __init__(self, name):
        self.name = name
        self.edges = []
        self.visited = 0

    def dfs(self, phase):
        if self.visited == phase:
            return
        if phase == 2:
            stack.append(self.name)
        self.visited = phase
        for name in self.edges:
            node = nodes[name]
            if phase == 2 and node.visited == phase:
                idx = stack.index(name)
                print('Cycle found:', stack[idx:])
            node.dfs(phase)
        if phase == 1:
            queue.append(self)
        elif phase == 2:
            stack.pop()


def main():
    with open(sys.argv[1]) as inp:
        current_node = None
        for line in inp:
            m = NODE_RE.match(line)
            if m:
                name = m[1]
                current_node = Node(name)
                nodes[name] = current_node
                continue
            m = EDGE_RE.match(line)
            if m:
                name = m[1]
                current_node.edges.append(name)
    for node in nodes.values():
        node.dfs(1)
    queue.reverse()
    for node in queue:
        node.dfs(2)


if __name__ == '__main__':
    main()
