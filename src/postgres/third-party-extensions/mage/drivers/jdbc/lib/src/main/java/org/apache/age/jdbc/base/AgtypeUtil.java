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

import java.util.StringJoiner;
import org.antlr.v4.runtime.BaseErrorListener;
import org.antlr.v4.runtime.CharStream;
import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.CommonTokenStream;
import org.antlr.v4.runtime.RecognitionException;
import org.antlr.v4.runtime.Recognizer;
import org.antlr.v4.runtime.TokenStream;
import org.antlr.v4.runtime.tree.ParseTreeWalker;
import org.apache.age.jdbc.antlr4.AgtypeLexer;
import org.apache.age.jdbc.antlr4.AgtypeParser;
import org.apache.age.jdbc.base.type.AgtypeList;
import org.apache.age.jdbc.base.type.AgtypeListBuilder;
import org.apache.age.jdbc.base.type.AgtypeMap;
import org.apache.age.jdbc.base.type.AgtypeMapBuilder;
import org.apache.commons.text.StringEscapeUtils;

/**
 * A set of utility methods to assist in using Agtype.
 */
public class AgtypeUtil {

    private static BaseErrorListener baseErrorListener = new BaseErrorListener() {
        @Override
        public void syntaxError(Recognizer<?, ?> recognizer, Object offendingSymbol, int line,
            int charPositionInLine, String msg, RecognitionException e) {
            throw new IllegalStateException(
                "Failed to parse at line " + line + " due to " + msg, e);
        }
    };

    /**
     * Do not instantiate AgtypeUtil, all methods are static.
     */
    private AgtypeUtil() {
    }

    /**
     * Returns to object as a double, if it is a valid double, otherwise will throw an exception.
     * <br><br>
     * Rules for converting from other types
     * <ul>
     *   <li>Agtype null - Converted to 0.0</li>
     *   <li>true - Converted to 1.0</li>
     *   <li>false - Converted to 0.0</li>
     *   <li>Integer/Long - Converted to it's double value. Follows
     *   <a href="https://docs.oracle.com/javase/specs/jls/se7/html/jls-5.html#jls-5.1.2"> Oracle's
     *   specifications </a>for widening primitives.</li>
     *   <li>Strings - Parsed to its double value, if it cannot be converted to a double, a
     *   NumberFormatException will be thrown</li>
     *   <li>{@link AgtypeList}/{@link AgtypeMap} - Throws InvalidAgtypeException</li>
     *   <li>All other values will throw an InvalidAgtypeException</li>
     * </ul>
     *
     * @param obj Object to parse
     * @return Object as a double
     * @throws NumberFormatException  if the given object is a number or a string that cannot be
     *                                parsed to a double
     * @throws InvalidAgtypeException if the given object is not a number or string and cannot be
     *                                converted to a double
     */
    public static double getDouble(Object obj)
        throws NumberFormatException, InvalidAgtypeException {
        if (obj == null) {
            return 0.0;
        } else if (obj instanceof Double) {
            return (Double) obj;
        } else if (obj instanceof Boolean) {
            return (Boolean) obj ? 1.0 : 0.0;
        } else if (obj instanceof String) {
            return Double.parseDouble((String) obj);
        } else if (obj instanceof Long) {
            return ((Long) obj).doubleValue();
        }

        throw new InvalidAgtypeException("Not a double: " + obj);
    }

    /**
     * Returns the object as an integer, if it is a valid integer, otherwise will throw an
     * exception.
     * <br><br>
     * Rules for converting from other types:
     * <ul>
     *   <li>null - Converted to 0</li>
     *   <li>true - Converted to 1</li>
     *   <li>false - Converted to 0</li>
     *   <li>Double - Converted to it's integer value. Follows
     *   <a href="https://docs.oracle.com/javase/specs/jls/se7/html/jls-5.html#jls-5.1.3"> Oracle's
     *   specifications </a>for widening primitives.</li>
     *   <li>Strings - Parsed to its integer value, an Exception will be thrown if its not an
     *   integer</li>
     *   <li>All other values will throw an InvalidAgtypeException</li>
     * </ul>
     *
     * @param obj Object to parse
     * @return Object as an int
     * @throws NumberFormatException  if the given object is a number or string that cannot be
     *                                parsed into an Integer
     * @throws InvalidAgtypeException if the given object is not a number of a string
     */
    public static int getInt(Object obj) throws NumberFormatException, InvalidAgtypeException {
        long l;
        try {
            l = getLong(obj);
            if (l >= Integer.MIN_VALUE && l <= Integer.MAX_VALUE) {
                return (int) l;
            }
        } catch (InvalidAgtypeException ex) {
            throw new InvalidAgtypeException("Not a int: " + obj, ex);
        }
        throw new NumberFormatException("Bad value for type int: " + l);
    }

