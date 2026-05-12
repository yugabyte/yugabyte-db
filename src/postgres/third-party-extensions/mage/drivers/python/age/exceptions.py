# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

from psycopg.errors import *

class AgeNotSet(Exception):
    def __init__(self, name):
        self.name = name

    def __repr__(self) :
        return 'AGE extension is not set.'

class GraphNotFound(Exception):
    def __init__(self, name):
        self.name = name

    def __repr__(self) :
        return 'Graph[' + self.name + '] does not exist.'


class GraphAlreadyExists(Exception):
    def __init__(self, name):
        self.name = name

    def __repr__(self) :
        return 'Graph[' + self.name + '] already exists.'


class GraphNotSet(Exception):
    def __repr__(self) :
        return 'Graph name is not set.'


class NoConnection(Exception):
    def __repr__(self) :
        return 'No Connection'

class NoCursor(Exception):
    def __repr__(self) :
        return 'No Cursor'

class SqlExecutionError(Exception):
    def __init__(self, msg, cause):
        self.msg = msg
        self.cause = cause
        super().__init__(msg, cause)

    def __repr__(self) :
        return 'SqlExecution [' + self.msg + ']'

class AGTypeError(Exception):
    def __init__(self, msg, cause):
        self.msg = msg
        self.cause = cause
        super().__init__(msg, cause)
