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

import com.google.common.collect.Lists;
import org.junit.Test;
import org.kududb.ColumnSchema;
import org.kududb.Type;
import org.kududb.tserver.Tserver;

import java.io.IOException;
import java.util.List;

import static org.junit.Assert.*;

public class TestColumnRangePredicate {

  @Test
  public void testRawLists() {
    ColumnSchema col1 = new ColumnSchema.ColumnSchemaBuilder("col1", Type.INT32).build();
    ColumnSchema col2 = new ColumnSchema.ColumnSchemaBuilder("col2", Type.STRING).build();

    ColumnRangePredicate pred1 = new ColumnRangePredicate(col1);
    pred1.setLowerBound(1);

    ColumnRangePredicate pred2 = new ColumnRangePredicate(col1);
    pred2.setUpperBound(2);

    ColumnRangePredicate pred3 = new ColumnRangePredicate(col2);
    pred3.setLowerBound("aaa");
    pred3.setUpperBound("bbb");

    List<ColumnRangePredicate> preds = Lists.newArrayList(pred1, pred2, pred3);

    byte[] rawPreds = ColumnRangePredicate.toByteArray(preds);

    List<Tserver.ColumnRangePredicatePB> decodedPreds = null;
    try {
      decodedPreds = ColumnRangePredicate.fromByteArray(rawPreds);
    } catch (IllegalArgumentException e) {
      fail("Couldn't decode: " + e.getMessage());
    }

    assertEquals(3, decodedPreds.size());

    assertEquals(col1.getName(), decodedPreds.get(0).getColumn().getName());
    assertEquals(1, Bytes.getInt(Bytes.get(decodedPreds.get(0).getLowerBound())));
    assertFalse(decodedPreds.get(0).hasUpperBound());

    assertEquals(col1.getName(), decodedPreds.get(1).getColumn().getName());
    assertEquals(2, Bytes.getInt(Bytes.get(decodedPreds.get(1).getUpperBound())));
    assertFalse(decodedPreds.get(1).hasLowerBound());

    assertEquals(col2.getName(), decodedPreds.get(2).getColumn().getName());
    assertEquals("aaa", Bytes.getString(Bytes.get(decodedPreds.get(2).getLowerBound())));
    assertEquals("bbb", Bytes.getString(Bytes.get(decodedPreds.get(2).getUpperBound())));
  }
}
