// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import io.ebean.config.dbplatform.DbEncrypt;
import io.ebean.config.dbplatform.DbEncryptFunction;
import io.ebean.config.dbplatform.ExtraDbTypes;
import java.sql.Types;

// An implementation of Postgresql DB encryption that supports JSON objects, since EBean's own
// implementation does not.
public class YbPgDbEncrypt implements DbEncrypt {

  private static class YbPgDbEncryptFunction implements DbEncryptFunction {
    @Override
    public String getDecryptSql(String columnWithTableAlias) {
      return "pgp_sym_decrypt(" + columnWithTableAlias + ",?)";
    }

    @Override
    public String getEncryptBindSql() {
      return "pgp_sym_encrypt(?::text,?)";
    }
  }

  protected DbEncryptFunction encryptFunction;

  public YbPgDbEncrypt() {
    this.encryptFunction = new YbPgDbEncryptFunction();
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
    return true;
  }
}
