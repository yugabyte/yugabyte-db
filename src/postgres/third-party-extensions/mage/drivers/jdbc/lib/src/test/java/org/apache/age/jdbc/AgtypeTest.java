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

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import org.apache.age.jdbc.base.Agtype;
import org.apache.age.jdbc.base.AgtypeFactory;
import org.apache.age.jdbc.base.AgtypeUtil;
import org.apache.age.jdbc.base.InvalidAgtypeException;
import org.apache.age.jdbc.base.type.AgtypeList;
import org.apache.age.jdbc.base.type.AgtypeMap;
import org.junit.jupiter.api.Test;

class AgtypeTest extends BaseDockerizedTest {

    private Agtype getAgType(Object fieldValue) throws SQLException {
        Statement stmt = getConnection().createStatement();
        String str = "SELECT '" + AgtypeFactory.create(fieldValue).getValue() + "'::agtype;";
        ResultSet rs = stmt.executeQuery(str);
        assertTrue(rs.next());
        return (Agtype) rs.getObject(1);
    }

    /*
      Cypher Return Statement should be SQL Null, not Agtype Null
    */
    @Test
    void agTypeCypherReturnNull() throws SQLException {
        Statement stmt = getConnection().createStatement();
        String str = "SELECT i from cypher('cypher', $$RETURN null$$) as t(i agtype);";
        ResultSet rs = stmt.executeQuery(str);
        assertTrue(rs.next());
        assertNull(rs.getObject(1));
    }

    /*
      Get String Unit Tests
    */
    @Test
    void agTypeGetString() throws SQLException, InvalidAgtypeException {
        assertEquals("Hello World", getAgType("Hello World").getString());
        assertEquals("\n", getAgType("\n").getString());
        assertEquals("\b", getAgType("\b").getString());
        assertEquals("\f", getAgType("\f").getString());
        assertEquals("\r", getAgType("\r").getString());
        assertEquals("\\", getAgType("\\").getString());
        assertEquals("/", getAgType("/").getString());
        assertEquals("\t", getAgType("\t").getString());
        //GREEK CAPITAL LETTER OMEGA, U+03A9
        assertEquals("Ω", getAgType("\u03A9").getString());
        //MATHEMATICAL ITALIC CAPITAL OMICRON, U+1D6F0
        assertEquals("\uD835\uDEF0", getAgType("\ud835\uDEF0").getString());
    }

    @Test
    void agTypeGetStringConvertAgtypeNull() throws SQLException, InvalidAgtypeException {
        assertNull(getAgType(null).getString());
    }

    @Test
    void agTypeGetStringConvertInt() throws SQLException, InvalidAgtypeException {
        assertEquals("1", getAgType(1).getString());
    }

    @Test
    void agTypeGetStringConvertBoolean() throws SQLException, InvalidAgtypeException {
        assertEquals("true", getAgType(true).getString());
        assertEquals("false", getAgType(false).getString());
    }

    @Test
    void agTypeGetStringConvertDouble() throws SQLException, InvalidAgtypeException {
        assertEquals("3.141592653589793", getAgType(3.141592653589793).getString());
        assertEquals("Infinity", getAgType(Double.POSITIVE_INFINITY).getString());
        assertEquals("-Infinity", getAgType(Double.NEGATIVE_INFINITY).getString());
    }

