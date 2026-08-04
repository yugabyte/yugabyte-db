package com.yugabyte.yw.common.audit.otel;

import javax.inject.Singleton;

@Singleton
public class AuditLogRegexGenerator extends BaseLogRegexGenerator {

  public LogRegexResult generateAuditLogRegex(String logPrefix, boolean onlyPrefix) {
    return parseLogPrefix(logPrefix, onlyPrefix);
  }

  @Override
  protected String getLogSuffix() {
    // See https://github.com/pgaudit/pgaudit/#format for placeholders description
    return "(?P<log_level>\\w+):  AUDIT:"
        + " (?P<audit_type>\\w+),(?P<statement_id>\\d+),(?P<substatement_id>\\d+),"
        + "(?P<class>\\w+),(?P<command>[^,]+),(?P<object_type>[^,]*),(?P<object_name>[^,]*),"
        + "(?P<statement>(.|\\n|\\r|\\s)*)";
  }
}
