// Copyright (c) YugaByte, Inc.

package org.yb.util;

import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import java.nio.ByteBuffer;
import java.util.Objects;
import java.util.UUID;
import javax.annotation.Nullable;
import org.yb.CommonNet.HostPortPB;

public class CommonUtil {

  public static UUID convertToUUID(@Nullable ByteString byteString) {
    if (Objects.isNull(byteString) || byteString.isEmpty()) {
      return null;
    }
    byte[] bytes = byteString.toByteArray();
    ByteBuffer bb = ByteBuffer.wrap(bytes);
    return new UUID(bb.getLong(), bb.getLong());
  }

  public static ByteString convertToByteString(UUID uuid) {
    ByteBuffer bb = ByteBuffer.wrap(new byte[16]);
    bb.putLong(uuid.getMostSignificantBits());
    bb.putLong(uuid.getLeastSignificantBits());
    return ByteString.copyFrom(bb.array());
  }

}
