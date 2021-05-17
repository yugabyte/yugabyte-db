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
