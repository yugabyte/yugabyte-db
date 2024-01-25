from __future__ import print_function
import argparse
import threading
import thread
import os
import datetime
import time
import subprocess
import signal
import logging
import re

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
)

DEFAULT_HOME_DIR = "/home/yugabyte/"
PROC_DIR = "/proc"
DOUBLE_HYPHEN = "--"
YB_PREFIX = "yb-"
CONFIG_ARG = "config"
FS_DATA_DIRS_ARG = "fs_data_dirs"
FILE_PATHS_ARG = "file_paths"
PROCESS_NAMES_ARG = "process_names"
TIME_LIMIT_ARG = "time_limit"
CONSECUTIVE_FAILURES_LIMIT_ARG = "consecutive_failures_limit"
EXAMPLE_TXT_FILE = "example.txt"
DEFAULT_HOME_DIR_TEST_PATH = DEFAULT_HOME_DIR + EXAMPLE_TXT_FILE
CONF_PATH = "/conf/server.conf"


def write_to_file(file_paths, result_holder):
    try:
        for file_path in file_paths:
            with open(file_path, 'w') as file:
                file.write("Hello!")
                file.flush()
                os.fsync(file.fileno())
        result_holder['success'] = True
    except Exception as e:
        result_holder['exception'] = e
        logging.error("Error writing to file: %s", e)


def identify_process_pids(pid_gid_dict):
    logging.info("Attempting to identify the pids of the processes: %s", pid_gid_dict.keys())
    try:
        for pid in os.listdir(PROC_DIR):
            if pid.isdigit():
                try:
                    with open(os.path.join(PROC_DIR, pid, 'cmdline'), 'r') as cmdline_file:
                        cmdline = cmdline_file.read()
                    if cmdline:
                        for process_name in pid_gid_dict.keys():
                            if re.search(YB_PREFIX + process_name, cmdline):
                                with open(os.path.join(PROC_DIR, pid, 'stat'), 'r') as stat_file:
                                    stat_info = stat_file.read().split()
                                pgid = stat_info[4]
                                pid_gid_dict[process_name] = (int(pid), int(pgid))
                except IOError as ioe:
                    logging.error("Error identifying the process pid: %s due to %s",
                                  process_name, ioe)
                    continue
    except Exception as e:
        logging.error("Error identifying the process pids: %s", e)


def stop_process(pid_gid_dict):
    logging.info("Attempting to stop the processes: %s", pid_gid_dict.keys())
    for process_name, process_pids in pid_gid_dict.items():
        if process_pids[0] != 0:
            logging.info("Stopping process: %s with pid: %s, pgid: %s",
                         process_name, process_pids[0], process_pids[1])
            try:
                os.killpg(process_pids[1], signal.SIGKILL)
            except Exception as e:
                logging.error("Error stopping process %s: %s", process_name, e)


def main(file_paths, process_names, time_limit=0.5, consecutive_failures_limit=5):
    pid_gid_dict = {key: (0, 0) for key in process_names}
    consecutive_failures = 0
    main_thread = None
    identify_process_pids(pid_gid_dict)

    while True:
        result_dict = {'success': False, 'exception': None}
        try:
            if main_thread is None or not main_thread.is_alive():
                main_thread = threading.Thread(target=write_to_file,
                                               args=(file_paths, result_dict))
                main_thread.start()
            main_thread.join(timeout=time_limit)

            if result_dict['success']:
                # now = datetime.datetime.now()
                # print("{} File write completed within the time limit.".format(str(now)))
                consecutive_failures = 0
            else:
                # now = datetime.datetime.now()
                # print("{} File write operation timed out.".format(str(now)))
                # print("{}".format(str(result_dict['exception'])))
                logging.error("File write operation timed out: %s", result_dict['exception'])
                consecutive_failures += 1

            if consecutive_failures >= consecutive_failures_limit:
                identify_process_pids(pid_gid_dict)
                stop_process(pid_gid_dict)
                consecutive_failures = 0

        except Exception as e:
            logging.error("Exception while running the script: %s", e)
        finally:
            time.sleep(1)


def parse_config_file(config_path):
    args = {}
    check_file = os.path.isfile(config_path)
    if check_file:
        with open(config_path, 'r') as file:
            for line in file:
                line = line.strip()
                key, value = line[2:].split('=')
                if key == PROCESS_NAMES_ARG:
                    args[key] = value.split()
                elif key == TIME_LIMIT_ARG:
                    args[key] = float(value)
                elif key == CONSECUTIVE_FAILURES_LIMIT_ARG:
                    args[key] = int(value)
                elif key == FILE_PATHS_ARG or key == FS_DATA_DIRS_ARG:
                    args[key] = value.split(',')
    return args


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Threaded file write operation with "
                                                 "process control.")
    parser.add_argument(DOUBLE_HYPHEN + CONFIG_ARG, type=str, default='disk_io_detection.conf',
                        help="Specify the configuration file path.")
    parser.add_argument(DOUBLE_HYPHEN + FILE_PATHS_ARG, type=str, default={}, nargs='*',
                        help="Specify the file path.")
    parser.add_argument(DOUBLE_HYPHEN + PROCESS_NAMES_ARG, type=str, default=["tserver", "master"],
                        nargs='*', help="Specify the process names.")
    parser.add_argument(DOUBLE_HYPHEN + TIME_LIMIT_ARG, type=float, default=0.5,
                        help="Set the desired time limit for each execution (in seconds).")
    parser.add_argument(DOUBLE_HYPHEN + CONSECUTIVE_FAILURES_LIMIT_ARG, type=int, default=5,
                        help="Set the number of consecutive failures to trigger service stopping.")

    args_cmd = parser.parse_args()

    config_args = parse_config_file(args_cmd.config)

    for key, value in vars(args_cmd).items():
        if value is not None:
            config_args[key] = value

    file_paths = set({})
    for process in config_args.get(PROCESS_NAMES_ARG):
        config_dict = parse_config_file(DEFAULT_HOME_DIR + process + CONF_PATH)
        process_file_paths = config_dict.get(FS_DATA_DIRS_ARG)
        if process_file_paths is not None:
            for process_file_path in process_file_paths:
                file_paths.add(process_file_path + "/" + EXAMPLE_TXT_FILE)

    file_paths.add(DEFAULT_HOME_DIR_TEST_PATH)
    # print("{} File paths.".format(str(file_paths)))

    logging.info("Running the main method with the args - %s: %s, %s: %s, %s: %s, %s: %s",
                 FILE_PATHS_ARG, file_paths,
                 PROCESS_NAMES_ARG, config_args.get(PROCESS_NAMES_ARG, []),
                 TIME_LIMIT_ARG, config_args.get(TIME_LIMIT_ARG, 0.5),
                 CONSECUTIVE_FAILURES_LIMIT_ARG,
                 config_args.get(CONSECUTIVE_FAILURES_LIMIT_ARG, 5))
    main(file_paths,
         config_args.get(PROCESS_NAMES_ARG, []),
         config_args.get(TIME_LIMIT_ARG, 0.5),
         config_args.get(CONSECUTIVE_FAILURES_LIMIT_ARG, 5))
