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

import os
import gzip
import json
import logging

from typing import Any, Optional

from yugabyte.logging_util import log_info, log_error
from yugabyte import file_util


JSON_INDENTATION = 2


def write_json_file(
        json_data: Any,
        output_path: str,
        description_for_log: Optional[str] = None) -> None:
    with open(output_path, 'w') as output_file:
        json.dump(json_data, output_file, indent=JSON_INDENTATION)
        if description_for_log is not None:
            log_info("Wrote %s: %s", description_for_log, output_path)


def read_json_file(input_path: str, allow_comments: bool = False) -> Any:
    """
    Reads the given JSON file and returns a parsed JSON structure. If the file path has a .gz
    extension, the file is assumed to be gzipped.

    :param input_path: The path to the JSON file to read.
    :param allow_comments: Whether to allow comments (starting with # or //) in the JSON file.
        This is less efficient than not allowing comments, because it requires reading the
        entire file into memory and removing comments before parsing the JSON.
    :return: The parsed JSON structure.
    """
    if not os.path.exists(input_path):
        log_error("JSON file not found: %s", input_path)
        raise FileNotFoundError("JSON file not found: %s" % input_path)

    if allow_comments:
        content = file_util.read_file(input_path).strip()
        lines = content.splitlines()
        json_lines = [
            line for line in lines
            if not line.strip().startswith('#') and not line.strip().startswith('//')
        ]
        content_without_comments = '\n'.join(json_lines).strip()
        try:
            return json.loads(content_without_comments)
        except json.decoder.JSONDecodeError as ex:
            log_info("Failed reading JSON file %s: %s", input_path, ex)
            if content_without_comments != content:
                log_info("JSON content with comments removed:\n%s", content_without_comments)
            raise

    try:
        if input_path.endswith('.gz'):
            with gzip.open(input_path, 'rt') as input_file:
                return json.load(input_file)
        else:
            with open(input_path) as input_file:
                return json.load(input_file)
    except json.decoder.JSONDecodeError as ex:
        logging.error("Failed reading JSON file %s", input_path, ex)
        raise


def json_to_str(json_data: Any) -> str:
    return json.dumps(json_data, indent=JSON_INDENTATION)
