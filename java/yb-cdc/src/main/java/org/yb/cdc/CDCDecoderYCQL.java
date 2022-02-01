// Copyright (c) YugaByte, Inc.
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

package org.yb.cdc;

import com.datastax.oss.protocol.internal.util.Bytes;
import org.apache.log4j.Logger;
import org.yb.Value;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.LocalDate;
import java.time.LocalTime;
import java.time.ZoneId;
import java.util.*;

public class CDCDecoderYCQL {
  private static final Logger LOG = Logger.getLogger(CDCDecoderYCQL.class);

  private static final byte[] HEX_ARRAY = "0123456789abcdef".getBytes(StandardCharsets.US_ASCII);

  public static String bytesToHex(byte[] bytes) {
    byte[] hexChars = new byte[bytes.length * 2];
    for (int j = 0; j < bytes.length; j++) {
      int v = bytes[j] & 0xFF;
      hexChars[j * 2] = HEX_ARRAY[v >>> 4];
      hexChars[j * 2 + 1] = HEX_ARRAY[v & 0x0F];
    }
    return new String(hexChars, StandardCharsets.UTF_8);
  }

  public static Object parseCDCValue(Value.QLValuePB recordValue) {
    ByteBuffer bytes;
    Object decodedObj = null;

      switch (recordValue.getValueCase()) {
        case INT64_VALUE: // bigint, counter
          decodedObj = recordValue.getInt64Value();
          break;

        case BOOL_VALUE: // boolean
          decodedObj = recordValue.getBoolValue();
          break;

        // picked this from cassandra-java-driver
        case DECIMAL_VALUE: // decimal
          bytes = ByteBuffer.wrap(recordValue.getDecimalValue().toByteArray());

          bytes = bytes.duplicate();
          int scale = bytes.getInt();
          byte[] bibytes = new byte[bytes.remaining()];
          bytes.get(bibytes);

          BigInteger bi = new BigInteger(bibytes);

          decodedObj = new BigDecimal(bi, scale);
          break;

        case DATE_VALUE: // date
          LocalDate EPOCH = LocalDate.of(1970, 1, 1);
          int signed = recordValue.getDateValue() + Integer.MIN_VALUE;
          LocalDate localDate = EPOCH.plusDays(signed);

          decodedObj =
              Date.from(localDate.atStartOfDay().atZone(ZoneId.systemDefault()).toInstant());
          break;

        case DOUBLE_VALUE: // float
          decodedObj = recordValue.getDoubleValue();
          break;

        // todo: map this using the codec from CQL driver
        case FROZEN_VALUE:
          decodedObj = "Frozen value";
          break;

        // picked this from cassandra-java-driver
        case INETADDRESS_VALUE: // inet
          bytes = ByteBuffer.wrap(recordValue.getInetaddressValue().toByteArray());
          try {
            decodedObj = InetAddress.getByAddress(Bytes.getArray(bytes)).getHostAddress();
          } catch (UnknownHostException e) {
            throw new IllegalArgumentException(
              "Invalid bytes for inet value, got " + bytes.remaining() + " bytes");
          }
          break;

        case INT32_VALUE: // int (integer)
          decodedObj = recordValue.getInt32Value();
          break;

        // inserting SET, LIST, MAP value is causing the CDC connection to break
        // and no record is streaming across the CDC Client
        case SET_VALUE:
          Value.QLSeqValuePB setValue = recordValue.getSetValue();
          int count = setValue.getElemsCount();
          for (int j = 0; j < count; ++j) {
            LOG.info("Set value " + (j + 1) + ": " +
                     setValue.getElems(j).getStringValue());
          }
          break;

        case LIST_VALUE:
          Value.QLSeqValuePB listValue = recordValue.getListValue();
          List<Value.QLValuePB> list = listValue.getElemsList();
          for (int i = 0; i < list.size(); ++i) {
            LOG.info(list.get(i).getStringValue());
          }
          break;

        case MAP_VALUE:
          Value.QLMapValuePB mapValue = recordValue.getMapValue();
          List<Value.QLValuePB> keyList = mapValue.getKeysList();
          for (int i = 0; i < keyList.size(); ++i) {
            LOG.info(keyList.get(i).getStringValue() +
                     " -- " + mapValue.getValues(i).getStringValue());
          }
          break;

        case INT16_VALUE: // smallint
          decodedObj = recordValue.getInt16Value();
          break;

        case STRING_VALUE: // text (varchar)
          decodedObj = recordValue.getStringValue();
          break;

        case TIME_VALUE:
          /*
          * getTimeValue() returns the current time since midnight in nano-seconds
          */
          long nanosOfDay = recordValue.getTimeValue();
          decodedObj = LocalTime.ofNanoOfDay(nanosOfDay);
          break;

        case TIMESTAMP_VALUE:
          /*
           * getTimestampValue() returns the number of microseconds for the particular timestamp
           * since epoch
           */
          long timestampMillis = recordValue.getTimestampValue() / 1000;
          Calendar calendar = Calendar.getInstance();
          calendar.setTimeInMillis(timestampMillis);
          decodedObj = calendar.getTime();
          break;

        // picked this from cassandra-java-driver
        case TIMEUUID_VALUE:
        case UUID_VALUE:
          bytes = ByteBuffer.wrap(recordValue.getUuidValue().toByteArray());
          if (bytes.remaining() == 0) {
            // skip this block
          } else if (bytes.remaining() != 16) {
            throw new IllegalArgumentException(
              "Unexpected number of bytes for a UUID, expected 16, got " + bytes.remaining());
          } else {
            decodedObj = new UUID(bytes.getLong(bytes.position()),
                                  bytes.getLong(bytes.position() + 8));
          }
          break;

        case INT8_VALUE: // tinyint
          decodedObj = recordValue.getInt8Value();
          break;

        // picked this from cassandra-java-driver
        case VARINT_VALUE:
          bytes = ByteBuffer.wrap(recordValue.getVarintValue().toByteArray());
          decodedObj = new BigInteger(Bytes.getArray(bytes));
          break;

        case JSONB_VALUE:
          decodedObj = recordValue.getJsonbValue().toStringUtf8();
          break;

        case BINARY_VALUE:
          // to handle BLOB values
          byte[] byteArr = recordValue.getBinaryValue().toByteArray();
          decodedObj = bytesToHex(byteArr);
          break;

        default:
          LOG.info("No matching type found");
    }
    return decodedObj;
  }
}
