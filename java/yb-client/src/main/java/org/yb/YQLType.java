// Copyright (c) YugaByte, Inc.

package org.yb;

import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;

import static org.yb.Common.DataType;

/**
 * Describes all the YQLTypes available to build table schemas.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving

public class YQLType {

  private final DataType main;
  private final List<YQLType> params;

  // These fields are only used for user-defined types.
  private final String udtypeName;
  private final String udtypeKeyspaceName;

  //------------------------------------------------------------------------------------------------
  // Private constructors, called by static methods below.

  // Constructor for primitive types
  private YQLType(DataType main, List<YQLType> params) {
    this.main = main;
    this.params = params;

    // For user-defined types the other constructor should be called
    assert main != DataType.USER_DEFINED_TYPE;
    udtypeName = null;
    udtypeKeyspaceName = null;
  }

  // Constructor for user-defined types
  private YQLType(String udtypeKeyspaceName, String udtypeName) {
    this.main = DataType.USER_DEFINED_TYPE;
    this.params = new ArrayList<>();
    this.udtypeKeyspaceName = udtypeKeyspaceName;
    this.udtypeName = udtypeName;
  }

  // Utility constructor for primitive types without parameters
  private YQLType(DataType main) {
    this(main, new ArrayList<>());
  }

  //------------------------------------------------------------------------------------------------
  // Getter methods.

  public DataType getMain() {
    return this.main;
  }

  public List<YQLType> getParams() {
    return this.params;
  }

  public boolean isUserDefined() {
    return main == DataType.USER_DEFINED_TYPE;
  }

  // Expects this is a user-defined type.
  public String getUdtName() {
    return this.udtypeName;
  }

  // Expects this is a user-defined type.
  public String getUdtKeyspaceName() {
    return this.udtypeKeyspaceName;
  }

  //------------------------------------------------------------------------------------------------
  // Utilities for producing the CQL/YQL representation for this type.

  /**
   * @return the Cql string representation of this type:
   *  1. Simple types: e.g. "int", "varchar", "timestamp".
   *  2  Parametric types: e.g. "list<int>", "map<int, varchar>".
   *  3. User-defined types: e.g. "test_keyspace.employee" or "employee".
   */
  public String toCqlString() {
    if (isUserDefined()) {
      return udtypeKeyspaceName.isEmpty() ? udtypeName : udtypeKeyspaceName + "." + udtypeName;
    }

    String name = getDataTypeName(main);
    name += params.stream().map(YQLType::toCqlString).collect(Collectors.joining(",", "<", ">"));
    return name;
  }

  private static String getDataTypeName(DataType type) {
    switch (type) {
      case INT8: return "int8";
      case INT16: return "int16";
      case INT32: return "int32";
      case INT64: return "int64";
      case STRING: return "string";
      case BOOL: return "bool";
      case FLOAT: return "float";
      case DOUBLE: return "double";
      case BINARY: return "binary";
      case TIMESTAMP: return "timestamp";
      case DECIMAL: return "decimal";
      case VARINT: return "varint";
      case INET: return "inet";
      case LIST: return "list";
      case MAP: return "map";
      case SET: return "set";
      case UUID: return "uuid";
      case TIMEUUID: return "timeuuid";
      case FROZEN: return "frozen";
      case USER_DEFINED_TYPE: return "user_defined_type";

      default:
        throw new IllegalArgumentException("The provided datatype doesn't map" +
                " to know any known one: " + type.getDescriptorForType().getFullName());
    }
  }

  //------------------------------------------------------------------------------------------------
  // Utilities for converting to/from Type. TODO: To be removed once YugaWare starts using YQLType

  @Deprecated
  public Type toType() {
    return Type.getTypeForDataType(main);
  }

  @Deprecated
  public static YQLType fromType(Type type) {
    return new YQLType(type.getDataType(), new ArrayList<>());
  }

  //------------------------------------------------------------------------------------------------
  // Static utilities for construct YQLType instances.

  /**
   * Constructs a YQLType instance from a protobuf message.
   * @param yqlType the protobuf serialization of a YQL Type (typically produced by YB Client).
   * @return the YQLType instance.
   */
  public static YQLType createFromYQLTypePB(Common.YQLTypePB yqlType) {
    if (yqlType.getMain() == DataType.USER_DEFINED_TYPE) {
      return new YQLType(yqlType.getUdtypeInfo().getKeyspaceName(),
                         yqlType.getUdtypeInfo().getName());
    }

    return new YQLType(yqlType.getMain(),
                       yqlType.getParamsList().stream()
                                              .map(YQLType::createFromYQLTypePB)
                                              .collect(Collectors.toList()));
  }

  /**
   * Constructs a YQLType instance of type Set (e.g. "set<int>").
   * The type of the elements should be a valid primary-key type (simple type or frozen).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param elemsType the YQLType for the elements of this Set.
   * @return the YQLType instance.
   */
  public static YQLType createSetType(YQLType elemsType) {
    ArrayList<YQLType> params = new ArrayList<>(1);
    params.add(elemsType);
    return new YQLType(DataType.SET, params);
  }

  /**
   * Constructs a YQLType instance of type Map (e.g. "map<int, varchar>").
   * The types of the keys and values should be valid primary-key types (simple type or frozen).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param keysType the YQLType for the keys of this Map.
   * @param valuesType the YQLType for the values of this Map.
   * @return the YQLType instance.
   */
  public static YQLType createMapType(YQLType keysType, YQLType valuesType) {
    ArrayList<YQLType> params = new ArrayList<>(2);
    params.add(keysType);
    params.add(valuesType);
    return new YQLType(DataType.MAP, params);
  }

  /**
   * Constructs a YQLType instance of type List (e.g. "list<varchar>").
   * The type of the elements should be a valid primary-key type (simple type or frozen).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param elemsType the YQLType for the elements of this List.
   * @return the YQLType instance.
   */
  public static YQLType createListType(YQLType elemsType) {
    ArrayList<YQLType> params = new ArrayList<>(1);
    params.add(elemsType);
    return new YQLType(DataType.LIST, params);
  }

  /**
   * Constructs a YQLType instance of type Frozen (e.g. "frozen<list<varchar>>").
   * The type of the elements should be a valid collection type (set, map or list).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param argType the YQLType to be frozen (i.e. stored in a serialized form)
   * @return the YQLType instance.
   */
  public static YQLType createFrozenType(YQLType argType) {
    ArrayList<YQLType> params = new ArrayList<>(1);
    params.add(argType);
    return new YQLType(DataType.FROZEN, params);
  }

  /**
   * Constructs a YQLType instance of a user-defined type (e.g. "test_keyspace.employee").
   * The referenced user-defined type should have already been created e.g. with a command like
   *   "CREATE TYPE test_keyspace.employee(first_name varchar, last_name varchar, ...);"
   * or (like for tables)
   *   "USE test_keyspace; CREATE TYPE employee(first_name varchar, last_name varchar, ...);"
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param keyspace_name the keyspace in which the user-defined type was declared.
   * @param type_name the name of the user-defined type.
   * @return the YQLType instance.
   */
  public static YQLType createUserDefinedType(String keyspace_name, String type_name) {
    return new YQLType(keyspace_name, type_name);
  }

  // convenience fields for simple YQLTypes
  public static YQLType INT8 = new YQLType(DataType.INT8);
  public static YQLType INT16 = new YQLType(DataType.INT16);
  public static YQLType INT32 = new YQLType(DataType.INT32);
  public static YQLType INT64 = new YQLType(DataType.INT64);
  public static YQLType STRING = new YQLType(DataType.STRING);
  public static YQLType BOOL = new YQLType(DataType.BOOL);
  public static YQLType FLOAT = new YQLType(DataType.FLOAT);
  public static YQLType DOUBLE = new YQLType(DataType.DOUBLE);
  public static YQLType BINARY = new YQLType(DataType.BINARY);
  public static YQLType TIMESTAMP = new YQLType(DataType.TIMESTAMP);
  public static YQLType DECIMAL = new YQLType(DataType.DECIMAL);
  public static YQLType VARINT = new YQLType(DataType.VARINT);
  public static YQLType INET = new YQLType(DataType.INET);
  public static YQLType UUID = new YQLType(DataType.UUID);
  public static YQLType TIMEUUID = new YQLType(DataType.TIMEUUID);

}
