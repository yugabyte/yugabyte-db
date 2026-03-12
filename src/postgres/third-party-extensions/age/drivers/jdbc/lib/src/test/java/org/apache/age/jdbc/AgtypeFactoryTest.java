/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.apache.age.jdbc;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.apache.age.jdbc.base.AgtypeFactory;
import org.apache.age.jdbc.base.AgtypeUtil;
import org.apache.age.jdbc.base.InvalidAgtypeException;
import org.apache.age.jdbc.base.type.AgtypeList;
import org.apache.age.jdbc.base.type.AgtypeMap;
import org.junit.jupiter.api.Test;

class AgtypeFactoryTest {

  @Test
  void agTypeInvalidTypes() {
    //float
    assertThrows(InvalidAgtypeException.class,
        () -> AgtypeFactory.create(Float.parseFloat("3.14")));
    //char
    assertThrows(InvalidAgtypeException.class, () -> AgtypeFactory.create('c'));
    //Object
    assertThrows(InvalidAgtypeException.class, () -> AgtypeFactory.create(new Object()));
    //StringBuilder
    assertThrows(InvalidAgtypeException.class,
        () -> AgtypeFactory.create(new StringBuilder().append("Hello")));
  }

  @Test
  void agTypeFactoryGetInteger() throws InvalidAgtypeException {
    assertTrue(AgtypeFactory.create(1).getObject() instanceof Long);

    assertEquals(Integer.MAX_VALUE, AgtypeFactory.create(2147483647).getInt());
    assertEquals(Integer.MIN_VALUE, AgtypeFactory.create(-2147483648).getInt());
    assertThrows(NumberFormatException.class, () -> AgtypeFactory.create(Long.MAX_VALUE).getInt());
  }

  @Test
  void agTypeFactoryGetLong() throws InvalidAgtypeException {
    assertTrue(AgtypeFactory.create(1L).getObject() instanceof Long);

    assertEquals(Long.MAX_VALUE, AgtypeFactory.create(9223372036854775807L).getLong());
    assertEquals(Long.MIN_VALUE, AgtypeFactory.create(-9223372036854775808L).getLong());
    assertEquals(-0L, AgtypeFactory.create(-0).getLong());
  }

  @Test
  void agTypeFactoryDouble() throws InvalidAgtypeException {
    assertTrue(AgtypeFactory.create(1.0).getObject() instanceof Double);

    assertEquals(Math.PI, AgtypeFactory.create(Math.PI).getDouble());
    assertEquals(Double.POSITIVE_INFINITY,
            AgtypeFactory.create(Double.POSITIVE_INFINITY).getDouble());
    assertEquals(Double.NEGATIVE_INFINITY,
            AgtypeFactory.create(Double.NEGATIVE_INFINITY).getDouble());
    assertEquals(Double.NaN, AgtypeFactory.create(Double.NaN).getDouble());
  }

  @Test
  void agTypeFactoryString() throws InvalidAgtypeException {
    assertTrue(AgtypeFactory.create("Hello World").getObject() instanceof String);

    assertEquals("Hello World", AgtypeFactory.create("Hello World").getString());
    assertEquals("\n", AgtypeFactory.create("\n").getString());
    assertEquals("\t", AgtypeFactory.create("\t").getString());
    assertEquals("\b", AgtypeFactory.create("\b").getString());
    assertEquals("\f", AgtypeFactory.create("\f").getString());
    assertEquals("\r", AgtypeFactory.create("\r").getString());
    assertEquals("\\", AgtypeFactory.create("\\").getString());
    assertEquals("/", AgtypeFactory.create("/").getString());
    assertEquals("\t", AgtypeFactory.create("\t").getString());
    //GREEK CAPITAL LETTER OMEGA, U+03A9
    assertEquals("Ω", AgtypeFactory.create("\u03A9").getString());
    //MATHEMATICAL ITALIC CAPITAL OMICRON, U+1D6F0
    assertEquals("\uD835\uDEF0", AgtypeFactory.create("\ud835\uDEF0").getString());

  }

  @Test
  void agTypeFactoryBoolean() throws InvalidAgtypeException {
    assertTrue(AgtypeFactory.create(true).getObject() instanceof Boolean);

    assertTrue(AgtypeFactory.create(true).getBoolean());
    assertFalse(AgtypeFactory.create(false).getBoolean());
  }

  @Test
  void agTypeFactoryMap() throws InvalidAgtypeException {
    AgtypeMap map = AgtypeUtil.createMapBuilder().add("key","value").build();

    assertTrue(AgtypeFactory.create(map).getObject() instanceof AgtypeMap);

    assertEquals("value", AgtypeFactory.create(map).getMap().getString("key"));
  }

  @Test
  void agTypeFactoryList() throws InvalidAgtypeException {
    AgtypeList list = AgtypeUtil.createListBuilder().add("value").build();

    assertTrue(AgtypeFactory.create(list).getObject() instanceof AgtypeList);

    assertEquals("value", AgtypeFactory.create(list).getList().getString(0));
  }
}
