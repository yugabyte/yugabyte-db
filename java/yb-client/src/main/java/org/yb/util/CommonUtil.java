// Copyright (c) YugabyteDB, Inc.

package org.yb.util;

import com.google.protobuf.ByteString;
import java.nio.ByteBuffer;
import java.util.Objects;
import java.util.UUID;
import javax.annotation.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class CommonUtil {
  public static final Logger LOG = LoggerFactory.getLogger(CommonUtil.class);

  // ByteString is expected to be 16 bytes long, representing a UUID.
  public static UUID convertToUUID(@Nullable ByteString byteString) {
    if (Objects.isNull(byteString) || byteString.isEmpty()) {
      return null;
    }
    if (byteString.size() != 16) {
      // TODO: Add validation for the length of the ByteString after making sure it does not
      // cause regressions.
      LOG.warn("Expected ByteString of length 16 for UUID conversion, but got length: {}",
          byteString.size());
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
