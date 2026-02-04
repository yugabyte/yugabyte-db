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

package org.apache.age.jdbc.base.type;

import java.util.Map;
import java.util.Set;
import org.apache.age.jdbc.base.AgtypeUtil;
import org.apache.age.jdbc.base.InvalidAgtypeException;

/**
 * Non-Mutable Map of Agtype values. This implementation provides partial implementation of map
 * operations, and permits null values, but not null keys. This class makes no guarantees as to the
 * order of the map; in particular, it does not guarantee that the order will remain constant over
 * time.
 *
 * @see AgtypeMapBuilder
 */
public interface AgtypeMap extends AgtypeObject {

    /**
     * Returns a set of keys.
     *
     * @return a set of keys
     */
    Set<String> keySet();

    /**
     * Returns true if the given key is contained.
     *
     * @param key the given key
     * @return true if the given key is contained
     */
    boolean containsKey(String key);

    /**
     * Returns the String value to which the specified key is mapped, or null if this AgtypeMap
     * contains no mapping for the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the string value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a String
     * @see AgtypeUtil#getString(Object)
     */
    String getString(String key) throws InvalidAgtypeException;

    /**
     * Returns the String value to which the specified key is mapped, or defaultValue if this
     * AgtypeMap contains no mapping for the key.
     *
     * @param key          the key whose associated value is to be returned
     * @param defaultValue the default mapping of the key
     * @return the string value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a String
     * @see AgtypeUtil#getString(Object)
     */
    String getString(String key, String defaultValue) throws InvalidAgtypeException;

    /**
     * Returns the int value to which the specified key is mapped, or 0 if this AgtypeMap contains
     * no mapping for the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the int value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to an int
     * @see AgtypeUtil#getInt(Object)
     */
    int getInt(String key) throws InvalidAgtypeException;

    /**
     * Returns the int value to which the specified key is mapped, or defaultValue if this AgtypeMap
     * contains no mapping for the key.
     *
     * @param key          the key whose associated value is to be returned
     * @param defaultValue the default mapping of the key
     * @return the int value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to an int
     * @see AgtypeUtil#getInt(Object)
     */
    int getInt(String key, int defaultValue) throws InvalidAgtypeException;

    /**
     * Returns the long value to which the specified key is mapped, or 0 if this AgtypeMap contains
     * no mapping for the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the long value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a long
     * @see AgtypeUtil#getLong(Object)
     */
    long getLong(String key) throws InvalidAgtypeException;

    /**
     * Returns the long value to which the specified key is mapped, or defaultValue if this
     * AgtypeMap contains no mapping for the key.
     *
     * @param key          the key whose associated value is to be returned
     * @param defaultValue the default mapping of the key
     * @return the long value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a long
     * @see AgtypeUtil#getLong(Object)
     */
    long getLong(String key, long defaultValue) throws InvalidAgtypeException;

    /**
     * Returns the double value to which the specified key is mapped, or 0.0 if this AgtypeMap
     * contains no mapping for the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the double value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a double
     * @see AgtypeUtil#getDouble(Object)
     */
    double getDouble(String key) throws InvalidAgtypeException;

    /**
     * Returns the double value to which the specified key is mapped, or defaultValue if this
     * AgtypeMap contains no mapping for the key.
     *
     * @param key          the key whose associated value is to be returned
     * @param defaultValue the default mapping of the key
     * @return the double value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a double
     * @see AgtypeUtil#getDouble(Object)
     */
    double getDouble(String key, double defaultValue) throws InvalidAgtypeException;

    /**
     * Returns the boolean value to which the specified key is mapped, or false if this AgtypeMap
     * contains no mapping for the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the boolean value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a boolean
     * @see AgtypeUtil#getBoolean(Object)
     */
    boolean getBoolean(String key) throws InvalidAgtypeException;

    /**
     * Returns the boolean value to which the specified key is mapped, or defaultValue if this
     * AgtypeMap contains no mapping for the key.
     *
     * @param key          the key whose associated value is to be returned
     * @param defaultValue the default mapping of the key
     * @return the boolean value stored at the key
     * @throws InvalidAgtypeException Throws if the value cannot be converted to a boolean
     * @see AgtypeUtil#getBoolean(Object)
     */
    boolean getBoolean(String key, boolean defaultValue) throws InvalidAgtypeException;

    /**
     * Returns the AgtypeList value to which the specified key is mapped, or null if this AgtypeMap
     * contains no mapping for the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the AgtypeList value stored at the key
     * @throws InvalidAgtypeException Throws if the value is not an AgtypeList
     * @see AgtypeUtil#getList(Object)
     */
    AgtypeList getList(String key) throws InvalidAgtypeException;

    /**
     * Returns the AgtypeMap value to which the specified key is mapped, or null if this AgtypeMap
     * contains no mapping for the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the AgtypeMap value stored at the key
     * @throws InvalidAgtypeException Throws if the value is not a AgtypeMap
     * @see AgtypeUtil#getMap(Object)
     */
    AgtypeMap getMap(String key) throws InvalidAgtypeException;

    /**
     * Returns the value stored at the key.
     *
     * @param key the key whose associated value is to be returned
     * @return the object value stored at the key
     */
    Object getObject(String key);

    /**
     * Returns true if the value stored at the key is null.
     *
     * @param key the given key
     * @return true if the value stored at the key is null
     */
    boolean isNull(String key);

    /**
     * Returns the size of this AgtypeMap.
     *
     * @return the size of this AgtypeMap
     */
    int size();

    /**
     * Returns a Set view of the mappings contained in this map.
     *
     * @return a set view of the mappings contained in this map
     */
    Set<Map.Entry<String, Object>> entrySet();
}
