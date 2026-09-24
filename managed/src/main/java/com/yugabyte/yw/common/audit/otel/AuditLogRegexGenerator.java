package com.yugabyte.yw.common.audit.otel;

import javax.inject.Singleton;

@Singleton
public class AuditLogRegexGenerator extends BaseLogRegexGenerator {

  // log_error_verbosity=VERBOSE (a global GUC, so query logging turns it on for pgaudit too) makes
  // Postgres insert the 5-char SQLSTATE before the message: "LOG:  00000: AUDIT: ...". Matched
  // optionally so audit records are still classified as audit and not as query logs.
  // Backslash-free, so it embeds verbatim in both regex_parser patterns and expr filter strings.
  public static final String AUDIT_MARKER_REGEX = ":  (?:[0-9A-Z]{5}: )?AUDIT:";

  public LogRegexResult generateAuditLogRegex(String logPrefix, boolean onlyPrefix) {
    return parseLogPrefix(logPrefix, onlyPrefix);
  }

  @Override
  protected String getLogSuffix() {
    // See https://github.com/pgaudit/pgaudit/#format for placeholders description
    return "(?P<log_level>\\w+)"
        + AUDIT_MARKER_REGEX
        + " (?P<audit_type>\\w+),(?P<statement_id>\\d+),(?P<substatement_id>\\d+),"
        + "(?P<class>\\w+),(?P<command>[^,]+),(?P<object_type>[^,]*),(?P<object_name>[^,]*),"
        + "(?P<statement>(.|\\n|\\r|\\s)*)";
  }
}