    @Test
    void agTypeGetStringConvertMap() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createMapBuilder().build()).getString());
    }

    @Test
    void agTypeGetStringConvertList() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createListBuilder().build()).getString());
    }

    /*
      Get Integer Unit Tests
     */
    @Test
    void agTypeGetInteger() throws SQLException, InvalidAgtypeException {
        //Agtype is made in SELECT clause
        assertEquals(Integer.MAX_VALUE, getAgType(2147483647).getInt());
        assertEquals(Integer.MIN_VALUE, getAgType(-2147483648).getInt());
        assertThrows(NumberFormatException.class, () -> getAgType(2147483648L).getInt());
        assertThrows(NumberFormatException.class, () -> getAgType(-2147483649L).getInt());
    }

    @Test
    void agTypeGetIntConvertAgtypeNull() throws SQLException, InvalidAgtypeException {
        assertEquals(0, getAgType(null).getInt());
    }

    @Test
    void agTypeGetIntConvertString() throws SQLException, InvalidAgtypeException {
        assertThrows(NumberFormatException.class, () -> getAgType("Not A Number").getInt());
        assertEquals(1, getAgType("1").getInt());
        assertEquals(1, getAgType("1.1").getInt());
    }

    @Test
    void agTypeGetIntConvertDouble() throws SQLException, InvalidAgtypeException {
        assertEquals(1, getAgType(1.1).getInt());
    }

    @Test
    void agTypeGetIntConvertBoolean() throws SQLException, InvalidAgtypeException {
        assertEquals(1, getAgType(true).getInt());
        assertEquals(0, getAgType(false).getInt());
    }

    @Test
    void agTypeGetIntConvertMap() {
        assertThrows(InvalidAgtypeException.class, () ->
            getAgType(AgtypeUtil.createMapBuilder().build()).getInt());
    }

    @Test
    void agTypeGetIntConvertList() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createListBuilder().build()).getInt());
    }

    /*
      Get Long Unit Tests
     */
    @Test
    void agTypeGetLong() throws SQLException, InvalidAgtypeException {
        assertEquals(Long.MAX_VALUE, getAgType(Long.MAX_VALUE).getLong());
        assertEquals(Long.MIN_VALUE, getAgType(Long.MIN_VALUE).getLong());
        assertEquals(-0L, getAgType(-0).getLong());
    }

    @Test
    void agTypeGetLongConvertAgtypeNull() throws SQLException, InvalidAgtypeException {
        assertEquals(0L, getAgType(null).getLong());
    }

    @Test
    void agTypeGetLongConvertString() throws SQLException, InvalidAgtypeException {
        assertThrows(NumberFormatException.class, () -> getAgType("Not a Number").getLong());
        assertEquals(1L, getAgType("1").getLong());
    }

    @Test
    void agTypeGetLongConvertDouble() throws SQLException, InvalidAgtypeException {
        assertEquals(3L, getAgType(Math.PI).getLong());
        assertEquals(1L, getAgType(1.6).getLong());
    }

    @Test
    void agTypeGetLongConvertMap() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createMapBuilder().build()).getLong());
    }

    @Test
    void agTypeGetLongConvertList() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createListBuilder().build()).getLong());
    }

    /*
      Get Double Unit Tests
     */
    @Test
    void agTypeGetDouble() throws SQLException, InvalidAgtypeException {
        assertEquals(Math.PI, getAgType(Math.PI).getDouble());
        assertEquals(-Math.PI, getAgType(-Math.PI).getDouble());
        assertEquals(Double.POSITIVE_INFINITY, getAgType(Double.POSITIVE_INFINITY).getDouble());
        assertEquals(Double.NEGATIVE_INFINITY, getAgType(Double.NEGATIVE_INFINITY).getDouble());
        assertEquals(Double.NaN, getAgType(Double.NaN).getDouble());
        assertEquals(Double.MIN_NORMAL, getAgType(Double.MIN_NORMAL).getDouble());
        assertEquals(Double.MIN_VALUE, getAgType(Double.MIN_VALUE).getDouble());
        assertEquals(Double.MAX_VALUE, getAgType(Double.MAX_VALUE).getDouble());
    }

    @Test
    void agTypeGetDoubleConvertAgtypeNull() throws SQLException, InvalidAgtypeException {
        assertEquals(0L, getAgType(null).getDouble());
    }

    @Test
    void agTypeGetDoubleConvertString() throws SQLException, InvalidAgtypeException {
        assertThrows(NumberFormatException.class, () -> getAgType("Not a Number").getDouble());
        assertEquals(1.0, getAgType("1").getDouble());
        assertEquals(1.1, getAgType("1.1").getDouble());
        assertEquals(1e9, getAgType("1e9").getDouble());
    }

    @Test
    void agTypeGetDoubleConvertLong() throws SQLException, InvalidAgtypeException {
        assertEquals(1.0, getAgType(1).getDouble());
    }

    @Test
    void agTypeGetDoubleConvertBoolean() throws SQLException, InvalidAgtypeException {
        assertEquals(1.0, getAgType(true).getDouble());
        assertEquals(0.0, getAgType(false).getDouble());
    }

    @Test
    void agTypeGetDoubleConvertMap() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createMapBuilder().build()).getDouble());
    }

    @Test
    void agTypeGetDoubleConvertList() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createListBuilder().build()).getDouble());
    }

    /*
      Get Boolean Unit Tests
     */
    @Test
    void agTypeGetBoolean() throws SQLException, InvalidAgtypeException {
        assertTrue(getAgType(true).getBoolean());
        assertFalse(getAgType(false).getBoolean());
    }

    @Test
    void agTypeGetBooleanConvertAgtypeNull() throws SQLException, InvalidAgtypeException {
        assertFalse(getAgType(null).getBoolean());
    }

    @Test
    void agTypeGetBooleanConvertString() throws SQLException, InvalidAgtypeException {
        assertTrue(getAgType("Non-Empty String").getBoolean());
        assertFalse(getAgType("").getBoolean());
    }

    @Test
    void agTypeGetBooleanConvertLong() throws SQLException, InvalidAgtypeException {
        assertFalse(getAgType(0).getBoolean());
        assertTrue(getAgType(1).getBoolean());
    }

    @Test
    void agTypeGetBooleanConvertDouble() throws SQLException, InvalidAgtypeException {
        assertTrue(getAgType(Math.PI).getBoolean());
        assertFalse(getAgType(0.0).getBoolean());
    }

    @Test
    void agTypeGetBooleanConvertMap() throws SQLException, InvalidAgtypeException {
        assertFalse(getAgType(AgtypeUtil.createMapBuilder().build()).getBoolean());
        assertTrue(
            getAgType(AgtypeUtil.createMapBuilder().add("key", "hello").build()).getBoolean());
    }

    @Test
    void agTypeGetBooleanConvertList() throws SQLException, InvalidAgtypeException {
        assertFalse(getAgType(AgtypeUtil.createListBuilder().build()).getBoolean());
        assertTrue(getAgType(AgtypeUtil.createListBuilder().add("Hello").build()).getBoolean());
    }

    /*
      Get Map Unit Tests
     */
    @Test
    void agTypeGetMap() throws SQLException, InvalidAgtypeException {
        AgtypeMap agtypeMap = AgtypeUtil.createMapBuilder()
            .add("i", 1)
            .add("f", 3.14)
            .add("s", "Hello World")
            .add("m", AgtypeUtil.createMapBuilder().add("i", 1))
            .add("l", AgtypeUtil.createListBuilder().add(1).add(2).add(3))
            .add("bt", true)
            .add("bf", false)
            .addNull("z")
            .add("pinf", Double.POSITIVE_INFINITY)
            .add("ninf", Double.NEGATIVE_INFINITY)
            .add("n", Double.NaN)
            .build();

        AgtypeMap m = getAgType(agtypeMap).getMap();

        assertEquals(1L, m.getLong("i"));
        assertEquals(3.14, m.getDouble("f"));
        assertEquals("Hello World", m.getString("s"));
        assertTrue(m.getBoolean("bt"));
        assertFalse(m.getBoolean("bf"));
        assertTrue(m.isNull("z"));
        assertEquals(Double.POSITIVE_INFINITY, m.getDouble("pinf"));
        assertTrue(Double.isNaN(m.getDouble("n")));
        assertEquals(Double.NEGATIVE_INFINITY, m.getDouble("ninf"));

        AgtypeMap subMap = m.getMap("m");
        assertEquals(1, subMap.getInt("i"));

        AgtypeList list = m.getList("l");
        for (int i = 0; i < list.size(); i++) {
            assertEquals(i + 1, list.getLong(i));
        }
    }

    @Test
    void agTypeGetMapConvertAgtypeNull() {
        assertDoesNotThrow(() -> getAgType(null).getMap());
    }

    @Test
    void agTypeGetMapConvertString() {
        assertThrows(InvalidAgtypeException.class, () -> getAgType("Non-Empty String").getMap());
        assertThrows(InvalidAgtypeException.class, () -> getAgType("").getMap());
    }

    @Test
    void agTypeGetMapConvertLong() {
        assertThrows(InvalidAgtypeException.class, () -> getAgType(0L).getMap());
        assertThrows(InvalidAgtypeException.class, () -> getAgType(1L).getMap());
    }

    @Test
    void agTypeGetMapConvertDouble() {
        assertThrows(InvalidAgtypeException.class, () -> getAgType(Math.PI).getMap());
        assertThrows(InvalidAgtypeException.class, () -> getAgType(0.0).getMap());
    }

    @Test
    void agTypeGetMapConvertList() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createListBuilder().build()).getMap());
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createListBuilder().add("Hello").build()).getMap());
    }

    /*
      Get List Unit Tests
     */
    @Test
    void agTypeGetList() throws SQLException, InvalidAgtypeException {
        AgtypeList agArray = AgtypeUtil.createListBuilder()
            .add(1)
            .add("Hello World")
            .add(3.14)
            .add(AgtypeUtil.createMapBuilder().add("key0", 1))
            .add(AgtypeUtil.createListBuilder().add(1).add(2))
            .add(true)
            .add(false)
            .addNull()
            .add(Double.NaN)
            .add(Double.POSITIVE_INFINITY)
            .add(Double.NEGATIVE_INFINITY)
            .build();

        AgtypeList l = getAgType(agArray).getList();

        assertEquals("Hello World", l.getString(1));
        assertEquals(1, l.getInt(0));
        assertEquals(1L, l.getLong(0));
        assertEquals(3.14, l.getDouble(2));
        assertEquals(Double.NaN, l.getDouble(8));
        assertEquals(Double.POSITIVE_INFINITY, l.getDouble(9));
        assertEquals(Double.NEGATIVE_INFINITY, l.getDouble(10));
        assertTrue(l.getBoolean(5));
        assertFalse(l.getBoolean(6));
        assertNull(l.getObject(7));
        assertEquals(1L, l.getList(4).getLong(0));
        assertEquals(2L, l.getList(4).getLong(1));
        assertEquals(1L, l.getMap(3).getLong("key0"));
    }

    @Test
    void agTypeGetListConvertAgtypeNull() throws SQLException {
        assertNull(getAgType(null).getList());
    }

    @Test
    void agTypeGetListConvertString() {
        assertThrows(InvalidAgtypeException.class, () -> getAgType("Non-Empty String").getList());
        assertThrows(InvalidAgtypeException.class, () -> getAgType("").getList());
    }

    @Test
    void agTypeGetListConvertLong() {
        assertThrows(InvalidAgtypeException.class, () -> getAgType(0).getList());
        assertThrows(InvalidAgtypeException.class, () -> getAgType(1).getList());
    }

    @Test
    void agTypeGetListConvertDouble() {
        assertThrows(InvalidAgtypeException.class, () -> getAgType(Math.PI).getList());
        assertThrows(InvalidAgtypeException.class, () -> getAgType(0.0).getList());
    }

    @Test
    void agTypeGetListConvertMap() {
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createMapBuilder().build()).getList());
        assertThrows(InvalidAgtypeException.class,
            () -> getAgType(AgtypeUtil.createMapBuilder().add("key", "hello").build()).getList());
    }
}
