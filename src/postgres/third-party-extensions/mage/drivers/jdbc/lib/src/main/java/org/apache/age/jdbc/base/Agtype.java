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

package org.apache.age.jdbc.base;

import java.sql.SQLException;
import org.apache.age.jdbc.base.type.AgtypeAnnotation;
import org.apache.age.jdbc.base.type.AgtypeList;
import org.apache.age.jdbc.base.type.AgtypeMap;
import org.postgresql.util.PGobject;
import org.postgresql.util.PSQLException;
import org.postgresql.util.PSQLState;

/**
 * Stores values of various kinds in a single object for use in PostgreSQL. The text representation
 * is built on top of the <a href="https://tools.ietf.org/html/rfc8259">JSON format
 * specification</a>. The goal of the text representation is making it compatible with JSON as much
 * as possible so that valid JSON values can be parsed without effort.
 * <br><br>
 * Valid Agtypes:
 * <ul>
 * <li>null</li>
 * <li>int</li>
 * <li>long</li>
 * <li>double</li>
 * <li>boolean</li>
 * <li>String</li>
 * <li>{@link AgtypeList}</li>
 * <li>{@link AgtypeMap}</li>
 * </ul>
 */
public class Agtype extends PGobject implements Cloneable {

    private Object obj;

    /**
     * Public constructor for Agtype. Do not call directly, use the AgtypeFactory when creating
     * Agtype objects on the client-side and casting the received object in the ResultSet when the
     * object is created on the server-side.
     */
    public Agtype() {
        super.setType("ag_catalog.agtype");
    }

    Agtype(Object obj) {
        this();

        this.obj = obj;
    }

    /**
     * TODO: need to define for PreparedStatement.
     */
    @Override
    public String getValue() {
        if (value == null) {
            value = AgtypeUtil.serializeAgtype(obj);
        }

        return value;
    }

    /**
     * Parses the serialized value to it's Agtype value. {@inheritDoc}
     *
     * @param value Serialized representation of Agtype value.
     * @throws SQLException throws if the String value cannot be parsed to a valid Agtype.
     * @see AgtypeUtil#parse(String)
     */
    @Override
    public void setValue(String value) throws SQLException {
        try {
            obj = AgtypeUtil.parse(value);
        } catch (Exception e) {
            throw new PSQLException("Parsing AgType failed", PSQLState.DATA_ERROR, e);
        }

        super.setValue(value);
    }

    /**
     * Returns the value stored in Agtype as a String. Attempts to perform an implicit conversion of
     * types stored as non-strings values.
     *
     * @return value stored in Agtype as a String.
     * @throws InvalidAgtypeException Throws if the stored Agtype value cannot be represented as a
     *                                String.
     * @see AgtypeUtil#getString(Object)
     */
    public String getString() throws InvalidAgtypeException {
        return AgtypeUtil.getString(obj);
    }

    /**
     * Returns the value stored in Agtype as a generic object.
     *
     * @return value stored in Agtype as a generic object.
     */
    public Object getObject() throws InvalidAgtypeException {
        return obj;
    }

    /**
     * Returns the value stored in Agtype as an int. Attempts to perform an implicit conversion of
     * types stored as non-int values.
     *
     * @return value stored in Agtype as an int.
     * @throws InvalidAgtypeException Throws if the stored Agtype value cannot be represented as an
     *                                int.
     * @see AgtypeUtil#getInt(Object)
     */
    public int getInt() throws InvalidAgtypeException {
        return AgtypeUtil.getInt(obj);
    }

    /**
     * Returns the value stored in Agtype as a long. Attempts to perform an implicit conversion of
     * types stored as non-long values.
     *
     * @return value stored in Agtype as a long.
     * @throws InvalidAgtypeException Throws if the stored Agtype value cannot be represented as an
     *                                long.
     * @see AgtypeUtil#getLong(Object)
     */
    public long getLong() throws InvalidAgtypeException {
        return AgtypeUtil.getLong(obj);
    }

    /**
     * Returns the value stored in Agtype as a double. Attempts to perform an implicit conversion of
     * types stored as non-double values.
     *
     * @return value stored in Agtype as a double.
     * @throws InvalidAgtypeException Throws if the stored Agtype value cannot be represented as an
     *                                double.
     * @see AgtypeUtil#getDouble(Object)
     */
    public double getDouble() throws InvalidAgtypeException {
        return AgtypeUtil.getDouble(obj);
    }

    /**
     * Returns the value stored in Agtype as a boolean. Attempts to perform an implicit conversion
     * of types stored as non-boolean values.
     *
     * @return value stored in Agtype as a long.
     * @throws InvalidAgtypeException Throws if the stored Agtype value cannot be represented as an
     *                                boolean.
     * @see AgtypeUtil#getBoolean(Object)
     */
    public boolean getBoolean() throws InvalidAgtypeException {
        return AgtypeUtil.getBoolean(obj);
    }

    /**
     * Returns the value stored in Agtype as an AgtypeList.
     *
     * @return value stored in Agtype as an AgtypeList.
     * @throws InvalidAgtypeException Throws if the stored Agtype value cannot be represented as an
     *                                AgtypeList.
     * @see AgtypeUtil#getList(Object)
     */
    public AgtypeList getList() throws InvalidAgtypeException {
        return AgtypeUtil.getList(obj);
    }

    /**
     * Returns the value stored in Agtype as an AgtypeMap.
     *
     * @return value stored in Agtype as an AgtypeMap.
     * @throws InvalidAgtypeException Throws if the stored Agtype value cannot be represented as an
     *                                AgtypeMap.
     * @see AgtypeUtil#getMap(Object)
     */
    public AgtypeMap getMap() throws InvalidAgtypeException {
        return AgtypeUtil.getMap(obj);
    }

    /**
     * Returns whether stored is Agtype Null.
     *
     * @return true if the value is Agtype null, false otherwise.
     */
    public boolean isNull() {
        return obj == null;
    }

    /**
     * Returns a string representation of this Agtype object.
     *
     * @return a string representation of this Agtype object.
     */
    @Override
    public String toString() {
        if (obj != null && obj instanceof AgtypeAnnotation) {
            return obj
                + (type != null ? "::" + ((AgtypeAnnotation) obj).getAnnotation() : "");
        }
        return (obj != null ? obj.toString() : "null")
            + (type != null ? "::" + type : "");
    }

}
