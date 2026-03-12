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

import org.apache.age.jdbc.base.type.AgtypeList;
import org.apache.age.jdbc.base.type.AgtypeMap;

/**
 * Factory for creating Agtype objects.
 *
 * @see Agtype
 */
public class AgtypeFactory {

    /**
     * Creates an Agtype object.
     *
     * @param obj Object to store in the an Agtype Object.
     * @return new Agtype Object
     * @throws InvalidAgtypeException Thrown if the object passed is not a {@link Agtype valid
     *                                Agtype}
     */
    public static Agtype create(Object obj) throws InvalidAgtypeException {
        if (obj == null) {
            return new Agtype(null);
        } else if (obj instanceof Integer) {
            return new Agtype(((Integer) obj).longValue());
        } else if (obj instanceof Long) {
            return new Agtype(obj);
        } else if (obj instanceof String) {
            return new Agtype(obj);
        } else if (obj instanceof Boolean) {
            return new Agtype(obj);
        } else if (obj instanceof Double) {
            return new Agtype(obj);
        } else if (obj instanceof AgtypeList) {
            return new Agtype(obj);
        } else if (obj instanceof AgtypeMap) {
            return new Agtype(obj);
        } else {
            String s = String
                .format("%s is not a valid Agtype value", obj.getClass().getSimpleName());
            throw new InvalidAgtypeException(s);
        }
    }
}
