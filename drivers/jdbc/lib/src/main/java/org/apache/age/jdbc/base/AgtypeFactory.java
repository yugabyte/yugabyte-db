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
