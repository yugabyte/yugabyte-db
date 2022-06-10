import json
import multiprocessing
import shlex
import statistics
import subprocess
import sys
import time
import typing


class Job:
    def __init__(self, file: str, command: str):
        self.file = file
        self.command = command
        self.command_args: typing.Union[None, str] = None
        self.process: typing.Union[None, subprocess.Popen] = None
        self.start_time: typing.Union[None, int] = None
        self.times: typing.List[int] = []

    def launch(self):
        if self.command_args is not None:
            raise Exception('Double launch {}'.format(self.file))
        self.command_args = shlex.split(self.command)
        if len(self.command_args) == 0:
            raise Exception('Failed to parse command for {}'.format(self.file))
        self.execute()

    def execute(self):
        self.start_time = time.monotonic_ns()
        self.process = subprocess.Popen(
            self.command_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def poll(self) -> bool:
        return_code = self.process.poll()
        if return_code is None:
            return False
        self.process = None
        stop_time = time.monotonic_ns()
        if return_code != 0:
            sys.stderr.write("Failed to build {}: {}\n".format(self.file, return_code))
        passed = stop_time - self.start_time
        self.times.append(passed)
        if len(self.times) < 5:
            self.execute()
            return False
        med_time = statistics.median(self.times) / 1e9
        print("{:.3} {}".format(int(med_time * 1000) / 1000, self.file), flush=True)
        return True


def main():
    json_str = subprocess.check_output(['ninja', '-t', 'compdb'])
    compdb_json = json.loads(json_str)
    jobs: typing.List[Job] = []
    for entry in compdb_json:
        file = entry['file']
        if file.endswith('.cc') and entry['output'].endswith('.cc.o'):
            command = entry['command']
            jobs.append(Job(file, command))
    max_jobs = multiprocessing.cpu_count() / 2
    next_job = 0
    active_jobs: typing.List[Job] = []
    while True:
        need_wait = True
        while next_job < len(jobs) and len(active_jobs) < max_jobs:
            active_jobs.append(jobs[next_job])
            active_jobs[-1].launch()
            next_job += 1
            need_wait = False
        if len(active_jobs) == 0:
            break
        i = len(active_jobs)
        while i > 0:
            i -= 1
            if active_jobs[i].poll():
                last_job = active_jobs.pop()
                if i < len(active_jobs):
                    active_jobs[i] = last_job
                    need_wait = False
        if need_wait:
            time.sleep(0.01)


if __name__ == '__main__':
    main()
