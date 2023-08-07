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

import com.google.common.primitives.Ints;
import com.google.common.primitives.Longs;
import com.google.common.primitives.Shorts;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import static org.yb.Value.PersistentDataType;

/**
 * Describes all the types available to build table schemas.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public enum Type {

  INT8 (PersistentDataType.INT8, "int8"),
  INT16 (PersistentDataType.INT16, "int16"),
  INT32 (PersistentDataType.INT32, "int32"),
  INT64 (PersistentDataType.INT64, "int64"),
  STRING (PersistentDataType.STRING, "string"),
  BOOL (PersistentDataType.BOOL, "bool"),
  FLOAT (PersistentDataType.FLOAT, "float"),
  DOUBLE (PersistentDataType.DOUBLE, "double"),
  BINARY (PersistentDataType.BINARY, "binary"),
  TIMESTAMP (PersistentDataType.TIMESTAMP, "timestamp"),
  DECIMAL (PersistentDataType.DECIMAL, "decimal"),
  VARINT (PersistentDataType.VARINT, "varint"),
  INET (PersistentDataType.INET, "inet"),
  LIST (PersistentDataType.LIST, "list"),
  MAP (PersistentDataType.MAP, "map"),
  SET (PersistentDataType.SET, "set"),
  UUID (PersistentDataType.UUID, "uuid"),
  TIMEUUID (PersistentDataType.TIMEUUID, "timeuuid"),
  FROZEN (PersistentDataType.FROZEN, "frozen"),
  DATE (PersistentDataType.DATE, "date"),
  TIME (PersistentDataType.TIME, "time"),
  JSONB (PersistentDataType.JSONB, "jsonb"),
  USER_DEFINED_TYPE (PersistentDataType.USER_DEFINED_TYPE, "user_defined_type"),
  NULL_VALUE_TYPE (PersistentDataType.NULL_VALUE_TYPE, "null_value_type"),
  GIN_NULL (PersistentDataType.GIN_NULL, "gin_null"),
  TYPEARGS (PersistentDataType.TYPEARGS, "typeargs"),
  TUPLE (PersistentDataType.TUPLE, "tuple");

  private final PersistentDataType dataType;
  private final String name;
  private final int size;

  /**
   * Private constructor used to pre-create the types
   * @param dataType DataType from the common's pb
   * @param name string representation of the type
   */
  private Type(PersistentDataType dataType, String name) {
    this.dataType = dataType;
    this.name = name;
    this.size = getTypeSize(this.dataType);
  }

  /**
   * Get the data type from the common's pb
   * @return A DataType
   */
  public PersistentDataType getDataType() {
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
  static int getTypeSize(PersistentDataType type) {
    switch (type) {
      case DECIMAL:
      case VARINT:
      case INET:
      case LIST:
      case MAP:
      case SET:
      case UUID:
      case TIMEUUID:
      case FROZEN:
      case USER_DEFINED_TYPE:
      case JSONB:
      // TODO(mihnea) handle the cases above properly after cleaning up inherited code
      case STRING:
      case BINARY:
      case TYPEARGS:
      case TUPLE:
        return 8 + 8; // offset then string length
      case BOOL:
      case INT8: return 1;
      case INT16: return Shorts.BYTES;
      case INT32:
      case FLOAT:
      case DATE:
      case GIN_NULL: return Ints.BYTES;
      case INT64:
      case DOUBLE:
      case TIMESTAMP:
      case TIME: return Longs.BYTES;
      case NULL_VALUE_TYPE: return 0;
      default: throw new IllegalArgumentException("The provided data type doesn't map" +
          " to know any known one.");
    }
  }

  /**
   * Convert the pb DataType to a Type
   * @param type DataType to convert
   * @return a matching Type
   */
  public static Type getTypeForDataType(PersistentDataType type) {
    switch (type) {
      case INT8:
      case UINT8: return INT8;
      case INT16:
      case UINT16: return INT16;
      case INT32:
      case UINT32: return INT32;
      case INT64:
      case UINT64: return INT64;
      case STRING: return STRING;
      case BOOL: return BOOL;
      case FLOAT: return FLOAT;
      case DOUBLE: return DOUBLE;
      case BINARY: return BINARY;
      case TIMESTAMP: return TIMESTAMP;
      case DECIMAL: return DECIMAL;
      case VARINT: return VARINT;
      case INET: return INET;
      case LIST: return LIST;
      case MAP: return MAP;
      case SET: return SET;
      case UUID: return UUID;
      case TIMEUUID: return TIMEUUID;
      case FROZEN: return FROZEN;
      case USER_DEFINED_TYPE: return USER_DEFINED_TYPE;
      case DATE: return DATE;
      case TIME: return TIME;
      case JSONB: return JSONB;
      case NULL_VALUE_TYPE: return NULL_VALUE_TYPE;
      case GIN_NULL: return GIN_NULL;
      case TYPEARGS: return TYPEARGS;
      case TUPLE: return TUPLE;

      default:
        throw new IllegalArgumentException("The provided data type doesn't map" +
            " to know any known one: " + type.getDescriptorForType().getFullName());

    }
  }

}
