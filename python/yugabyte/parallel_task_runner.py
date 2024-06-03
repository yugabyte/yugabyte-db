# Copyright (c) Yugabyte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

import traceback
import logging
import time
import sys

from typing import Iterable, Tuple, Optional

import concurrent.futures

from typing import List, Any, Type
from dataclasses import dataclass

from yugabyte.type_util import assert_type

from yugabyte.file_util import write_file


@dataclass
class TaskRunnerProgressInfo:
    total_num_tasks: int
    num_finished_tasks: int
    num_successful_tasks: int
    elapsed_time_sec: float

    @property
    def num_failed_tasks(self) -> int:
        return self.num_finished_tasks - self.num_successful_tasks

    @property
    def percent_finished_tasks(self) -> float:
        if self.total_num_tasks == 0:
            return 0
        return self.num_finished_tasks / self.total_num_tasks * 100

    @property
    def estimated_tasks_per_sec(self) -> float:
        if self.elapsed_time_sec == 0:
            return 0
        return self.num_finished_tasks / self.elapsed_time_sec

    @property
    def num_remaining_tasks(self) -> int:
        return self.total_num_tasks - self.num_finished_tasks

    @property
    def estimated_remaining_time_sec(self) -> float:
        estimated_tasks_per_sec = self.estimated_tasks_per_sec
        if estimated_tasks_per_sec == 0:
            return 0
        return self.num_remaining_tasks / self.estimated_tasks_per_sec


class ReportHelper:
    lines: List[str]

    def __init__(self) -> None:
        self.lines = []

    def add_item(self, description: str, value: str) -> None:
        """
        Adds an item to the report. If the value contains a newline, it will be added on a separate
        line, followed by an empty line.
        """
        if '\n' in value:
            line_str = '{}:\n{}\n'.format(description, value.rstrip())
        else:
            line_str = '{}: {}'.format(description, value)
        self.lines.append(line_str)

    def add_items(self, items: Iterable[Optional[Tuple[str, str]]]) -> None:
        for maybe_item in items:
            if maybe_item is not None:
                description, value = maybe_item
                self.add_item(description, value)

    def as_str(self) -> str:
        return '\n'.join(self.lines)

    def add_raw_line(self, line: str) -> None:
        self.lines.append(line)

    def add_raw_lines(self, lines: Iterable[str]) -> None:
        self.lines.extend(lines)

    def write_to_file(self, file_path: str) -> None:
        write_file(content=self.as_str(), output_file_path=file_path)


class ParallelTaskRunner:
    """
    A class for running tasks in parallel. The tasks are run in a thread pool.
    """

    parallelism: int
    task_type: Type
    task_result_type: Type

    def __init__(
            self,
            parallelism: int,
            task_type: Type,
            task_result_type: Type) -> None:
        self.parallelism = parallelism
        self.task_type = task_type
        self.task_result_type = task_result_type

    def assert_task_type(self, task: Any) -> None:
        assert isinstance(task, self.task_type), "Expected task of type {}, got {}: {}".format(
            self.task_type, type(task), task)

    def run_task(self, task: Any) -> Any:
        self.assert_task_type(task)

    def did_task_succeed(self, task_result: Any) -> bool:
        assert_type(self.task_result_type, task_result)
        return True

    def report_progress(self, progress_info: TaskRunnerProgressInfo) -> None:
        logging.info(
            "Processed %d/%d (%.2f%%) tasks, succeeded: %d (%.2f%%), failed: %d (%.2f%%), "
            "elapsed time %.1f sec, estimated remaining time %.1f sec",
            progress_info.num_finished_tasks,
            progress_info.total_num_tasks,
            progress_info.percent_finished_tasks,
            progress_info.num_successful_tasks,
            progress_info.num_successful_tasks / progress_info.num_finished_tasks * 100,
            progress_info.num_failed_tasks,
            progress_info.num_failed_tasks / progress_info.num_finished_tasks * 100,
            progress_info.elapsed_time_sec,
            progress_info.estimated_remaining_time_sec)

    def report_task_exception(self, task: Any, exc: Exception) -> None:
        logging.error(f"Task {task} generated an exception: {traceback.format_exc()}")

    def report_task_result(self, task: Any, task_result: Any, succeeded: bool) -> None:
        pass

    def run_tasks(self, tasks: List[Any]) -> None:
        for task in tasks:
            self.assert_task_type(task)

        num_successes = 0
        num_failures = 0
        start_time_sec = time.time()
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.parallelism) as executor:
            future_to_task = {
                executor.submit(self.run_task, task): task
                for task in tasks
            }

            total_num_tasks = len(tasks)
            num_finished = 0
            for future in concurrent.futures.as_completed(future_to_task):
                task = future_to_task[future]

                succeeded = True
                task_result = None
                try:
                    task_result = future.result()
                    if not isinstance(task_result, self.task_result_type):
                        logging.warning(
                            "Expected task result of type {}, got {}: {}".format(
                                self.task_result_type, type(task_result), task_result))
                        task_result = None
                        succeeded = False
                except Exception as exc:
                    self.report_task_exception(task, exc)
                    succeeded = False
                else:
                    if not self.did_task_succeed(task_result):
                        succeeded = False

                num_finished += 1
                if succeeded:
                    num_successes += 1
                else:
                    num_failures += 1

                elapsed_time_sec = time.time() - start_time_sec
                if task_result is not None:
                    try:
                        self.report_task_result(task, task_result, succeeded)
                    except Exception as ex:
                        logging.exception("Error while reporting task result")

                self.report_progress(TaskRunnerProgressInfo(
                    total_num_tasks=total_num_tasks,
                    num_finished_tasks=num_finished,
                    num_successful_tasks=num_successes,
                    elapsed_time_sec=elapsed_time_sec
                ))
                sys.stdout.flush()
                sys.stderr.flush()
