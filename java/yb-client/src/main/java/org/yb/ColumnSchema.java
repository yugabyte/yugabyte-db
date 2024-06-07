// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb;

import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

/**
 * Represents a YB Table column. Use {@link ColumnSchema.ColumnSchemaBuilder} in order to
 * create columns.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class ColumnSchema {

  private final Integer id;
  private final String name;
  // TODO Having both type and yqlType is redundant. After YugaWare is updated and
  // inherited dependencies cleaned up, type can be removed.
  private final Type type;
  private final QLType yqlType;
  private final boolean key;
  private final boolean hashKey;
  private final boolean nullable;
  private final SortOrder sortOrder;

  public enum SortOrder {
    NONE(0),
    ASC(1),
    DESC(2);

    private final int sortOrder;

    SortOrder(int sortOrder) {
      this.sortOrder = sortOrder;
    }

    public int getValue() {
      return sortOrder;
    }

    public static SortOrder findFromValue(int value) {
      for (SortOrder sortOrder : values()) {
        if (sortOrder.getValue() == value) {
          return sortOrder;
        }
      }
      return NONE;
    }
  };

  private ColumnSchema(Integer id, String name, QLType yqlType, boolean key, boolean hashKey,
                       boolean nullable, SortOrder sortOrder) {
    this.id = id;
    this.name = name;
    this.type = yqlType.toType();
    this.yqlType = yqlType;
    this.key = key;
    this.hashKey = hashKey;
    this.nullable = nullable;
    this.sortOrder = sortOrder;
  }

  public Integer getId() {
    return id;
  }

  /**
   * Get the column's Type
   * @return the type
   */
  @Deprecated
  public Type getType() {
    return type;
  }


  /**
   * Get the column's QLType
   * @return the type
   */
  public QLType getQLType() {
    return yqlType;
  }

  /**
   * Get the column's name
   * @return A string representation of the name
   */
  public String getName() {
    return name;
  }

  /**
   * Answers if the column part of the key
   * @return true if the column is part of the key, else false
   */
  public boolean isKey() {
    return key;
  }

  /**
   * Answers if the column is used in hashing part of the key
   * @return true if the column is part of the hash key, else false
   */
  public boolean isHashKey() {
    return hashKey;
  }

  /**
   * Returns the sort order. Valid only if the key is a range primary key.
   * @return the sort order.
   */
  public SortOrder getSortOrder() {
    return sortOrder;
  }

  /**
   * Answers if the column can be set to null
   * @return true if it can be set to null, else false
   */
  public boolean isNullable() {
    return nullable;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;

    ColumnSchema that = (ColumnSchema) o;

    if (key != that.key) return false;
    if (hashKey != that.hashKey) return false;
    if (!name.equals(that.name)) return false;
    if (!type.equals(that.type)) return false;

    return true;
  }

  @Override
  public int hashCode() {
    int result = name.hashCode();
    result = 31 * result + type.hashCode();
    result = 31 * result + (key ? 1 : 0);
    return result;
  }

  @Override
  public String toString() {
    return "Column name: " + name + ", type: " + type.getName();
  }

  /**
   * Builder for ColumnSchema.
   */
  public static class ColumnSchemaBuilder {
    private Integer id = null;
    private final String name;
    private final QLType yqlType;
    private boolean key = false;
    private boolean hashKey = false;
    private boolean nullable = false;
    private SortOrder sortOrder = SortOrder.NONE;

    /**
     * Constructor for the required parameters.
     * @param name column's name
     * @param type column's type
     */
    @Deprecated
    public ColumnSchemaBuilder(String name, Type type) {
      this.name = name;
      this.yqlType = QLType.fromType(type);
    }

    /**
     * Constructor for the required parameters.
     * @param name column's name
     * @param yqlType column's QL Type
     */
    public ColumnSchemaBuilder(String name, QLType yqlType) {
      this.name = name;
      this.yqlType = yqlType;
    }

    /**
     * Sets if the column id is known. null by default.
     * @param id an int that is the column's id
     * @return this instance
     */
    public ColumnSchemaBuilder id(Integer id) {
      this.id = id;
      return this;
    }

    /**
     * Sets if the column is part of the row key. False by default.
     * @param key a boolean that indicates if the column is part of the key
     * @return this instance
     */
    public ColumnSchemaBuilder key(boolean key) {
      return rangeKey(key, SortOrder.NONE);
    }

    /**
     * Sets if the column is part of the range row key, along with a sort order.
     * @param key a boolean that indicates if the column is part of the key. False by default.
     * @param sortOrder an enum that indicates if the column is sorted in ascending, descending, or
     *                  no order
     * @return this instance.
     */
    public ColumnSchemaBuilder rangeKey(boolean key, SortOrder sortOrder) {
      this.key = key;
      this.sortOrder = sortOrder;
      return this;
    }

    /**
     * Sets if the column is to be used in the hash part of the row key. False by default.
     * @param hashKey a boolean that indicates if the column is part of the hash key
     * @return this instance
     */
    public ColumnSchemaBuilder hashKey(boolean hashKey) {
      this.hashKey = hashKey;
      if (hashKey) {
        this.key = true;
      }
      return this;
    }

    /**
     * Marks the column as allowing null values. False by default.
     * @param nullable a boolean that indicates if the column allows null values
     * @return this instance
     */
    public ColumnSchemaBuilder nullable(boolean nullable) {
      this.nullable = nullable;
      return this;
    }

    /**
     * Builds a {@link ColumnSchema} using the passed parameters.
     * @return a new {@link ColumnSchema}
     */
    public ColumnSchema build() {
      return new ColumnSchema(id, name, yqlType, key, hashKey, nullable, sortOrder);
    }
  }
}