    /**
     * Returns to object as a long, if it is a valid long, otherwise will throw an exception.
     * <br><br>
     * Rules for converting from other types:
     * <ul>
     *   <li>null - Converted to 0</li>
     *   <li>true - Converted to 1</li>
     *   <li>false - Converted to 0</li>
     *   <li>Double - Converted to it's integer value. Follows
     *   <a href="https://docs.oracle.com/javase/specs/jls/se7/html/jls-5.html#jls-5.1.3"> Oracle's
     *   specifications </a>for widening primitives.</li>
     *   <li>Strings - Parsed to its integer value, an Exception will be thrown if its not an
     *   integer</li>
     *   <li>All other values will throw an InvalidAgtypeException</li>
     * </ul>
     *
     * @param obj Object to parse
     * @return Object as an long
     * @throws InvalidAgtypeException if the object cannot be converted to a Long
     * @throws NumberFormatException  if the given object is a number or string that cannot be
     *                                parsed into a double
     */
    public static long getLong(Object obj) throws NumberFormatException, InvalidAgtypeException {
        if (obj == null) {
            return 0;
        } else if (obj instanceof Long) {
            return (Long) obj;
        } else if (obj instanceof String) {
            return (long) Double.parseDouble((String) obj);
        } else if (obj instanceof Boolean) {
            return (boolean) obj ? 1 : 0;
        } else if (obj instanceof Double) {
            return ((Double) obj).longValue();
        }

        throw new InvalidAgtypeException("Not a long: " + obj);
    }

    /**
     * Returns to object as a string, if it is a valid string, otherwise will throw an exception.
     * <br><br>
     * Rules for converting from other types:
     * <ul>
     *   <li>null - Returns null</li>
     *   <li>Long - Returns a String representation of the Long</li>
     *   <li>Double - Returns a String representation of the double</li>
     *   <li>Boolean - Returns a String representation of the boolean</li>
     *   <li>All other values will throw an InvalidAgtypeException</li>
     * </ul>
     *
     * @param obj Object to parse to a String
     * @return Object as an string
     * @throws InvalidAgtypeException if the object cannot be converted to a String
     */
    public static String getString(Object obj) throws InvalidAgtypeException {
        if (obj == null) {
            return null;
        } else if (obj instanceof String) {
            return (String) obj;
        } else if (obj instanceof Long) {
            return Long.toString((long) obj);
        } else if (obj instanceof Double) {
            return Double.toString((double) obj);
        } else if (obj instanceof Boolean) {
            return Boolean.toString((boolean) obj);
        }

        throw new InvalidAgtypeException("Not a string: " + obj);
    }

    /**
     * Returns to object as a boolean, if it is a valid boolean, otherwise will throw an exception.
     * <br><br>
     * Data Conversions from other types
     * <ul>
     *   <li>null - Returns false</li>
     *   <li>Long - Returns false is the value is 0, returns true otherwise.</li>
     *   <li>Double - Returns false is the value is 0.0, returns true otherwise.</li>
     *   <li>String - Returns false if the length of the String is 0, returns true otherwise.</li>
     *   <li>
     *     {@link AgtypeList} - Returns false if the size of the list is 0, returns true otherwise.
     *   </li>
     *   <li>
     *     {@link AgtypeMap} - Returns false if the size of the map is 0, returns true otherwise.
     *   </li>
     *   <li>All other values will throw an InvalidAgtypeException</li>
     * </ul>
     *
     * @param obj Object to parse
     * @return Object as an boolean
     * @throws InvalidAgtypeException if the object cannot be converted to a boolean
     */
    public static boolean getBoolean(Object obj) throws InvalidAgtypeException {
        if (obj == null) {
            return false;
        } else if (obj instanceof Boolean) {
            return (Boolean) obj;
        } else if (obj instanceof String) {
            return ((String) obj).length() > 0;
        } else if (obj instanceof Long) {
            return (Long) obj != 0L;
        } else if (obj instanceof Double) {
            return (Double) obj != 0.0;
        } else if (obj instanceof AgtypeList) {
            return ((AgtypeList) obj).size() > 0;
        } else if (obj instanceof AgtypeMap) {
            return ((AgtypeMap) obj).size() > 0;
        }
        throw new InvalidAgtypeException("Not a valid Agtype: " + obj);
    }

