// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.junit.Test;
import org.yb.Common;
import org.yb.Common.YQLTypePB;
import org.yb.YQLType;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

import static org.junit.Assert.assertEquals;

public class TestYQLType extends BaseYBClientTest {

    private void checkType(YQLType yqlType, Common.DataType dataType, YQLType... params) {
        // Creating YQLTypePB from DataType (no parameters for simple types).
        YQLTypePB.Builder typeBuilder = YQLTypePB.newBuilder();
        typeBuilder.setMain(dataType);
        typeBuilder.addAllParams(Arrays.stream(params)
                .map(ProtobufHelper::YQLTypeToPb)
                .collect(Collectors.toList()));
        YQLTypePB yqlTypePb = typeBuilder.build();

        // Check given yqlType serializes to expected protobuf.
        assertEquals(yqlTypePb, ProtobufHelper.YQLTypeToPb(yqlType));
        // Check serialization/deseralization is idempotent.
        assertEquals(yqlTypePb, ProtobufHelper.YQLTypeToPb(YQLType.createFromYQLTypePB(yqlTypePb)));
    }

    @Test
    public void testSimpleTypes() {
        checkType(YQLType.INT8, Common.DataType.INT8);
        checkType(YQLType.INT16, Common.DataType.INT16);
        checkType(YQLType.INT32, Common.DataType.INT32);
        checkType(YQLType.INT64, Common.DataType.INT64);
        checkType(YQLType.STRING, Common.DataType.STRING);
        checkType(YQLType.BOOL, Common.DataType.BOOL);
        checkType(YQLType.FLOAT, Common.DataType.FLOAT);
        checkType(YQLType.DOUBLE, Common.DataType.DOUBLE);
        checkType(YQLType.BINARY, Common.DataType.BINARY);
        checkType(YQLType.TIMESTAMP, Common.DataType.TIMESTAMP);
        checkType(YQLType.DECIMAL, Common.DataType.DECIMAL);
        checkType(YQLType.VARINT, Common.DataType.VARINT);
        checkType(YQLType.INET, Common.DataType.INET);
        checkType(YQLType.UUID, Common.DataType.UUID);
        checkType(YQLType.TIMEUUID, Common.DataType.TIMEUUID);
    }

    @Test
    public void testUserDefinedTypes() {

        // Testing basic UDT.
        {
            YQLTypePB.Builder typeBuilder = YQLTypePB.newBuilder();
            typeBuilder.setMain(Common.DataType.USER_DEFINED_TYPE);
            YQLTypePB.UDTypeInfo.Builder udtBuilder = YQLTypePB.UDTypeInfo.newBuilder();
            udtBuilder.setKeyspaceName("foo");
            udtBuilder.setName("bar");
            typeBuilder.setUdtypeInfo(udtBuilder);
            YQLTypePB yqlTypePb = typeBuilder.build();

            YQLType yqlType = YQLType.createUserDefinedType("foo", "bar");

            // Check given yqlType serializes to expected protobuf.
            assertEquals(yqlTypePb, ProtobufHelper.YQLTypeToPb(yqlType));
            // Check serialization/deseralization is idempotent.
            assertEquals(yqlTypePb,
                    ProtobufHelper.YQLTypeToPb(YQLType.createFromYQLTypePB(yqlTypePb)));
        }

        // Testing empty namespace.
        {
            YQLTypePB.Builder typeBuilder = YQLTypePB.newBuilder();
            typeBuilder.setMain(Common.DataType.USER_DEFINED_TYPE);
            YQLTypePB.UDTypeInfo.Builder udtBuilder = YQLTypePB.UDTypeInfo.newBuilder();
            udtBuilder.setKeyspaceName("");
            udtBuilder.setName("test");
            typeBuilder.setUdtypeInfo(udtBuilder);
            YQLTypePB yqlTypePb = typeBuilder.build();

            YQLType yqlType = YQLType.createUserDefinedType("", "test");
            // Check given yqlType serializes to expected protobuf.
            assertEquals(yqlTypePb, ProtobufHelper.YQLTypeToPb(yqlType));
            // Check serialization/deseralization is idempotent.
            assertEquals(yqlTypePb,
                    ProtobufHelper.YQLTypeToPb(YQLType.createFromYQLTypePB(yqlTypePb)));
        }
    }

    @Test
    public void testParametricTypes() {
        // Sample list of yql types to test serialization/deserialization of parameters
        // Base values are tested separately above in testSimpleTypes.
        List<YQLType> typeParams = new ArrayList<>(6);
        typeParams.add(YQLType.STRING); // varchar
        // frozen<list<tinyint>>
        typeParams.add(YQLType.createFrozenType(YQLType.createListType(YQLType.INT8)));
        // frozen<set<uuid>>
        typeParams.add(YQLType.createFrozenType(YQLType.createSetType(YQLType.UUID)));
        // frozen<map<timeuuid, decimal>>
        typeParams.add(YQLType.createFrozenType(YQLType.createMapType(YQLType.TIMEUUID,
                                                                      YQLType.DECIMAL)));
        // frozen<list<frozen<set<uuid>>>>
        typeParams.add(YQLType.createFrozenType(YQLType.createListType(
                     YQLType.createFrozenType(YQLType.createSetType(YQLType.UUID)))));
        // frozen<foo.bar> (user-defined type)
        typeParams.add(YQLType.createFrozenType(
                YQLType.createUserDefinedType("foo", "bar")));

        for (int i = 0; i < typeParams.size(); i++) {
            checkType(YQLType.createSetType(typeParams.get(i)),
                      Common.DataType.SET, typeParams.get(i));

            checkType(YQLType.createListType(typeParams.get(i)),
                      Common.DataType.LIST, typeParams.get(i));

            // Ensure map keys and values types are different
            int j = (i + 1) % typeParams.size();
            checkType(YQLType.createMapType(typeParams.get(i), typeParams.get(j)),
                      Common.DataType.MAP, typeParams.get(i), typeParams.get(j));
        }
    }
}
