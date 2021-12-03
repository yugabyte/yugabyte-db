import os
import statistics
import subprocess
import time


def n2s(n):
    return int(n * 1000) / 1000


def main() -> None:
    os.chdir("build/latest")
    includes = []
    with open("inc.txt") as inp:
        for line in inp:
            tokens = line.rstrip().split(" ")
            count = None
            for token in tokens:
                if len(token) != 0:
                    count = int(token)
                    break
            fname = tokens[-1]
            if not fname.endswith(".h"):
                continue
            for prefix in ["../../src/", "src/", "../../ent/src/"]:
                if fname.startswith(prefix):
                    includes.append((count, fname[len(prefix):]))
                    break

    includes.reverse()
    for (count, include) in includes:
        with open("../../src/yb/master/empty.cc", 'w') as out:
            out.write('#include "{}"\n'.format(include))
        min_time = 1e9
        max_time = 0
        times = []
        msg = None
        iterations = 3
        for i in range(iterations):
            start = time.monotonic()
            res = subprocess.run(['bash', 'cmd.sh'], stderr=subprocess.DEVNULL)
            if res.returncode != 0:
                msg = "failed {}".format(res.returncode)
                break
            finish = time.monotonic()
            passed = finish - start
            if passed < min_time:
                min_time = passed
            if passed > max_time:
                max_time = passed
            times.append(passed)
        if msg is None:
            med_time = statistics.median(times)
            msg = 'min {} max {} med {}'.format(n2s(min_time), n2s(max_time), n2s(med_time))
            impact = med_time * count
        else:
            impact = -1
        print("{} {} {}: {}".format(n2s(impact), count, include, msg), flush=True)


if __name__ == '__main__':
    main()
