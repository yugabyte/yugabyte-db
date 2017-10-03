# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

class KuduException(Exception):
    pass


class KuduBadStatus(KuduException):
    """
    A Kudu C++ client operation returned an error Status
    """
    pass


class KuduNotFound(KuduBadStatus):
    pass


class KuduNotSupported(KuduBadStatus):
    pass


class KuduInvalidArgument(KuduBadStatus):
    pass


class KuduNotAuthorized(KuduBadStatus):
    pass


class KuduAborted(KuduBadStatus):
    pass


cdef check_status(const Status& status):
    if status.ok():
        return

    cdef string c_message = status.message().ToString()

    if status.IsNotFound():
        raise KuduNotFound(c_message)
    elif status.IsNotSupported():
        raise KuduNotSupported(c_message)
    elif status.IsInvalidArgument():
        raise KuduInvalidArgument(c_message)
    else:
        raise KuduBadStatus(status.ToString())
