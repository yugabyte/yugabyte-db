#!/usr/bin/env python

from distutils.version import LooseVersion
from sys import exit
import argparse
import errno
import glob
import logging
import os
import pipes
import psutil
import shutil
import signal
import subprocess
import tarfile

CONTROLLER_DIR = "/tmp/yugabyte/controller"
CONTROLLER_PID_FILE = CONTROLLER_DIR + "/yb-controller.pid"
PV_CONTROLLER_DIR = "/mnt/disk0/yw-data/controller"
CONTROLLER_DIR_LOGS = "/mnt/disk0/ybc-data/controller/logs"


class YBC:

    def __init__(self):
        self.args = []
        self.parse_arguments()

    def parse_arguments(self):
        parser = argparse.ArgumentParser()
        parser.add_argument(
                    'command', choices=['configure', 'start', 'status', 'stop'],
                    help='Configure, start, stop, get the status of YBC.')
        self.args = parser.parse_args()

    def identify_latest_pkg(self, path):
        if not os.path.exists(path):
            return ""
        dir_list = os.listdir(path)
        dir_list.sort(reverse=True, key=LooseVersion)
        if len(dir_list) == 0:
            return ""
        else:
            return dir_list[0]

    def quote_cmd_line_for_bash(self, cmd_line):
        if not isinstance(cmd_line, list) and not isinstance(cmd_line, tuple):
            raise Exception("Expected a list/tuple, got: [[ {} ]]".format(cmd_line))
        return ' '.join([pipes.quote(str(arg)) for arg in cmd_line])

    def move_contents(self, root_src_dir, root_dst_dir):
        for src_dir, dirs, files in os.walk(root_src_dir):
            dst_dir = src_dir.replace(root_src_dir, root_dst_dir, 1)
            if not os.path.exists(dst_dir):
                os.makedirs(dst_dir)
            for file_ in files:
                src_file = os.path.join(src_dir, file_)
                dst_file = os.path.join(dst_dir, file_)
                if os.path.exists(dst_file):
                    # in case of the src and dst are the same file
                    if os.path.samefile(src_file, dst_file):
                        continue
                    os.remove(dst_file)
                shutil.move(src_file, dst_dir)

    def run_program(self, args):
        cmd_as_str = self.quote_cmd_line_for_bash(args)
        try:
            proc_env = os.environ.copy()
            subprocess_result = subprocess.Popen(
                args, stderr=subprocess.STDOUT,
                env=proc_env)

            logging.info(
                "Output from running command [[ {} ]]:\n{}\n[[ END OF OUTPUT ]]".format(
                    cmd_as_str, subprocess_result))
            return subprocess_result
        except subprocess.CalledProcessError as e:
            logging.error("Failed to run command [[ {} ]]: code={} output={}".format(
                cmd_as_str, e.returncode, str(e.output.decode('utf-8', errors='replace')
                                              .encode("ascii", "ignore")
                                              .decode("ascii"))))
            raise e
        except Exception as ex:
            logging.error("Failed to run command [[ {} ]]: {}".format(cmd_as_str, ex))
            raise ex

    def read_pid_file(self, pid_file_path):
        if os.path.exists(pid_file_path):
            pidfile = open(pid_file_path, 'r')
            line = pidfile.readline().strip()
            pid = int(line)
            return pid
        else:
            return None

    # Fetch the latest ybc package from the PV_CONTROLLER_DIR + "/tmp", untar it and
    # move it to the PV_CONTROLLER_DIR + "/bin" directory.
    def configure(self):
        latest_ybc_pkg = self.identify_latest_pkg(PV_CONTROLLER_DIR + "/tmp")
        # latest_ybc_pkg is the latest package among the all the packages in the
        # PV_CONTROLLER_DIR + "/tmp" directory.
        # ybc-1.0.0-b9-linux-x86_64.tar.gz is an example of latest_ybc_pkg if
        # there are ybc packages in the PV_CONTROLLER_DIR + "/tmp" directory. If the
        # directory is empty, then latest_ybc_pkg is empty.
        # ybc-1.0.0-b9-linux-x86_64 is an example of latest_ybc_prefix for the
        # above latest_ybc_pkg
        if latest_ybc_pkg:
            latest_ybc_prefix = latest_ybc_pkg[0:-7]
            tar = tarfile.open(PV_CONTROLLER_DIR + "/tmp/" + latest_ybc_pkg)
            tar.extractall(PV_CONTROLLER_DIR)
            self.move_contents(PV_CONTROLLER_DIR + "/" + latest_ybc_prefix + "/bin",
                               PV_CONTROLLER_DIR + "/bin")
            shutil.rmtree(PV_CONTROLLER_DIR + "/" + latest_ybc_prefix)

    def run(self):
        self.configure()
        if not os.path.isfile(CONTROLLER_PID_FILE) and \
                os.path.exists(PV_CONTROLLER_DIR + '/bin/yb-controller-server'):
            arguments = [CONTROLLER_DIR + '/bin/yb-controller-server',
                         '--flagfile',
                         PV_CONTROLLER_DIR + '/conf/server.conf']
            subprocess_result = self.run_program(arguments)
            pidfile = open(CONTROLLER_PID_FILE, 'w')
            pidfile.write(str(subprocess_result.pid))
            pidfile.close()

    def stop(self):
        if os.path.exists(CONTROLLER_PID_FILE):
            pid = self.read_pid_file(CONTROLLER_PID_FILE)
            try:
                parent = psutil.Process(pid)
                children = parent.children(recursive=True)
                for p in children:
                    try:
                        os.kill(p.pid, signal.SIGKILL)
                        os.waitpid(p.pid)
                    except Exception:
                        pass
                try:
                    os.kill(parent.pid, signal.SIGKILL)
                    os.waitpid(parent.pid)
                except Exception:
                    pass
            except Exception:
                pass
            os.remove(CONTROLLER_PID_FILE)

    def status(self):
        if os.path.exists(CONTROLLER_PID_FILE):
            pid = None
            try:
                pid = self.read_pid_file(CONTROLLER_PID_FILE)
                if pid is not None and psutil.pid_exists(pid):
                    exit(0)
                else:
                    os.remove(CONTROLLER_PID_FILE)
                    exit(1)
            except Exception:
                os.remove(CONTROLLER_PID_FILE)
                exit(1)
        else:
            exit(1)


if __name__ == "__main__":
    logging.basicConfig(
        format="%(asctime)s [%(levelname)s] %(filename)s: %(message)s",
        level=logging.INFO,
    )

    ybc = YBC()
    if ybc.args.command == 'configure':
        ybc.configure()
    elif ybc.args.command == 'start':
        ybc.run()
    elif ybc.args.command == 'stop':
        ybc.stop()
    elif ybc.args.command == 'status':
        ybc.status()
    else:
        logging.error('Incorrect command provided')
