// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Parse, encode, and redact ldapbindpasswd in ysql_hba style strings (doubled CSV, single quotes,
 * unquoted, JSON escapes).
 */
public final class LdapBindPasswdHbaFormat {

  private LdapBindPasswdHbaFormat() {}

  public enum QuoteStyle {
    DOUBLED_CSV,
    ESCAPED_DOUBLED_CSV,
    DEEP_ESCAPED_DOUBLED_CSV,
    SINGLE_CSV,
    UNQUOTED
  }

  private static final String LDAPBINDPASSWD_KEY = "ldapbindpasswd=";

  // HBA connection-type keywords that can appear after a comma and signal a new record boundary.
  private static final String HBA_RECORD_START_CONN_TYPE =
      "local|host(?:ssl|nossl|gssenc|nogssenc)?";

  // Unquoted value ends before the next ldap*= attr, a comma+name= pair, a new HBA record, or end.
  // Prefer quoted passwords for values that contain commas without a following '='.
  private static final Pattern UNQUOTED_VALUE =
      Pattern.compile(
          "ldapbindpasswd=(?!\")"
              + "((?:(?!\\s+ldap[a-zA-Z0-9_]*=).)+?)"
              + "(?=\\s+ldap[a-zA-Z0-9_]*=|,(?=\\s*[A-Za-z][A-Za-z0-9_]*=)|,(?=\\s*(?:"
              + HBA_RECORD_START_CONN_TYPE
              + ")\\s)|$)");

  private record ParsedValue(int endExclusive, String decoded, QuoteStyle style) {}

  /**
   * Redacts every ldapbindpasswd value, preserving the original quoting style and encoding
   * replacement accordingly.
   */
  public static String redactAllValues(String input, String replacement) {
    if (input == null || input.isEmpty()) {
      return input;
    }
    StringBuilder out = new StringBuilder();
    int last = 0;
    int idx;
    while ((idx = input.indexOf(LDAPBINDPASSWD_KEY, last)) >= 0) {
      out.append(input, last, idx);
      int eq = idx + LDAPBINDPASSWD_KEY.length();
      ParsedValue parsed = tryParseValue(input, eq);
      if (parsed == null) {
        out.append(LDAPBINDPASSWD_KEY);
        last = eq;
        continue;
      }
      out.append(LDAPBINDPASSWD_KEY);
      out.append(encode(parsed.style(), replacement));
      last = parsed.endExclusive();
    }
    out.append(input, last, input.length());
    return out.toString();
  }

  /** Encodes a decoded password for output using the given hba quoting style. */
  public static String encode(QuoteStyle style, String decodedPassword) {
    if (decodedPassword == null) {
      return "";
    }
    return switch (style) {
      case UNQUOTED -> decodedPassword;
      case ESCAPED_DOUBLED_CSV -> {
        String body = decodedPassword.replace("\"", "\\\"\\\"");
        yield "\\\"\\\"" + body + "\\\"\\\"";
      }
      case DEEP_ESCAPED_DOUBLED_CSV -> {
        String body = decodedPassword.replace("\"", "\\\\\"\\\\\"");
        yield "\\\\\"\\\\\"" + body + "\\\\\"\\\\\"";
      }
      case DOUBLED_CSV -> {
        String body = decodedPassword.replace("\"", "\"\"");
        yield "\"\"" + body + "\"\"";
      }
      case SINGLE_CSV -> "\"" + decodedPassword.replace("\"", "\"\"") + "\"";
    };
  }

  /** Returns true if the character(s) after a closing delimiter mark the end of the field. */
  private static boolean isClosingDelimiterFollowedByTerminator(String s, int indexAfterDelimiter) {
    int j = indexAfterDelimiter;
    while (j < s.length() && Character.isWhitespace(s.charAt(j))) {
      j++;
    }
    if (j >= s.length()) {
      return true;
    }
    if (s.charAt(j) == ',') {
      return true;
    }
    if (s.charAt(j) == '}' || s.charAt(j) == ']') {
      return true;
    }
    if (s.charAt(j) == '"') {
      if (j + 1 < s.length() && s.charAt(j + 1) == '"') {
        return false;
      }
      return true;
    }
    if (s.regionMatches(j, "ldap", 0, 4)) {
      int k = j + 4;
      while (k < s.length() && (Character.isLetterOrDigit(s.charAt(k)) || s.charAt(k) == '_')) {
        k++;
      }
      return k < s.length() && s.charAt(k) == '=';
    }
    return false;
  }

