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

from typing import TypeVar

import dataclasses


DataClassType = TypeVar('DataClassType')


def assert_data_classes_equal(inst1: DataClassType, inst2: DataClassType) -> None:
    if inst1 == inst2:
        return
    assert type(inst1) is type(inst2), (
        "Instances being compared are of different types: "
        "%s (%s) and %s (%s)" % (inst1, type(inst1), inst2, type(inst2)))
    fields1 = dataclasses.fields(inst1)  # type: ignore
    fields2 = dataclasses.fields(inst2)  # type: ignore
    for field_name in sorted(set(field.name for field in list(fields1) + list(fields2))):
        value1 = getattr(inst1, field_name)
        value2 = getattr(inst2, field_name)
        assert value1 == value2, \
               "Mismatch of values of field %s: %s vs. %s. First object: %s, second object: %s" % (
                   field_name, value1, value2, inst1, inst2
               )