    /**
     * Returns to object as an {@link AgtypeList}. If this obj is not an AgtypeList, an
     * InvalidAgtypeException will be thrown.
     *
     * @param obj Object to parse and return as an AgtypeList
     * @return Object as an agTypeArray
     * @throws InvalidAgtypeException if the object cannot be converted to an AgtypeList
     */
    public static AgtypeList getList(Object obj) throws InvalidAgtypeException {
        if (obj == null) {
            return null;
        } else if (obj instanceof AgtypeList) {
            return (AgtypeList) obj;
        }

        throw new InvalidAgtypeException("Not an AgtypeList: " + obj);
    }

    /**
     * Returns to object as an {@link AgtypeMap}. If this obj is not an AgtypeMap, an
     * InvalidAgtypeException will be thrown.
     *
     * @param obj Object to parse and return as an AgtypeMap
     * @return Object as an AgtypeMap
     * @throws InvalidAgtypeException if the object cannot be converted to an AgtypeMap
     */
    public static AgtypeMap getMap(Object obj) throws InvalidAgtypeException {
        if (obj == null) {
            return null;
        } else if (obj instanceof AgtypeMap) {
            return (AgtypeMap) obj;
        }

        throw new InvalidAgtypeException("Not an AgtypeMap: " + obj);
    }

    /**
     * Creates a new AgtypeMapBuilder.
     *
     * @return Newly created AgtypeMapBuilder
     */
    public static AgtypeMapBuilder createMapBuilder() {
        return new AgtypeMapBuilder();
    }

    /**
     * Creates a new AgtypeListBuilder.
     *
     * @return Newly created AgtypeListBuilder
     */
    public static AgtypeListBuilder createListBuilder() {
        return new AgtypeListBuilder();
    }

    /**
     * Converts a serialized Agtype value into it's non-serialized value.
     *
     * @param strAgtype Serialized Agtype value to be parsed.
     * @return Parsed object that can be stored in {@link Agtype}
     * @throws IllegalStateException if the value cannot be parsed into an Agtype.
     */
    public static Object parse(String strAgtype) throws IllegalStateException {
        CharStream charStream = CharStreams.fromString(strAgtype);
        AgtypeLexer lexer = new AgtypeLexer(charStream);
        TokenStream tokens = new CommonTokenStream(lexer);
        AgtypeParser parser = new AgtypeParser(tokens);

        lexer.removeErrorListeners();
        lexer.addErrorListener(baseErrorListener);

        parser.removeErrorListeners();
        parser.addErrorListener(baseErrorListener);

        AgtypeListener agtypeListener = new AgtypeListener();

        ParseTreeWalker walker = new ParseTreeWalker();
        walker.walk(agtypeListener, parser.agType());

        return agtypeListener.getOutput();
    }

    /**
     * Converts the passed object into its serialized Agtype form.
     *
     * @param obj Agtype object to convert into its serialized form
     * @return Serialized Agtype object
     */
    static String serializeAgtype(Object obj) {
        if (obj == null) {
            return "null";
        } else if (obj instanceof String) {
            return '"' + StringEscapeUtils.escapeJson((String) obj) + '"';
        } else if (obj instanceof AgtypeMap) {
            StringJoiner join = new StringJoiner(",", "{", "}");

            ((AgtypeMap) obj).entrySet()
                .stream()
                .map((entry) -> new StringJoiner(":")
                    .add(serializeAgtype(entry.getKey()))
                    .add(serializeAgtype(entry.getValue()))
                )
                .forEach(join::merge);

            return join.toString();
        } else if (obj instanceof AgtypeList) {
            StringJoiner join = new StringJoiner(",", "[", "]");

            ((AgtypeList) obj)
                .stream()
                .map(AgtypeUtil::serializeAgtype)
                .forEach(join::add);

            return join.toString();
        }

        return String.valueOf(obj);
    }
}
