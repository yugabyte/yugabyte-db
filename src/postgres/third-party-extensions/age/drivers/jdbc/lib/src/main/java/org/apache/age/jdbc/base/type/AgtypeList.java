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

import java.util.Iterator;
import java.util.Spliterator;
import java.util.function.Consumer;
import java.util.stream.Stream;
import org.apache.age.jdbc.base.Agtype;
import org.apache.age.jdbc.base.AgtypeUtil;
import org.apache.age.jdbc.base.InvalidAgtypeException;

/**
 * Non-mutable list of Agtype values.
 *
 * @see AgtypeListBuilder
 * @see Agtype
 */
public interface AgtypeList extends AgtypeObject {

    /**
     * Performs the given action for each element of the Iterable until all elements have been
     * processed or the action throws an exception. Unless otherwise specified by the implementing
     * class, actions are performed in the order of iteration (if an iteration order is specified).
     * Exceptions thrown by the action are relayed to the caller.
     *
     * @param action The action to be performed for each element
     * @throws NullPointerException - if the specified action is null
     */
    void forEach(Consumer<? super Object> action);

    /**
     * Returns the String value at the specified position in this list. Throws an
     * InvalidAgtypeException if the element is not a String.
     *
     * @param index index of the element to return
     * @return the String value at the specified position in this list
     * @throws InvalidAgtypeException    if the value cannot be converted to a String
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     * @see AgtypeUtil#getString(Object)
     */
    String getString(int index) throws InvalidAgtypeException;

    /**
     * Returns the int value at the specified position in this list. Throws an
     * InvalidAgtypeException if the element is not an int.
     *
     * @param index index of the element to return
     * @return the int value at the specified position in this list
     * @throws InvalidAgtypeException    if the value cannot be converted to an int
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     * @see AgtypeUtil#getInt(Object)
     */
    int getInt(int index) throws InvalidAgtypeException;

    /**
     * Returns the long value at the specified position in this list. Throws an
     * InvalidAgtypeException if the element is not a long.
     *
     * @param index index of the element to return
     * @return the long value at the specified position in this list
     * @throws InvalidAgtypeException    if the value cannot be converted to a long
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     * @see AgtypeUtil#getLong(Object)
     */
    long getLong(int index) throws InvalidAgtypeException;

    /**
     * Returns the double value at the specified position in this list. Throws an
     * InvalidAgtypeException if the element is not a double.
     *
     * @param index index of the element to return
     * @return the double value at the specified position in this list
     * @throws InvalidAgtypeException    if the value cannot be converted to a double
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     * @see AgtypeUtil#getDouble(Object)
     */
    double getDouble(int index) throws InvalidAgtypeException;

    /**
     * Returns the double value at the specified position in this list. Throws an
     * InvalidAgtypeException if the element is not a double.
     *
     * @param index index of the element to return
     * @return the boolean value at the specified position in this list
     * @throws InvalidAgtypeException    if the value cannot be converted to a boolean
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     * @see AgtypeUtil#getBoolean(Object)
     */
    boolean getBoolean(int index) throws InvalidAgtypeException;

    /**
     * Returns the AgtypeList at the specified position in this list. Throws an
     * InvalidAgtypeException if the element is not an AgtypeList.
     *
     * @param index index of the element to return
     * @return the AgtypeList at the specified position in this list
     * @throws InvalidAgtypeException    if the value stored at the index is not an AgtypeList
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     * @see AgtypeUtil#getList(Object)
     */
    AgtypeList getList(int index) throws InvalidAgtypeException;

    /**
     * Returns the AgtypeMap at the specified position in this list. Throws an
     * InvalidAgtypeException if the element is not an AgtypeMap.
     *
     * @param index index of the element to return
     * @return the AgtypeList at the specified position in this list
     * @throws InvalidAgtypeException    if the value stored at the index is not an AgtypeMap
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     * @see AgtypeUtil#getMap(Object)
     */
    AgtypeMap getMap(int index) throws InvalidAgtypeException;

    /**
     * Returns the object at the specified position in this list.
     *
     * @param index index of the element to return
     * @return the object at the specified position in this list
     * @throws IndexOutOfBoundsException if the index is out of range (index {@literal <} 0 || index
     *                                   {@literal >}= size())
     */
    Object getObject(int index);

    /**
     * Returns an iterator over the elements.
     *
     * @return Iterator over the elements
     */
    Iterator<Object> iterator();

    /**
     * Returns the size of this AgtypeList.
     *
     * @return the size of this AgtypeList
     */
    int size();

    /**
     * Creates a Spliterator over the elements described by this Iterable.
     *
     * @return Spliterator over the elements described by this Iterable
     */
    Spliterator<Object> spliterator();

    /**
     * Returns a sequential Stream with this collection as its source.
     *
     * @return a sequential Stream with this collection as its source.
     */
    Stream<Object> stream();

}
