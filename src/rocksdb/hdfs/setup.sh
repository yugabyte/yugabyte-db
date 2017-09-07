#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Copyright (c) YugaByte, Inc.
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
#
export USE_HDFS=1
export LD_LIBRARY_PATH=$JAVA_HOME/jre/lib/amd64/server:$JAVA_HOME/jre/lib/amd64:/usr/lib/hadoop/lib/native

export CLASSPATH=
for f in `find /usr/lib/hadoop-hdfs | grep jar`; do export CLASSPATH=$CLASSPATH:$f; done
for f in `find /usr/lib/hadoop | grep jar`; do export CLASSPATH=$CLASSPATH:$f; done
for f in `find /usr/lib/hadoop/client | grep jar`; do export CLASSPATH=$CLASSPATH:$f; done
