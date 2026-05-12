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

import java.util.ArrayList;
import java.util.stream.Stream;
import org.apache.age.jdbc.base.AgtypeUtil;
import org.apache.age.jdbc.base.InvalidAgtypeException;

public class AgtypeListImpl extends ArrayList<Object> implements Cloneable,
    AgtypeList {

    @Override
    public String getString(int index) throws InvalidAgtypeException {
        return AgtypeUtil.getString(get(index));
    }

    @Override
    public int getInt(int index) throws InvalidAgtypeException {
        return AgtypeUtil.getInt(get(index));
    }

    @Override
    public long getLong(int index) throws InvalidAgtypeException {
        return AgtypeUtil.getLong(get(index));
    }

    @Override
    public double getDouble(int index) throws InvalidAgtypeException {
        return AgtypeUtil.getDouble(get(index));
    }

    @Override
    public boolean getBoolean(int index) throws InvalidAgtypeException {
        return AgtypeUtil.getBoolean(get(index));
    }

    @Override
    public AgtypeList getList(int index) throws InvalidAgtypeException {
        return AgtypeUtil.getList(get(index));
    }

    @Override
    public AgtypeMap getMap(int index) throws InvalidAgtypeException {
        return AgtypeUtil.getMap(get(index));
    }

    @Override
    public Stream<Object> stream() {
        return super.stream();
    }

    @Override
    public Object getObject(int index) {
        return get(index);
    }
}
