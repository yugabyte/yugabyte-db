/**
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 */
package org.yb.util;

import org.yb.YBParameterizedTestRunner;

public class YBParameterizedTestRunnerNonTsanAsan extends YBParameterizedTestRunner {

  public YBParameterizedTestRunnerNonTsanAsan(Class<?> klass) throws Throwable {
    super(klass);
  }

  @Override
  public boolean shouldRunTests() {
    return !BuildTypeUtil.isTSAN() && !BuildTypeUtil.isASAN();
  }

}
