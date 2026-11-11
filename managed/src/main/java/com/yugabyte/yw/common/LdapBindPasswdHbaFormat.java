// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Redacts ldapbindpasswd values in any text that holds ysql_hba_conf_csv records: the gflag value
 * itself, JSON that embeds it (API responses, audit entries, task params), or a file written from
 * it (server.conf, ysql_hba.conf).
 *
 * <p>Each of these quotes the same password differently, so a value is read by the quote that opens
 * it (see {@link QuoteStyle}), and the replacement is written back in that style. Text around the
 * value is kept as is, so redacted JSON still parses.
 *
 * <p>Some values have no end this parser can be sure of: one quoted only in part, such as {@code
 * "pa"ss}; an unquoted one followed by more text that isn't an option, such as {@code Two Word
 * Secret}; or one quoted in a way this parser doesn't read, such as an HBA line's {@code
 * \"secret\"} inside a JSON string. Such a value is redacted to the end of the CSV field or JSON
 * string that holds it.
 */
public final class LdapBindPasswdHbaFormat {

  private LdapBindPasswdHbaFormat() {}

  /** How a value is quoted, with the password secret as the example. */
  public enum QuoteStyle {
    /** A Postgres-quoted value inside a quoted CSV field, quotes doubled: {@code ""secret""}. */
    DOUBLED_CSV,
    /** DOUBLED_CSV inside a JSON string: {@code \"\"secret\"\"}. */
    ESCAPED_DOUBLED_CSV,
    /** DOUBLED_CSV with two backslashes before each quote: {@code \\"\\"secret\\"\\"}. */
    DEEP_ESCAPED_DOUBLED_CSV,
    /**
     * A Postgres-quoted value as written in an HBA line, as in ysql_hba.conf: {@code "secret"}. In
     * ysql_hba_conf_csv it is always inside a quoted CSV field, as DOUBLED_CSV.
     */
    SINGLE_CSV,
    /** No quotes: {@code secret}. */
    UNQUOTED
  }

  private static final String LDAPBINDPASSWD_KEY = "ldapbindpasswd=";

  // The connection type that starts an HBA record.
  private static final Pattern HBA_RECORD_TYPE =
      Pattern.compile("(?<![\\w.-])(?:local|host(?:ssl|nossl|gssenc|nogssenc)?)[ \\t]");

  // How far back closingQuoteEscapes looks for the record start, so a payload with many values
  // stays linear.
  private static final int RECORD_SEARCH_LIMIT = 4096;

  private record ParsedValue(int endExclusive, String decoded, QuoteStyle style) {}

  private record Redaction(String text, boolean anyEndUnknown) {}

  /**
   * Replaces every ldapbindpasswd value in {@code input} with {@code replacement}, quoted the way
   * the value was. An unquoted empty value is left alone.
   */
  public static String redactAllValues(String input, String replacement) {
    if (input == null || input.isEmpty()) {
      return input;
    }
    return redact(input, replacement).text();
  }

  /**
   * True if some ldapbindpasswd value in {@code input} has no end redactAllValues can be sure of
   * (see the class comment).
   */
  public static boolean hasValueWithUnknownEnd(String input) {
    return input != null && redact(input, "").anyEndUnknown();
  }

  private static Redaction redact(String input, String replacement) {
    StringBuilder out = new StringBuilder();
    // Set for a value with no sure end (see the class comment). checkLdapBindPasswdQuoting in
    // GFlagsUtil refuses new values like this, but a refused request is still logged, and a
    // universe may have stored one before that check existed.
    boolean anyEndUnknown = false;
    int last = 0;
    int idx;
    while ((idx = input.indexOf(LDAPBINDPASSWD_KEY, last)) >= 0) {
      out.append(input, last, idx);
      int eq = idx + LDAPBINDPASSWD_KEY.length();
      out.append(LDAPBINDPASSWD_KEY);
      ParsedValue parsed = tryParseValue(input, eq);
      if (parsed != null
          && endsValue(input, parsed.endExclusive())
          && (parsed.style() != QuoteStyle.UNQUOTED
              || unquotedValueEndsThere(input, parsed.endExclusive()))) {
        out.append(encode(parsed.style(), replacement));
        last = parsed.endExclusive();
      } else if (endsValue(input, eq)) {
        last = eq;
      } else {
        out.append(replacement);
        last = enclosingFieldEnd(input, eq);
        anyEndUnknown = true;
      }
    }
    out.append(input, last, input.length());
    return new Redaction(out.toString(), anyEndUnknown);
  }

  /**
   * True if an unquoted value that stops at s[i] ends there. Postgres ends one at any whitespace or
   * comma, but unless the next option, record, comment or the end of the field follows, the text
   * after it is most likely more of the password, typed without the quotes it needs.
   */
  private static boolean unquotedValueEndsThere(String s, int i) {
    if (i >= s.length()) {
      return true;
    }
    boolean comma = s.charAt(i) == ',';
    if (!comma && s.charAt(i) != ' ' && s.charAt(i) != '\t') {
      return true;
    }
    int j = i + 1;
    while (j < s.length() && (s.charAt(j) == ' ' || s.charAt(j) == '\t')) {
      j++;
    }
    if (j >= s.length()
        || s.charAt(j) == '\n'
        || s.charAt(j) == '\r'
        || s.charAt(j) == '#'
        || isJsonEscapedWhitespace(s, j)) {
      return true;
    }
    if (!comma && s.charAt(j) == ',') {
      return unquotedValueEndsThere(s, j);
    }
    if (quoteLengthAt(s, j) > 0) {
      // After a comma, a quote opens the next CSV field; after whitespace, it must close this one.
      return comma || endsValue(s, j);
    }
    return isOptionAssignment(s, j)
        || (comma && HBA_RECORD_TYPE.matcher(s).region(j, s.length()).lookingAt());
  }

  /**
   * True if a value can end just before s[i]: at the end of the input, whitespace, a comma, a JSON
   * delimiter, or a quote that closes the CSV field or JSON string. A doubled quote is a quote
   * character inside the value, so a value can't end there.
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
   * Where to stop redacting a value with no known end. Anything up to the end of its field may be
   * password: stop at a line break, or at the first quote (at any escape depth) that isn't doubled
   * and closes the CSV field or JSON string holding the record.
   */
  private static int enclosingFieldEnd(String s, int i) {
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
   * True if s[i] ends the CSV field or JSON string around a value whose quotes each carry {@code
   * escapes} backslashes (0 in raw text). That is a line break, a quote escaped less than the
   * value's, or a quote escaped the same and followed by a field end. A fully quoted value closes
   * before any of these, so a scan that reaches one is reading a value quoted only in part. It must
   * stop there, or it would take a later password's quote as its closing quote.
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

  /**
   * True if the quote at s[i] is repeated right after it, CSV's way of writing a quote character.
   */
  private static boolean isDoubledQuote(String s, int i, int quoteLength) {
    return s.startsWith(s.substring(i, i + quoteLength), i + quoteLength);
  }

  /** True if s[i], just after a quote, shows that the quote closed a CSV field or JSON string. */
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

  /** True if s[i] starts a JSON-escaped line break or tab. */
  private static boolean isJsonEscapedWhitespace(String s, int i) {
    return s.charAt(i) == '\\'
        && i + 1 < s.length()
        && (s.charAt(i + 1) == 'n' || s.charAt(i + 1) == 'r' || s.charAt(i + 1) == 't');
  }

  /**
   * Writes {@code decodedPassword} in the given quoting style. redactAllValues uses it only for the
   * replacement.
   */
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

  /**
   * True if what follows a possible closing quote shows the value ended there: the end, a comma, a
   * closing brace or bracket, a quote that isn't doubled, or whitespace and the next option's
   * name=. Anything else means the quote is part of the password.
   */
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
   * Reads a DOUBLED_CSV value from just after its opening "" into {@code decodedOut}. Returns the
   * index of the closing "", or -1 if the field ends first.
   */
  private static int scanDoubledCsvInner(String s, int i, StringBuilder decodedOut) {
    while (i < s.length()) {
      // """" is one quote character: Postgres's "" doubled again for CSV. Its second half can't
      // be the closing quote.
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

  /**
   * Reads a SINGLE_CSV value from just after its opening quote into {@code decodedOut}, taking ""
   * as a quote character. Returns the index after the closing quote, or the input length.
   */
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

  /**
   * scanDoubledCsvInner for the escaped styles: {@code wrapperLen} is 4 for ESCAPED_DOUBLED_CSV's
   * \"\" and 6 for DEEP_ESCAPED_DOUBLED_CSV's \\"\\".
   */
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

  /**
   * Reads the value that starts at s[eqIndex], right after ldapbindpasswd=, by the quote that opens
   * it. Returns null for an unquoted empty value, or one that doesn't close in its style.
   */
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
      // The value opens with a quote escaped more deeply than the styles above, as in JSON inside
      // a JSON string. Redact to the end of that string.
      end = enclosingJsonStringEnd(s, eqIndex);
    }
    if (end > eqIndex) {
      return new ParsedValue(end, s.substring(eqIndex, end), QuoteStyle.UNQUOTED);
    }
    return null;
  }

  /**
   * End of an unquoted value. Postgres ends one at whitespace or a comma, so an unquoted password
   * can't contain either. A quote, raw or backslash-escaped, ends it too. Usually that quote closes
   * the CSV field or JSON string holding the record. Otherwise the value is quoted only in part,
   * and redactAllValues redacts it to the end of its field.
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
   * How many of the {@code backslashes} right before the quote that ends the unquoted value at
   * s[valueStart] escape that quote; the rest belong to the password. ldapbindpasswd=abc\" is the
   * password abc\ in a raw CSV field, but abc inside a JSON string, where the quote is escaped. The
   * quote that opens the record (a CSV-quoted record, or the JSON string it starts) is escaped like
   * the closing one, so its backslashes decide. With no quote there, an even count is all password
   * and an odd count leaves one for the quote. This takes the quote to close the record; when it
   * doesn't, as with an HBA line's \"secret\" inside a JSON string, the value has no end
   * redactAllValues can find.
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
