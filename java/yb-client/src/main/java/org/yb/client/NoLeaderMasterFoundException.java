// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.client;

import com.google.common.base.Functions;
import com.google.common.base.Joiner;
import com.google.common.collect.Lists;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import java.util.List;

/**
 * Indicates that the request failed because we couldn't find a leader master server.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public final class NoLeaderMasterFoundException extends RecoverableException {

  NoLeaderMasterFoundException(final String msg) {
    super(msg);
  }
  NoLeaderMasterFoundException(final String msg, final Exception cause) {
    super(msg, cause);
  }

  /**
   * Factory method that creates a NoLeaderException given a message and a list
   * (which may be empty, but must be initialized) of exceptions encountered: they indicate
   * why {@link GetMasterRegistrationRequest} calls to the masters in the config
   * have failed, to aid in debugging the issue. If the list is non-empty, each exception's
   * 'toString()' message is appended to 'msg' and the last exception is used as the
   * cause for the exception.
   * @param msg A message detailing why this exception occurred.
   * @param causes List of exceptions encountered when retrieving registration from individual
   *               masters.
   * @return An instantiated NoLeaderMasterFoundException which can be thrown.
   */
  static NoLeaderMasterFoundException create(String msg, List<Exception> causes) {
    if (causes.isEmpty()) {
      return new NoLeaderMasterFoundException(msg);
    }
    String joinedMsg = msg + ". Exceptions received: " +
        Joiner.on(",").join(Lists.transform(causes, Functions.toStringFunction()));
    return new NoLeaderMasterFoundException(joinedMsg, causes.get(causes.size() - 1));
  }
}
