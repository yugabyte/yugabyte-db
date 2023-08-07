// Copyright (c) YugaByte, Inc.

package org.yb;

import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;

import static org.yb.Value.PersistentDataType;

/**
 * Describes all the QLTypes available to build table schemas.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving

public class QLType {

  private final PersistentDataType main;
  private final List<QLType> params;

  // These fields are only used for user-defined types.
  private final String udtypeName;
  private final String udtypeKeyspaceName;

  //------------------------------------------------------------------------------------------------
  // Private constructors, called by static methods below.

  // Constructor for primitive types
  private QLType(PersistentDataType main, List<QLType> params) {
    this.main = main;
    this.params = params;

    // For user-defined types the other constructor should be called
    assert main != PersistentDataType.USER_DEFINED_TYPE;
    udtypeName = null;
    udtypeKeyspaceName = null;
  }

  // Constructor for user-defined types
  private QLType(String udtypeKeyspaceName, String udtypeName) {
    this.main = PersistentDataType.USER_DEFINED_TYPE;
    this.params = new ArrayList<>();
    this.udtypeKeyspaceName = udtypeKeyspaceName;
    this.udtypeName = udtypeName;
  }

  // Utility constructor for primitive types without parameters
  private QLType(PersistentDataType main) {
    this(main, new ArrayList<>());
  }

  //------------------------------------------------------------------------------------------------
  // Getter methods.

  public PersistentDataType getMain() {
    return this.main;
  }

  public List<QLType> getParams() {
    return this.params;
  }

  public boolean isUserDefined() {
    return main == PersistentDataType.USER_DEFINED_TYPE;
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
  // Utilities for producing the CQL/QL representation for this type.

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
    name += params.stream().map(QLType::toCqlString).collect(Collectors.joining(",", "<", ">"));
    return name;
  }

  private static String getDataTypeName(PersistentDataType type) {
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
  // Utilities for converting to/from Type. TODO: To be removed once YugaWare starts using QLType

  @Deprecated
  public Type toType() {
    return Type.getTypeForDataType(main);
  }

  @Deprecated
  public static QLType fromType(Type type) {
    return new QLType(type.getDataType(), new ArrayList<>());
  }

  //------------------------------------------------------------------------------------------------
  // Static utilities for construct QLType instances.

  /**
   * Constructs a QLType instance from a protobuf message.
   * @param yqlType the protobuf serialization of a QL Type (typically produced by YB Client).
   * @return the QLType instance.
   */
  public static QLType createFromQLTypePB(Common.QLTypePB yqlType) {
    if (yqlType.getMain() == PersistentDataType.USER_DEFINED_TYPE) {
      return new QLType(yqlType.getUdtypeInfo().getKeyspaceName(),
                         yqlType.getUdtypeInfo().getName());
    }

    return new QLType(yqlType.getMain(),
                       yqlType.getParamsList().stream()
                                              .map(QLType::createFromQLTypePB)
                                              .collect(Collectors.toList()));
  }

  /**
   * Constructs a QLType instance of type Set (e.g. "set<int>").
   * The type of the elements should be a valid primary-key type (simple type or frozen).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param elemsType the QLType for the elements of this Set.
   * @return the QLType instance.
   */
  public static QLType createSetType(QLType elemsType) {
    ArrayList<QLType> params = new ArrayList<>(1);
    params.add(elemsType);
    return new QLType(PersistentDataType.SET, params);
  }

  /**
   * Constructs a QLType instance of type Map (e.g. "map<int, varchar>").
   * The types of the keys and values should be valid primary-key types (simple type or frozen).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param keysType the QLType for the keys of this Map.
   * @param valuesType the QLType for the values of this Map.
   * @return the QLType instance.
   */
  public static QLType createMapType(QLType keysType, QLType valuesType) {
    ArrayList<QLType> params = new ArrayList<>(2);
    params.add(keysType);
    params.add(valuesType);
    return new QLType(PersistentDataType.MAP, params);
  }

  /**
   * Constructs a QLType instance of type List (e.g. "list<varchar>").
   * The type of the elements should be a valid primary-key type (simple type or frozen).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param elemsType the QLType for the elements of this List.
   * @return the QLType instance.
   */
  public static QLType createListType(QLType elemsType) {
    ArrayList<QLType> params = new ArrayList<>(1);
    params.add(elemsType);
    return new QLType(PersistentDataType.LIST, params);
  }

  /**
   * Constructs a QLType instance of type Frozen (e.g. "frozen<list<varchar>>").
   * The type of the elements should be a valid collection type (set, map or list).
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param argType the QLType to be frozen (i.e. stored in a serialized form)
   * @return the QLType instance.
   */
  public static QLType createFrozenType(QLType argType) {
    ArrayList<QLType> params = new ArrayList<>(1);
    params.add(argType);
    return new QLType(PersistentDataType.FROZEN, params);
  }

  /**
   * Constructs a QLType instance of a user-defined type (e.g. "test_keyspace.employee").
   * The referenced user-defined type should have already been created e.g. with a command like
   *   "CREATE TYPE test_keyspace.employee(first_name varchar, last_name varchar, ...);"
   * or (like for tables)
   *   "USE test_keyspace; CREATE TYPE employee(first_name varchar, last_name varchar, ...);"
   * TODO the constraints are not checked here, instead we rely on YB Client to ensure validity.
   * @param keyspace_name the keyspace in which the user-defined type was declared.
   * @param type_name the name of the user-defined type.
   * @return the QLType instance.
   */
  public static QLType createUserDefinedType(String keyspace_name, String type_name) {
    return new QLType(keyspace_name, type_name);
  }

  // convenience fields for simple QLTypes
  public static QLType INT8 = new QLType(PersistentDataType.INT8);
  public static QLType INT16 = new QLType(PersistentDataType.INT16);
  public static QLType INT32 = new QLType(PersistentDataType.INT32);
  public static QLType INT64 = new QLType(PersistentDataType.INT64);
  public static QLType STRING = new QLType(PersistentDataType.STRING);
  public static QLType BOOL = new QLType(PersistentDataType.BOOL);
  public static QLType FLOAT = new QLType(PersistentDataType.FLOAT);
  public static QLType DOUBLE = new QLType(PersistentDataType.DOUBLE);
  public static QLType BINARY = new QLType(PersistentDataType.BINARY);
  public static QLType TIMESTAMP = new QLType(PersistentDataType.TIMESTAMP);
  public static QLType DECIMAL = new QLType(PersistentDataType.DECIMAL);
  public static QLType VARINT = new QLType(PersistentDataType.VARINT);
  public static QLType INET = new QLType(PersistentDataType.INET);
  public static QLType UUID = new QLType(PersistentDataType.UUID);
  public static QLType TIMEUUID = new QLType(PersistentDataType.TIMEUUID);

}
