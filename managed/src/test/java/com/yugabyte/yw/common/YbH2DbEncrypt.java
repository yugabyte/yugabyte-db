// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import io.ebean.config.dbplatform.DbEncrypt;
import io.ebean.config.dbplatform.DbEncryptFunction;
import io.ebean.config.dbplatform.ExtraDbTypes;
import java.sql.Types;

// An implementation of H2 DB encryption that supports JSON objects, since EBean's own
// implementation does not.
public class YbH2DbEncrypt implements DbEncrypt {

  private static class YbH2DbEncryptFunction implements DbEncryptFunction {
    @Override
    public String getDecryptSql(String columnWithTableAlias) {
      return "TRIM(CHAR(0) FROM UTF8TOSTRING(DECRYPT('AES', STRINGTOUTF8(?), "
          + columnWithTableAlias
          + ")))";
    }

    @Override
    public String getEncryptBindSql() {
      return "ENCRYPT('AES', STRINGTOUTF8(?), STRINGTOUTF8(?))";
    }
  }

  protected DbEncryptFunction encryptFunction;

  public YbH2DbEncrypt() {
    this.encryptFunction = new YbH2DbEncryptFunction();
  }

  @Override
  public DbEncryptFunction getDbEncryptFunction(int jdbcType) {
    switch (jdbcType) {
      case Types.CHAR:
      case Types.CLOB:
      case Types.VARCHAR:
      case Types.LONGVARCHAR:
      case ExtraDbTypes.JSON:
        return encryptFunction;
      default:
        return null;
    }
  }

  @Override
  public int getEncryptDbType() {
    return Types.VARBINARY;
  }

  @Override
  public boolean isBindEncryptDataFirst() {
    return false;
  }
}
