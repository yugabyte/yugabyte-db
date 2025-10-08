package com.yugabyte.yw.common.audit.otel;

import javax.inject.Singleton;

@Singleton
public class QueryLogRegexGenerator extends BaseLogRegexGenerator {

  public LogRegexResult generateQueryLogRegex(String logPrefix, boolean onlyPrefix) {
    return parseLogPrefix(logPrefix, onlyPrefix);
  }

  @Override
  protected String getLogSuffix() {
    return "(?P<log_level>\\w+):[ ](?P<message>(.|\\n|\\r|\\s)*)";
  }
}
