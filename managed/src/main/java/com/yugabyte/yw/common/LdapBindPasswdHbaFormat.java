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

  private static final Pattern HBA_RECORD_TYPE =
      Pattern.compile("(?<![\\w.-])(?:local|host(?:ssl|nossl|gssenc|nogssenc)?)[ \\t]");

  // How far back closingQuoteEscapes looks for the record start, so a payload with many values
  // stays linear.
  private static final int RECORD_SEARCH_LIMIT = 4096;

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
      out.append(LDAPBINDPASSWD_KEY);
      ParsedValue parsed = tryParseValue(input, eq);
      if (parsed != null && endsValue(input, parsed.endExclusive())) {
        out.append(encode(parsed.style(), replacement));
        last = parsed.endExclusive();
      } else if (endsValue(input, eq)) {
        last = eq;
      } else {
        // A value quoted only in part. GFlagsUtil.checkLdapBindPasswdQuoting refuses one, but the
        // refused request is still logged, and a universe may store one from before that check.
        out.append(replacement);
        last = partlyQuotedValueEnd(input, eq);
      }
    }
    out.append(input, last, input.length());
    return out.toString();
  }

  /**
   * True if a value ending at s[i] ends there: at the end, whitespace, a comma, a JSON delimiter,
   * or a quote that closes the CSV field or JSON string. A doubled quote is part of the value.
   */
  private static boolean endsValue(String s, int i) {
    if (i >= s.length()) {
      return true;
    }
    char c = s.charAt(i);
    if (Character.isWhitespace(c)
        || c == ','
        || c == '}'
        || c == ']'
        || isJsonEscapedWhitespace(s, i)) {
      return true;
    }
    int quoteLength = quoteLengthAt(s, i);
    if (quoteLength == 0 || isDoubledQuote(s, i, quoteLength)) {
      return false;
    }
    int after = i + quoteLength;
    return after >= s.length()
        || Character.isWhitespace(s.charAt(after))
        || isFieldEndAfterQuote(s, after);
  }

  /**
   * End of a value quoted only in part: the first quote, at any escape depth, that is not doubled
   * and closes the CSV field or JSON string holding the record, or a line break. Postgres can't
   * parse such a value, so everything up to the end of its field may be password.
   */
  private static int partlyQuotedValueEnd(String s, int i) {
    while (i < s.length()) {
      char c = s.charAt(i);
      if (c == '\n' || c == '\r') {
        return i;
      }
      int quoteLength = quoteLengthAt(s, i);
      if (quoteLength == 0) {
        i++;
      } else if (isDoubledQuote(s, i, quoteLength)) {
        i += 2 * quoteLength;
      } else if (isFieldEndAfterQuote(s, i + quoteLength)) {
        return i;
      } else {
        i += quoteLength;
      }
    }
    return s.length();
  }

  /**
   * True if a line break, or a quote that the value's own scan didn't take as doubled, ends the CSV
   * field or JSON string holding a value quoted with {@code escapes} backslashes per quote: a quote
   * escaped less, or one escaped the same and followed by a field end. A quoted value closes before
   * either, so a scan reaching one is reading a value quoted only in part, and must not go on into
   * the next field for a closing quote. Backslashes escape nothing in raw text ({@code escapes} 0).
   */
  private static boolean endsEnclosingField(String s, int i, int escapes) {
    char c = s.charAt(i);
    if (c == '\n' || c == '\r') {
      return true;
    }
    if (c != '"') {
      return false;
    }
    int backslashes = 0;
    while (escapes > 0
        && backslashes <= escapes
        && backslashes < i
        && s.charAt(i - 1 - backslashes) == '\\') {
      backslashes++;
    }
    return backslashes < escapes || (backslashes == escapes && isFieldEndAfterQuote(s, i + 1));
  }

  /** Length of the quote at s[i], with the backslashes escaping it, or 0 if none starts there. */
  private static int quoteLengthAt(String s, int i) {
    int j = i;
    while (j < s.length() && s.charAt(j) == '\\') {
      j++;
    }
    return j < s.length() && s.charAt(j) == '"' ? j - i + 1 : 0;
  }

  private static boolean isDoubledQuote(String s, int i, int quoteLength) {
    return s.startsWith(s.substring(i, i + quoteLength), i + quoteLength);
  }

  private static boolean isFieldEndAfterQuote(String s, int i) {
    if (i >= s.length()) {
      return true;
    }
    char c = s.charAt(i);
    return c == ','
        || c == '}'
        || c == ']'
        || c == '"'
        || c == ':'
        || c == '\n'
        || c == '\r'
        || isJsonEscapedWhitespace(s, i);
  }

  private static boolean isJsonEscapedWhitespace(String s, int i) {
    return s.charAt(i) == '\\'
        && i + 1 < s.length()
        && (s.charAt(i + 1) == 'n' || s.charAt(i + 1) == 'r' || s.charAt(i + 1) == 't');
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
    if (s.startsWith("\\\"", j)) {
      return !s.startsWith("\\\"\\\"", j);
    }
    // Postgres separates options with whitespace, so a quote followed by whitespace and an option
    // name closes the value.
    return j > indexAfterDelimiter && isOptionAssignment(s, j);
  }

  /** True if s[i..] is an HBA option name, such as ldapserver or clientcert, followed by '='. */
  private static boolean isOptionAssignment(String s, int i) {
    int k = i;
    while (k < s.length() && ((s.charAt(k) >= 'a' && s.charAt(k) <= 'z') || s.charAt(k) == '_')) {
      k++;
    }
    return k > i && k < s.length() && s.charAt(k) == '=';
  }

  /**
   * Scans the inner content of a doubled-CSV value (after opening ""). Returns start of closing "",
   * or -1.
   */
  private static int scanDoubledCsvInner(String s, int i, StringBuilder decodedOut) {
    while (i < s.length()) {
      // A literal quote, "" in Postgres doubled again for CSV; its second half can't close.
      if (s.startsWith("\"\"\"\"", i)) {
        decodedOut.append("\"\"");
        i += 4;
      } else if (i + 1 < s.length() && s.charAt(i) == '"' && s.charAt(i + 1) == '"') {
        if (isClosingDelimiterFollowedByTerminator(s, i + 2)) {
          return i;
        }
        decodedOut.append('"');
        i += 2;
      } else if (endsEnclosingField(s, i, 0)) {
        return -1;
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
      if ((wrapperLen == 4 && isEscapedDoubledOpen(s, i) && isEscapedDoubledOpen(s, i + 4))
          || (wrapperLen == 6
              && isDeepEscapedDoubledOpen(s, i)
              && isDeepEscapedDoubledOpen(s, i + 6))) {
        decodedOut.append("\"\"");
        i += 2 * wrapperLen;
      } else if ((wrapperLen == 4 && isEscapedDoubledOpen(s, i))
          || (wrapperLen == 6 && isDeepEscapedDoubledOpen(s, i))) {
        if (isClosingDelimiterFollowedByTerminator(s, i + wrapperLen)) {
          return i;
        }
        decodedOut.append('"');
        i += wrapperLen;
      } else if (endsEnclosingField(s, i, wrapperLen / 2 - 1)) {
        return -1;
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
    int end = unquotedValueEnd(s, eqIndex);
    if (end == eqIndex && end < s.length() && s.charAt(end) == '\\') {
      // A quote escaped deeper than the forms above opens the value.
      end = enclosingJsonStringEnd(s, eqIndex);
    }
    if (end > eqIndex) {
      return new ParsedValue(end, s.substring(eqIndex, end), QuoteStyle.UNQUOTED);
    }
    return null;
  }

  /**
   * Postgres ends an unquoted option value at whitespace or a comma, and an unquoted password with
   * either is not valid HBA. A quote, raw or backslash-escaped, closes the CSV field or JSON string
   * holding the record. A value quoted only in part continues past that quote; redactAllValues
   * finds that it doesn't end there and redacts it to the end of its field.
   */
  private static int unquotedValueEnd(String s, int i) {
    int start = i;
    while (i < s.length()) {
      char c = s.charAt(i);
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == '"') {
        return i;
      }
      if (c == '\\') {
        int j = i;
        while (j < s.length() && s.charAt(j) == '\\') {
          j++;
        }
        if (j < s.length() && s.charAt(j) == '"') {
          return j - Math.min(j - i, closingQuoteEscapes(s, start, j - i));
        }
        i = j;
        continue;
      }
      i++;
    }
    return i;
  }

  /**
   * Backslashes escaping the quote that closes the unquoted value at s[valueStart]. A raw abc\"
   * reads the same as abc before a quote escaped once, so take the escapes of the quote just before
   * the record: a CSV-quoted record's, or that of the JSON string the record starts. With no quote
   * there, an even count is all password and an odd count leaves one for the quote.
   */
  private static int closingQuoteEscapes(String s, int valueStart, int backslashes) {
    Matcher record = HBA_RECORD_TYPE.matcher(s).useTransparentBounds(true);
    for (int i = valueStart - 1; i >= Math.max(0, valueStart - RECORD_SEARCH_LIMIT); i--) {
      char c = s.charAt(i);
      if ((c != 'l' && c != 'h') || !record.region(i, valueStart).lookingAt()) {
        continue;
      }
      if (i == 0 || s.charAt(i - 1) != '"') {
        break;
      }
      int k = i - 1;
      while (k > 0 && s.charAt(k - 1) == '\\') {
        k--;
      }
      return i - 1 - k;
    }
    return backslashes % 2;
  }

  /** Index of the unescaped quote or line break ending the JSON string that s[i] is in. */
  private static int enclosingJsonStringEnd(String s, int i) {
    while (i < s.length()) {
      char c = s.charAt(i);
      if (c == '"' || c == '\r' || c == '\n') {
        return i;
      }
      i += c == '\\' ? 2 : 1;
    }
    return s.length();
  }
}
