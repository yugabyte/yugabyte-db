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

import org.apache.age.jdbc.base.Agtype;

/**
 * A builder for creating an AgtypeMap object. This class initializes a AgtypeMap object, provides
 * methods to add name/value pairs and return the resulting object. The methods in this class can be
 * chained to add multiple name/value pairs to the AgtypeMap.
 *
 * @see AgtypeMap
 * @see Agtype
 */
public class AgtypeMapBuilder {

    private final AgtypeMapImpl agtypeMap;

    /**
     * Initializes an empty AgtypeMap.
     */
    public AgtypeMapBuilder() {
        agtypeMap = new AgtypeMapImpl();
    }

    /**
     * Adds the name/int pair to the Agtype object associated with this object builder. If the
     * object contains a mapping for the specified name, this method replaces the old value with
     * int.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, int value) {
        agtypeMap.put(name, (long) value);
        return this;
    }

    /**
     * Adds the name/long pair to the Agtype object associated with this object builder. If the
     * object contains a mapping for the specified name, this method replaces the old value with
     * long.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, long value) {
        agtypeMap.put(name, value);
        return this;
    }

    /**
     * Adds the name/double pair to the Agtype object associated with this object builder. If the
     * object contains a mapping for the specified name, this method replaces the old value with
     * double.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, double value) {
        agtypeMap.put(name, value);
        return this;
    }

    /**
     * Adds the name/String pair to the Agtype object associated with this object builder. If the
     * object contains a mapping for the specified name, this method replaces the old value with
     * String.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, String value) {
        agtypeMap.put(name, value);
        return this;
    }

    /**
     * Adds the name/boolean pair to the Agtype object associated with this object builder. If the
     * object contains a mapping for the specified name, this method replaces the old value with
     * boolean.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, boolean value) {
        agtypeMap.put(name, value);
        return this;
    }

    /**
     * Adds the name/AgtypeMap pair to the Agtype object associated with this object builder. If the
     * object contains a mapping for the specified name, this method replaces the old value with
     * AgtypeMap.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, AgtypeMap value) {
        agtypeMap.put(name, value);
        return this;
    }

    /**
     * Adds the name/AgtypeList pair to the Agtype object associated with this object builder. If
     * the object contains a mapping for the specified name, this method replaces the old value with
     * AgtypeList.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, AgtypeList value) {
        agtypeMap.put(name, value);
        return this;
    }

    /**
     * Adds the name/AgmapBuilder pair to the Agtype object associated with this object builder. If
     * the object contains a mapping for the specified name, this method replaces the old value with
     * AgtypeMap.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, AgtypeMapBuilder value) {
        agtypeMap.put(name, value.build());
        return this;
    }

    /**
     * Adds the name/AglistBuilder pair to the Agtype object associated with this object builder. If
     * the object contains a mapping for the specified name, this method replaces the old value with
     * AgtypeList.
     *
     * @param name  name in the name/value pair
     * @param value the value is the object associated with this builder
     * @return this object builder
     */
    public AgtypeMapBuilder add(String name, AgtypeListBuilder value) {
        agtypeMap.put(name, value.build());
        return this;
    }

    /**
     * Adds null to the Agtype object associated with this object builder at the given name. If the
     * object contains a mapping for the specified name, this method replaces the old value with
     * AgtypeList.
     *
     * @param name Name where null is to be placed
     * @return this object builder
     */
    public AgtypeMapBuilder addNull(String name) {
        agtypeMap.put(name, null);
        return this;
    }

    /**
     * Returns the AgtypeMap object associated with this object builder.
     *
     * @return the AgtypeMap object that is being built
     */
    public AgtypeMap build() {
        return agtypeMap;
    }
}
