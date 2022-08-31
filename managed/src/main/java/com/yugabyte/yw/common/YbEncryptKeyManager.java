// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import io.ebean.config.EncryptKey;
import io.ebean.config.EncryptKeyManager;

public class YbEncryptKeyManager implements EncryptKeyManager {

  @Override
  public EncryptKey getEncryptKey(String tableName, String columnName) {
    return () -> String.join("::", tableName, columnName);
  }
}
