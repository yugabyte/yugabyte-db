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
 * A builder for creating an AgtypeList object. This class initializes a AgtypeList and provides
 * methods to add values return the resulting AgtypeList. The methods in this class can be chained
 * to add multiple values to the AgtypeList.
 *
 * @see AgtypeList
 */
public class AgtypeListBuilder {

    private final AgtypeListImpl agtypeList;

    /**
     * Initializes an empty List BuilderAgtypeList.
     */
    public AgtypeListBuilder() {
        agtypeList = new AgtypeListImpl();
    }

    /**
     * Appends the Agtype value to the AglistBuilder.
     *
     * @param value the Agtype value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(Agtype value) {
        agtypeList.add(value.getObject());
        return this;
    }

    /**
     * Appends the int to the AglistBuilder.
     *
     * @param value the int value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(int value) {
        agtypeList.add((long) value);
        return this;
    }

    /**
     * Appends the String to the AglistBuilder.
     *
     * @param value the String value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(String value) {
        agtypeList.add(value);
        return this;
    }

    /**
     * Appends the double to the AglistBuilder.
     *
     * @param value the double value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(double value) {
        agtypeList.add(value);
        return this;
    }

    /**
     * Appends the long to the AglistBuilder.
     *
     * @param value the long value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(long value) {
        agtypeList.add(value);
        return this;
    }

    /**
     * Appends the boolean to the AglistBuilder.
     *
     * @param value the boolean value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(boolean value) {
        agtypeList.add(value);
        return this;
    }

    /**
     * Appends the AgtypeList to the AglistBuilder.
     *
     * @param value the AgtypeList value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(AgtypeList value) {
        agtypeList.add(value);
        return this;
    }

    /**
     * Appends the AgtypeMap to the AglistBuilder.
     *
     * @param value the AgtypeMap value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(AgtypeMap value) {
        agtypeList.add(value);
        return this;
    }

    /**
     * Appends the AglistBuilder to the AglistBuilder.
     *
     * @param value the AgtypeListBuilder value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(AgtypeListBuilder value) {
        agtypeList.add(value.build());
        return this;
    }

    /**
     * Appends the AgmapBuilder to the AglistBuilder.
     *
     * @param value the AgtypeMapBuilder value to be added to the end of the list
     * @return a reference to this object.
     */
    public AgtypeListBuilder add(AgtypeMapBuilder value) {
        agtypeList.add(value.build());
        return this;
    }

    /**
     * Appends the null to the AglistBuilder.
     *
     * @return a reference to this object.
     */
    public AgtypeListBuilder addNull() {
        agtypeList.add(null);
        return this;
    }

    /**
     * Returns the AgtypeList object associated with this object builder.
     *
     * @return the AgtypeList object that is being built
     */
    public AgtypeList build() {
        return agtypeList;
    }
}
