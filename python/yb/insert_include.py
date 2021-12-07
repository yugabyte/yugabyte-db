import sys


def process(include, file):
    line_to_insert = '#include {}'.format(include)
    path = line_to_insert[:line_to_insert.rfind('/') + 1]

    with open(file) as inp:
        lines = [line.rstrip() for line in inp]

    insert_idx = None
    perfect_idx = None
    for idx in range(len(lines)):
        line = lines[idx]
        if line == line_to_insert:
            sys.stdout.write("Already present in {}\n".format(file))
            return
        if line.startswith('#include ') and line < line_to_insert:
            if line.startswith(path):
                perfect_idx = idx
            else:
                insert_idx = idx
    if perfect_idx is None:
        perfect_idx = insert_idx
    if perfect_idx is None:
        sys.stderr.write("Unable to find place to insert in {}\n".format(file))
        return
    lines.insert(perfect_idx + 1, line_to_insert)
    sys.stdout.write("Inserting to {}\n".format(file))
    with open(file, 'w') as out:
        for line in lines:
            out.write(line + '\n')


def main() -> None:
    include = sys.argv[1]

    for file in sys.argv[2:]:
        process(include, file)


if __name__ == '__main__':
    main()
