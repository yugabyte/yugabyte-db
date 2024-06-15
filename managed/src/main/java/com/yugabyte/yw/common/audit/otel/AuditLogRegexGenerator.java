package com.yugabyte.yw.common.audit.otel;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.Getter;
import lombok.Value;
import org.apache.commons.lang3.StringUtils;

@Singleton
public class AuditLogRegexGenerator {

  Map<String, LogPrefixTokens> tokensMap =
      Arrays.stream(LogPrefixTokens.values())
          .collect(Collectors.toMap(LogPrefixTokens::getPlaceholder, Function.identity()));

  public LogRegexResult generateAuditLogRegex(String logPrefix, boolean onlyPrefix) {
    String result = "";
    List<LogPrefixTokens> namedTokens = new ArrayList<>();
    boolean lastPercent = false;
    List<LogPrefixTokens> collectedTokens = new ArrayList<>();
    for (char current : logPrefix.toCharArray()) {
      // %% is just translated to single % so we just ignore previous character.
      if (lastPercent && current != '%') {
        LogPrefixTokens token = tokensMap.get("%" + current);
        if (token != null) {
          // Valid token. But NON_SESSION_PROCESSES_STOP we just ignore as it translates to nothing.
          if (token != LogPrefixTokens.NON_SESSION_PROCESSES_STOP) {
            collectedTokens.add(token);
          }
        } else {
          // Append all collected tokens + % + character itself.
          result += outputTokens(collectedTokens, namedTokens, '%') + "%[" + current + "]";
        }
      } else {
        if (current == '%' && !lastPercent) {
          lastPercent = true;
          continue;
        }
        // Regular character. Output all collected tokens + character itself.
        result += outputTokens(collectedTokens, namedTokens, current) + "[" + current + "]";
      }
      lastPercent = false;
    }
    if (lastPercent) {
      // In case prefix just ends with single %
      result += "%";
    }
    if (!collectedTokens.isEmpty()) {
      result += outputTokens(collectedTokens, namedTokens, null);
    }
    if (onlyPrefix) {
      return new LogRegexResult(result, namedTokens);
    }
    // See https://github.com/pgaudit/pgaudit/#format for placeholders description
    result +=
        "(?P<log_level>\\w+):  AUDIT:"
            + " (?P<audit_type>\\w+),(?P<statement_id>\\d+),(?P<substatement_id>\\d+),"
            + "(?P<class>\\w+),(?P<command>[^,]+),(?P<object_type>[^,]*),(?P<object_name>[^,]*),"
            + "(?P<statement>(.|\\n|\\r|\\s)*)";
    return new LogRegexResult(result, namedTokens);
  }

  private String outputTokens(
      List<LogPrefixTokens> tokens, List<LogPrefixTokens> namedTokens, Character separator) {
    if (tokens.isEmpty()) {
      return StringUtils.EMPTY;
    }
    // In case no separator is present after the last token
    // - we will have to search for any characters till 'LOG:  AUDIT:' comes.
    String noSeparatorRegex = separator != null ? "[^" + separator + "]+" : ".+";
    LogPrefixTokens singleToken = tokens.size() == 1 ? tokens.get(0) : null;
    tokens.clear();
    if (singleToken != null) {
      // In case we have one token and a separator - we can try to parse attribute out of it.
      String regex =
          singleToken.getRegexPlaceholder() != null
              ? singleToken.getRegexPlaceholder()
              : noSeparatorRegex;
      namedTokens.add(singleToken);
      return "(?P<" + singleToken.getAttributeName() + ">" + regex + ")";
    } else {
      // In case we have multiple tokens one by one - we will not even
      // try to parse attributes from these - as in most cases it's impossible.
      return "(" + noSeparatorRegex + ")";
    }
  }

  @Value
  public static class LogRegexResult {
    String regex;
    List<LogPrefixTokens> tokens;
  }

  @Getter
  public enum LogPrefixTokens {
    // Standard Postgresql
    APPLICATION_NAME("%a"),
    USER_NAME("%u"),
    DATABASE_NAME("%d"),
    REMOTE_HOST_PORT("%r"),
    REMOTE_HOST("%h"),
    PROCESS_ID("%p", "\\d+"),
    GROUP_LEADER_PROCESS_ID("%P", "\\d+"),
    TIMESTAMP_WITHOUT_MS("%t", "\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2} \\w{3}"),
    TIMESTAMP_WITH_MS("%m", "\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}[.]\\d{3} \\w{3}"),
    TIMESTAMP_WITH_MS_EPOCH("%n", "\\d{10}[.]\\d{3}"),
    COMMAND_TAG("%i", "\\w+"),
    ERROR_CODE("%e", "\\d+"),
    SESSION_ID("%c"),
    LOG_LINE_NUMBER("%l", "\\d+"),
    PROCESS_START_TIMESTAMP("%s", "\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2} \\w{3}"),
    VIRTUAL_TRANSACTION_ID("%v"),
    TRANSACTION_ID("%x"),
    NON_SESSION_PROCESSES_STOP("%q", ""),
    QUERY_ID("%Q"),

    // Additional YugaByte DB
    CLOUD_NAME("%C"),
    REGION("%R"),
    AVAILABILITY_ZONE("%Z"),
    CLUSTER_UUID("%U"),
    NODE_CLUSTER_NAME("%N"),
    CURRENT_HOSTNAME("%H");
    private final String placeholder;
    private final String regexPlaceholder;

    LogPrefixTokens(String placeholder, String regexPlaceholder) {
      this.placeholder = placeholder;
      this.regexPlaceholder = regexPlaceholder;
    }

    LogPrefixTokens(String placeholder) {
      this(placeholder, null);
    }

    public String getAttributeName() {
      return name().toLowerCase();
    }
  }
}
