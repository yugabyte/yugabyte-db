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

import java.util.HashMap;
import java.util.Set;
import org.apache.age.jdbc.base.AgtypeUtil;
import org.apache.age.jdbc.base.InvalidAgtypeException;

public class AgtypeMapImpl extends HashMap<String, Object> implements Cloneable,
    AgtypeMap {

    @Override
    public Set<Entry<String, Object>> entrySet() {
        return super.entrySet();
    }

    @Override
    public Set<String> keySet() {
        return super.keySet();
    }

    @Override
    public boolean containsKey(String key) {
        return super.containsKey(key);
    }

    @Override
    public String getString(String key) throws InvalidAgtypeException {
        return AgtypeUtil.getString(get(key));
    }

    @Override
    public String getString(String key, String defaultValue) throws InvalidAgtypeException {
        return containsKey(key) ? getString(key) : defaultValue;
    }

    @Override
    public int getInt(String key) throws InvalidAgtypeException {
        return AgtypeUtil.getInt(get(key));
    }

    @Override
    public int getInt(String key, int defaultValue) throws InvalidAgtypeException {
        return containsKey(key) ? getInt(key) : defaultValue;
    }

    @Override
    public long getLong(String key) throws InvalidAgtypeException {
        return AgtypeUtil.getLong(get(key));
    }

    @Override
    public long getLong(String key, long defaultValue) throws InvalidAgtypeException {
        return containsKey(key) ? getLong(key) : defaultValue;
    }

    @Override
    public double getDouble(String key) throws InvalidAgtypeException {
        return AgtypeUtil.getDouble(get(key));
    }

    @Override
    public double getDouble(String key, double defaultValue) throws InvalidAgtypeException {
        return containsKey(key) ? getDouble(key) : defaultValue;
    }

    @Override
    public boolean getBoolean(String key) throws InvalidAgtypeException {
        return AgtypeUtil.getBoolean(get(key));
    }

    @Override
    public boolean getBoolean(String key, boolean defaultValue) throws InvalidAgtypeException {
        return containsKey(key) ? getBoolean(key) : defaultValue;
    }

    @Override
    public AgtypeList getList(String key) throws InvalidAgtypeException {
        return AgtypeUtil.getList(get(key));
    }

    @Override
    public AgtypeMap getMap(String key) throws InvalidAgtypeException {
        return AgtypeUtil.getMap(get(key));
    }

    @Override
    public Object getObject(String key) {
        return get(key);
    }

    @Override
    public boolean isNull(String key) {
        return get(key) == null;
    }
}