  /**
   * Scans the inner content of a doubled-CSV value (after opening ""). Returns start of closing "",
   * or -1.
   */
  private static int scanDoubledCsvInner(String s, int i, StringBuilder decodedOut) {
    while (i < s.length()) {
      if (i + 1 < s.length() && s.charAt(i) == '"' && s.charAt(i + 1) == '"') {
        if (isClosingDelimiterFollowedByTerminator(s, i + 2)) {
          return i;
        }
        decodedOut.append('"');
        i += 2;
      } else {
        decodedOut.append(s.charAt(i));
        i++;
      }
    }
    return -1;
  }

  /** Scans the inner content of a single-CSV value (after opening "). Treats "" as a literal ". */
  private static int scanSingleCsvInner(String s, int i, StringBuilder decodedOut) {
    while (i < s.length()) {
      if (s.charAt(i) == '"') {
        if (i + 1 < s.length() && s.charAt(i + 1) == '"') {
          decodedOut.append('"');
          i += 2;
        } else {
          return i + 1;
        }
      } else {
        decodedOut.append(s.charAt(i));
        i++;
      }
    }
    return i;
  }

  private static int scanEscapedWrapperInner(
      String s, int i, StringBuilder decodedOut, int wrapperLen) {
    while (i < s.length()) {
      if ((wrapperLen == 4 && isEscapedDoubledOpen(s, i))
          || (wrapperLen == 6 && isDeepEscapedDoubledOpen(s, i))) {
        if (isClosingDelimiterFollowedByTerminator(s, i + wrapperLen)) {
          return i;
        }
        decodedOut.append('"');
        i += wrapperLen;
      } else {
        decodedOut.append(s.charAt(i));
        i++;
      }
    }
    return -1;
  }

  private static boolean isEscapedDoubledOpen(String s, int i) {
    return i + 4 <= s.length()
        && s.charAt(i) == '\\'
        && s.charAt(i + 1) == '"'
        && s.charAt(i + 2) == '\\'
        && s.charAt(i + 3) == '"';
  }

  private static boolean isDeepEscapedDoubledOpen(String s, int i) {
    return i + 6 <= s.length()
        && s.charAt(i) == '\\'
        && s.charAt(i + 1) == '\\'
        && s.charAt(i + 2) == '"'
        && s.charAt(i + 3) == '\\'
        && s.charAt(i + 4) == '\\'
        && s.charAt(i + 5) == '"';
  }

  private static ParsedValue tryParseValue(String s, int eqIndex) {
    if (eqIndex > s.length()) {
      return null;
    }
    if (isDeepEscapedDoubledOpen(s, eqIndex)) {
      StringBuilder decoded = new StringBuilder();
      int closeStart = scanEscapedWrapperInner(s, eqIndex + 6, decoded, 6);
      if (closeStart >= 0 && isDeepEscapedDoubledOpen(s, closeStart)) {
        return new ParsedValue(
            closeStart + 6, decoded.toString(), QuoteStyle.DEEP_ESCAPED_DOUBLED_CSV);
      }
      return null;
    }
    if (isEscapedDoubledOpen(s, eqIndex)) {
      StringBuilder decoded = new StringBuilder();
      int closeStart = scanEscapedWrapperInner(s, eqIndex + 4, decoded, 4);
      if (closeStart >= 0 && isEscapedDoubledOpen(s, closeStart)) {
        return new ParsedValue(closeStart + 4, decoded.toString(), QuoteStyle.ESCAPED_DOUBLED_CSV);
      }
      return null;
    }
    if (eqIndex + 2 <= s.length() && s.startsWith("\"\"", eqIndex)) {
      StringBuilder decoded = new StringBuilder();
      int closeStart = scanDoubledCsvInner(s, eqIndex + 2, decoded);
      if (closeStart >= 0 && closeStart + 2 <= s.length() && s.startsWith("\"\"", closeStart)) {
        return new ParsedValue(closeStart + 2, decoded.toString(), QuoteStyle.DOUBLED_CSV);
      }
      return null;
    }
    if (eqIndex < s.length() && s.charAt(eqIndex) == '"') {
      StringBuilder decoded = new StringBuilder();
      int end = scanSingleCsvInner(s, eqIndex + 1, decoded);
      if (end > eqIndex + 1) {
        return new ParsedValue(end, decoded.toString(), QuoteStyle.SINGLE_CSV);
      }
      return null;
    }
    int keyStart = eqIndex - LDAPBINDPASSWD_KEY.length();
    if (keyStart < 0) {
      return null;
    }
    Matcher um = UNQUOTED_VALUE.matcher(s);
    um.region(keyStart, s.length());
    if (um.find()) {
      return new ParsedValue(um.end(), um.group(1), QuoteStyle.UNQUOTED);
    }
    return null;
  }
}
