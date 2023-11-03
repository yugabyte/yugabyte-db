// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.junit.Test;
import org.yb.Common;
import org.yb.Common.QLTypePB;
import org.yb.Value;
import org.yb.QLType;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestQLType extends BaseYBClientTest {

    private void checkType(QLType yqlType, Value.PersistentDataType dataType, QLType... params) {
        // Creating QLTypePB from DataType (no parameters for simple types).
        QLTypePB.Builder typeBuilder = QLTypePB.newBuilder();
        typeBuilder.setMain(dataType);
        typeBuilder.addAllParams(Arrays.stream(params)
                .map(ProtobufHelper::QLTypeToPb)
                .collect(Collectors.toList()));
        QLTypePB yqlTypePb = typeBuilder.build();

        // Check given yqlType serializes to expected protobuf.
        assertEquals(yqlTypePb, ProtobufHelper.QLTypeToPb(yqlType));
        // Check serialization/deseralization is idempotent.
        assertEquals(yqlTypePb, ProtobufHelper.QLTypeToPb(QLType.createFromQLTypePB(yqlTypePb)));
    }

    @Test
    public void testSimpleTypes() {
        checkType(QLType.INT8, Value.PersistentDataType.INT8);
        checkType(QLType.INT16, Value.PersistentDataType.INT16);
        checkType(QLType.INT32, Value.PersistentDataType.INT32);
        checkType(QLType.INT64, Value.PersistentDataType.INT64);
        checkType(QLType.STRING, Value.PersistentDataType.STRING);
        checkType(QLType.BOOL, Value.PersistentDataType.BOOL);
        checkType(QLType.FLOAT, Value.PersistentDataType.FLOAT);
        checkType(QLType.DOUBLE, Value.PersistentDataType.DOUBLE);
        checkType(QLType.BINARY, Value.PersistentDataType.BINARY);
        checkType(QLType.TIMESTAMP, Value.PersistentDataType.TIMESTAMP);
        checkType(QLType.DECIMAL, Value.PersistentDataType.DECIMAL);
        checkType(QLType.VARINT, Value.PersistentDataType.VARINT);
        checkType(QLType.INET, Value.PersistentDataType.INET);
        checkType(QLType.UUID, Value.PersistentDataType.UUID);
        checkType(QLType.TIMEUUID, Value.PersistentDataType.TIMEUUID);
    }

    @Test
    public void testUserDefinedTypes() {

        // Testing basic UDT.
        {
            QLTypePB.Builder typeBuilder = QLTypePB.newBuilder();
            typeBuilder.setMain(Value.PersistentDataType.USER_DEFINED_TYPE);
            QLTypePB.UDTypeInfo.Builder udtBuilder = QLTypePB.UDTypeInfo.newBuilder();
            udtBuilder.setKeyspaceName("foo");
            udtBuilder.setName("bar");
            typeBuilder.setUdtypeInfo(udtBuilder);
            QLTypePB yqlTypePb = typeBuilder.build();

            QLType yqlType = QLType.createUserDefinedType("foo", "bar");

            // Check given yqlType serializes to expected protobuf.
            assertEquals(yqlTypePb, ProtobufHelper.QLTypeToPb(yqlType));
            // Check serialization/deseralization is idempotent.
            assertEquals(yqlTypePb,
                    ProtobufHelper.QLTypeToPb(QLType.createFromQLTypePB(yqlTypePb)));
        }

        // Testing empty namespace.
        {
            QLTypePB.Builder typeBuilder = QLTypePB.newBuilder();
            typeBuilder.setMain(Value.PersistentDataType.USER_DEFINED_TYPE);
            QLTypePB.UDTypeInfo.Builder udtBuilder = QLTypePB.UDTypeInfo.newBuilder();
            udtBuilder.setKeyspaceName("");
            udtBuilder.setName("test");
            typeBuilder.setUdtypeInfo(udtBuilder);
            QLTypePB yqlTypePb = typeBuilder.build();

            QLType yqlType = QLType.createUserDefinedType("", "test");
            // Check given yqlType serializes to expected protobuf.
            assertEquals(yqlTypePb, ProtobufHelper.QLTypeToPb(yqlType));
            // Check serialization/deseralization is idempotent.
            assertEquals(yqlTypePb,
                    ProtobufHelper.QLTypeToPb(QLType.createFromQLTypePB(yqlTypePb)));
        }
    }

    @Test
    public void testParametricTypes() {
        // Sample list of yql types to test serialization/deserialization of parameters
        // Base values are tested separately above in testSimpleTypes.
        List<QLType> typeParams = new ArrayList<>(6);
        typeParams.add(QLType.STRING); // varchar
        // frozen<list<tinyint>>
        typeParams.add(QLType.createFrozenType(QLType.createListType(QLType.INT8)));
        // frozen<set<uuid>>
        typeParams.add(QLType.createFrozenType(QLType.createSetType(QLType.UUID)));
        // frozen<map<timeuuid, decimal>>
        typeParams.add(QLType.createFrozenType(QLType.createMapType(QLType.TIMEUUID,
                                                                      QLType.DECIMAL)));
        // frozen<list<frozen<set<uuid>>>>
        typeParams.add(QLType.createFrozenType(QLType.createListType(
                     QLType.createFrozenType(QLType.createSetType(QLType.UUID)))));
        // frozen<foo.bar> (user-defined type)
        typeParams.add(QLType.createFrozenType(
                QLType.createUserDefinedType("foo", "bar")));

        for (int i = 0; i < typeParams.size(); i++) {
            checkType(QLType.createSetType(typeParams.get(i)),
                      Value.PersistentDataType.SET, typeParams.get(i));

            checkType(QLType.createListType(typeParams.get(i)),
                      Value.PersistentDataType.LIST, typeParams.get(i));

            // Ensure map keys and values types are different
            int j = (i + 1) % typeParams.size();
            checkType(QLType.createMapType(typeParams.get(i), typeParams.get(j)),
                      Value.PersistentDataType.MAP, typeParams.get(i), typeParams.get(j));
        }
    }
}
