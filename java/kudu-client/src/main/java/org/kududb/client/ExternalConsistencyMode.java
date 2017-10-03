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
package org.kududb.client;

import org.kududb.Common;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;

/**
 * The possible external consistency modes on which Kudu operates.
 * See {@code src/kudu/common/common.proto} for a detailed explanations on the
 *      meaning and implications of each mode.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public enum ExternalConsistencyMode {
  CLIENT_PROPAGATED(Common.ExternalConsistencyMode.CLIENT_PROPAGATED),
  COMMIT_WAIT(Common.ExternalConsistencyMode.COMMIT_WAIT);

  private Common.ExternalConsistencyMode pbVersion;
  private ExternalConsistencyMode(Common.ExternalConsistencyMode pbVersion) {
    this.pbVersion = pbVersion;
  }
  @InterfaceAudience.Private
  public Common.ExternalConsistencyMode pbVersion() {
    return pbVersion;
  }
}
