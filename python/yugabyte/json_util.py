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

import json
import logging

from typing import Any, Optional

from yugabyte.logging_util import log_info


JSON_INDENTATION = 2


def write_json_file(
        json_data: Any, output_path: str, description_for_log: Optional[str] = None) -> None:
    with open(output_path, 'w') as output_file:
        json.dump(json_data, output_file, indent=JSON_INDENTATION)
        if description_for_log is not None:
            log_info("Wrote %s: %s", description_for_log, output_path)


def read_json_file(input_path: str) -> Any:
    try:
        with open(input_path) as input_file:
            return json.load(input_file)
    except:  # noqa: E129
        # We re-throw the exception anyway.
        logging.error("Failed reading JSON file %s", input_path)
        raise
