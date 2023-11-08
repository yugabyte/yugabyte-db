// Copyright (c) YugaByteDB, Inc.
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

package org.yb.util;

import static org.yb.AssertionWrappers.assertEquals;

import java.util.Arrays;
import java.util.List;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.BaseYBTest;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestStringUtil extends BaseYBTest {
  @Test
  public void testJoinLines() throws Exception {
    List<String> lines = Arrays.asList("a", "b");
    assertEquals("    a\n    b", StringUtil.joinLinesForLogging(lines));
  }

  @Test
  public void testJoinLinesNoPrefix() throws Exception {
    List<String> lines = Arrays.asList("a", "b");
    assertEquals("a\nb", StringUtil.joinLinesForLoggingNoPrefix(lines));
  }
}
