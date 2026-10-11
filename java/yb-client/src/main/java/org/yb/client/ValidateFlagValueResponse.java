// Copyright (c) YugabyteDB, Inc.
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

import java.util.Collections;
import java.util.Map;
import org.yb.annotations.InterfaceAudience;

@InterfaceAudience.Public
public class ValidateFlagValueResponse extends YRpcResponse {

  private final Map<String, String> errors;

  public ValidateFlagValueResponse(long elapsedMillis, String uuid) {
    super(elapsedMillis, uuid);
    this.errors = Collections.emptyMap();
  }

  public ValidateFlagValueResponse(long elapsedMillis, String uuid, Map<String, String> errors) {
    super(elapsedMillis, uuid);
    this.errors = errors != null ? errors : Collections.emptyMap();
  }

  /** Returns per-flag validation errors. Empty map means all flags passed validation. */
  public Map<String, String> getErrors() {
    return errors;
  }

  public boolean hasErrors() {
    return !errors.isEmpty();
  }
}
