# Copyright (c) YugabyteDB, Inc.
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


"""
Unit tests for csi_report.classify_execution, the retry_kind decision table.

Backport note: master's test_csi_report.py also covers the CSI GET retry helpers, which
do not exist on this branch; only the classify_execution table is carried here.
"""

from typing import Any

import pytest

from yugabyte import csi_report


# ---------------------------------------------------------------------------------------------
# classify_execution: the retry_kind decision table. Only fail_repetition vs repetition is
# exclusive by construction (--fail_repetitions is rejected alongside --num_repetitions > 1);
# a Spark task resubmit (attempt > 0) can occur inside either job, and the branch order in
# classify_execution resolves those overlaps: fail_repetition wins (the "first attempt failed"
# implication must stay exact for consumers), then task_resubmit (needs the resubmit wait),
# then repetition. The kind attribute is what lets downstream consumers separate a
# fail-repetition (which must never enter a first-attempt failure rate) from a Spark task
# resubmit (infra artifact) and a plain repetition.

@pytest.mark.parametrize('rerun,attempt,reps,attempt_index,expected', [
    # expected = (retry, retry_kind, wait)
    (False, 0, '1', 1, (False, '', False)),                # plain first attempt
    (True, 0, '1', 1, (True, 'fail_repetition', False)),   # --fail_repetitions re-run
    (True, 0, '1', 3, (True, 'fail_repetition', False)),
    (False, 1, '1', 1, (True, 'task_resubmit', True)),     # Spark re-ran a dead task
    (False, 2, '1', 1, (True, 'task_resubmit', True)),
    (False, 0, '10', 1, (True, 'repetition', False)),      # first repetition may skip the wait
    (False, 0, '10', 3, (True, 'repetition', True)),
    # Overlaps, resolved by precedence:
    (True, 1, '1', 2, (True, 'fail_repetition', False)),   # resubmit inside the rerun job
    (False, 1, '10', 3, (True, 'task_resubmit', True)),    # resubmit inside a repetitions job
])
def test_classify_execution(rerun: bool, attempt: int, reps: str, attempt_index: int,
                            expected: Any) -> None:
    assert csi_report.classify_execution(rerun, attempt, reps, attempt_index) == expected
