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
package org.kududb;

import com.google.common.primitives.Ints;
import com.google.common.primitives.Longs;
import com.google.common.primitives.Shorts;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;

import static org.kududb.Common.DataType;

/**
 * Describes all the types available to build table schemas.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public enum Type {

  INT8 (DataType.INT8, "int8"),
  INT16 (DataType.INT16, "int16"),
  INT32 (DataType.INT32, "int32"),
  INT64 (DataType.INT64, "int64"),
  BINARY (DataType.BINARY, "binary"),
  STRING (DataType.STRING, "string"),
  BOOL (DataType.BOOL, "bool"),
  FLOAT (DataType.FLOAT, "float"),
  DOUBLE (DataType.DOUBLE, "double"),
  TIMESTAMP (DataType.TIMESTAMP, "timestamp");


  private final DataType dataType;
  private final String name;
  private final int size;

  /**
   * Private constructor used to pre-create the types
   * @param dataType DataType from the common's pb
   * @param name string representation of the type
   */
  private Type(DataType dataType, String name) {
    this.dataType = dataType;
    this.name = name;
    this.size = getTypeSize(this.dataType);
  }

  /**
   * Get the data type from the common's pb
   * @return A DataType
   */
  public DataType getDataType() {
    return this.dataType;
  }

  /**
   * Get the string representation of this type
   * @return The type's name
   */
  public String getName() {
    return this.name;
  }

  /**
   * The size of this type on the wire
   * @return A size
   */
  public int getSize() {
    return this.size;
  }

  @Override
  public String toString() {
    return "Type: " + this.name + ", size: " + this.size;
  }

  /**
   * Gives the size in bytes for a given DataType, as per the pb specification
   * @param type pb type
   * @return size in bytes
   */
  static int getTypeSize(DataType type) {
    switch (type) {
      case STRING:
      case BINARY: return 8 + 8; // offset then string length
      case BOOL:
      case INT8: return 1;
      case INT16: return Shorts.BYTES;
      case INT32:
      case FLOAT: return Ints.BYTES;
      case INT64:
      case DOUBLE:
      case TIMESTAMP: return Longs.BYTES;
      default: throw new IllegalArgumentException("The provided data type doesn't map" +
          " to know any known one.");
    }
  }

  /**
   * Convert the pb DataType to a Type
   * @param type DataType to convert
   * @return a matching Type
   */
  public static Type getTypeForDataType(DataType type) {
    switch (type) {
      case STRING: return STRING;
      case BINARY: return BINARY;
      case BOOL: return BOOL;
      case INT8: return INT8;
      case INT16: return INT16;
      case INT32: return INT32;
      case INT64: return INT64;
      case TIMESTAMP: return TIMESTAMP;
      case FLOAT: return FLOAT;
      case DOUBLE: return DOUBLE;
      default:
        throw new IllegalArgumentException("The provided data type doesn't map" +
            " to know any known one: " + type.getDescriptorForType().getFullName());

    }
  }

}
