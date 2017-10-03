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

import org.junit.Assert;
import org.junit.Test;

public class TestErrorCollector {

  @Test
  public void testErrorCollector() {
    int maxErrors = 10;
    ErrorCollector collector = new ErrorCollector(maxErrors);

    // Test with no errors.
    int countToTest = 0;
    Assert.assertEquals(countToTest, collector.countErrors());
    RowErrorsAndOverflowStatus reos = collector.getErrors();
    Assert.assertEquals(0, collector.countErrors());
    Assert.assertFalse(reos.isOverflowed());
    Assert.assertEquals(countToTest, reos.getRowErrors().length);

    // Test a single row error.
    countToTest = 1;
    collector.addError(createRowError(countToTest));
    Assert.assertEquals(countToTest, collector.countErrors());
    reos = collector.getErrors();
    Assert.assertEquals(0, collector.countErrors());
    Assert.assertFalse(reos.isOverflowed());
    Assert.assertEquals(countToTest, reos.getRowErrors().length);
    Assert.assertEquals(countToTest + "", reos.getRowErrors()[0].getStatus());

    // Test filling the collector to the max.
    countToTest = maxErrors;
    fillCollectorWith(collector, countToTest);
    Assert.assertEquals(countToTest, collector.countErrors());
    reos = collector.getErrors();
    Assert.assertEquals(0, collector.countErrors());
    Assert.assertFalse(reos.isOverflowed());
    Assert.assertEquals(countToTest, reos.getRowErrors().length);
    Assert.assertEquals((countToTest - 1) + "", reos.getRowErrors()[9].getStatus());

    // Test overflowing.
    countToTest = 95;
    fillCollectorWith(collector, countToTest);
    Assert.assertEquals(maxErrors, collector.countErrors());
    reos = collector.getErrors();
    Assert.assertEquals(0, collector.countErrors());
    Assert.assertTrue(reos.isOverflowed());
    Assert.assertEquals(maxErrors, reos.getRowErrors().length);
    Assert.assertEquals((countToTest - 1) + "", reos.getRowErrors()[9].getStatus());

    // Test overflowing on a newly created collector.
    countToTest = 95;
    collector = new ErrorCollector(maxErrors);
    fillCollectorWith(collector, countToTest);
    Assert.assertEquals(maxErrors, collector.countErrors());
    reos = collector.getErrors();
    Assert.assertEquals(0, collector.countErrors());
    Assert.assertTrue(reos.isOverflowed());
    Assert.assertEquals(maxErrors, reos.getRowErrors().length);
    Assert.assertEquals((countToTest - 1) + "", reos.getRowErrors()[9].getStatus());
  }

  private void fillCollectorWith(ErrorCollector collector, int errorsToAdd) {
    for (int i = 0; i < errorsToAdd; i++) {
      collector.addError(createRowError(i));
    }
  }

  private RowError createRowError(int id) {
    // Use the error status as a way to message pass and so that we can test we're getting the right
    // messages on the other end.
    return new RowError(id + "", "test", null, "test");
  }
}
