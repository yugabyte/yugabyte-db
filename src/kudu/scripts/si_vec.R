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

si_num <- function (x) {

  if (!is.na(x)) {
    if (x >= 1e9) { 
      rem <- format(x/1e9, digits=3)
      rem <- append(rem, "B");
    } else if (x >= 1e6) { 
      rem <- format(x/1e6, digits=3)
      rem <- append(rem, "M");
    } else if (x > 1e3) { 
      rem <- format(x/1e3, digits=3)
      rem <- append(rem, "K");
    }
    else {
      return(x);
    }

    return(paste(rem, sep="", collapse=""));
  }
  else return(NA);
}

si_vec <- function(x) {
  sapply(x, FUN=si_num);
}

