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
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.apache.age.jdbc.base.AgtypeUtil;
import org.apache.age.jdbc.base.type.AgtypeList;
import org.apache.age.jdbc.base.type.AgtypeMap;
import org.apache.commons.text.StringEscapeUtils;
import org.junit.jupiter.api.Test;

class AgtypeUtilTest {

  @Test
  void parseEmptyString() {
    assertEquals("", AgtypeUtil.parse("\"\""));
  }

  @Test
  void parseString() {
    assertEquals("Hello World", AgtypeUtil.parse("\"Hello World\""));
  }

  @Test
  void parseEscapedSequences() {
    assertEquals("\"", AgtypeUtil.parse("\"\\\"\""));
    assertEquals("\\", AgtypeUtil.parse("\"\\\\\""));
    assertEquals("/", AgtypeUtil.parse("\"\\/\""));
    assertEquals("\b", AgtypeUtil.parse("\"\\b\""));
    assertEquals("\f", AgtypeUtil.parse("\"\\f\""));
    assertEquals("\n", AgtypeUtil.parse("\"\\n\""));
    assertEquals("\r", AgtypeUtil.parse("\"\\r\""));
    assertEquals("\t", AgtypeUtil.parse("\"\\t\""));
  }

  @Test
  void parseEscapedUnicodeSequences() {
    //GREEK CAPITAL LETTER OMEGA, U+03A9
    assertEquals("Ω",
        StringEscapeUtils.unescapeJson((String)AgtypeUtil.parse("\"\\u03A9\"")));
    //MATHEMATICAL ITALIC CAPITAL OMICRON, U+1D6F0
    assertEquals("\uD835\uDEF0",
        StringEscapeUtils.unescapeJson((String)AgtypeUtil.parse("\"\\ud835\\uDEF0\"")));
  }

  @Test
  void parseInvalidStrings() {
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse("\"Hello World"));
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse("Hello World\""));
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse("\\a"));
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse("\\u03A"));
  }

  @Test
  void parseInteger() {
    assertEquals(0x7FFFFFFFFFFFFFFFL, AgtypeUtil.parse("9223372036854775807"));
    assertEquals(0x8000000000000000L, AgtypeUtil.parse("-9223372036854775808"));
    assertEquals(-0L, AgtypeUtil.parse("-0"));
  }

  @Test
  void parseInvalidIntegerValues() {
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse("01"));
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse("00"));
    assertThrows(NumberFormatException.class, () -> AgtypeUtil.parse("9223372036854775808"));
    assertThrows(NumberFormatException.class, () -> AgtypeUtil.parse("-9223372036854775809"));
  }

  @Test
  void parseDouble() {
    assertEquals(Math.PI, AgtypeUtil.parse(Double.toString(Math.PI)));
    assertEquals(-Math.PI, AgtypeUtil.parse(Double.toString(-Math.PI)));
    assertEquals(1e09, AgtypeUtil.parse("1e09"));
    assertEquals(3.14e-1, AgtypeUtil.parse("3.14e-1"));
    assertEquals(Double.POSITIVE_INFINITY, AgtypeUtil.parse("Infinity"));
    assertEquals(Double.NEGATIVE_INFINITY, AgtypeUtil.parse("-Infinity"));
    assertEquals(Double.NaN, AgtypeUtil.parse("NaN"));
  }

  @Test
  void parseInvalidFloatValues() {
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse("1."));
    assertThrows(IllegalStateException.class, () -> AgtypeUtil.parse(".1"));
  }

  @Test
  void parseFalseBoolean() {
    assertFalse((Boolean) AgtypeUtil.parse("false"));
  }

  @Test
  void parseTrueBoolean() {
    assertTrue((Boolean) AgtypeUtil.parse("true"));
  }

  @Test
  void parseNull() {
    assertNull(AgtypeUtil.parse("null"));
  }

  @Test
  void parseEmptyArray() {
    AgtypeList agArray = (AgtypeList) AgtypeUtil.parse("[]");
    assertEquals(0, agArray.size());
  }

  @Test
  void parseArray() {
    AgtypeList agArray = (AgtypeList) AgtypeUtil.parse("[1]");
    assertEquals(1, agArray.size());
  }

  @Test
  void parseObject() {
    AgtypeMap agObject = (AgtypeMap) AgtypeUtil.parse("{\"i\":1}");
    assertEquals(1, agObject.size());
  }

  @Test
  void parseEmptyObject() {
    AgtypeMap agObject = (AgtypeMap) AgtypeUtil.parse("{}");
    assertEquals(0, agObject.size());
  }
}
