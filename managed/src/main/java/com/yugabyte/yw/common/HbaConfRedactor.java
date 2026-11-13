// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.common.CsvFieldSplitter.CsvField;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Deque;
import java.util.List;
import java.util.Set;

/**
 * Redacts ldapbindpasswd in ysql_hba_conf_csv by reading the value the way the DB does, rather than
 * guessing where the password ends from the characters around it.
 *
 * <p>The value is CSV, and each field becomes one line of ysql_hba.conf. {@link CsvFieldSplitter}
 * splits fields by Commons CSV's default rules: {@code GFlagsUtil.mergeCSVs} re-encodes the value
 * with them before it reaches server.conf, and they split every value the DB's own reader ({@code
 * ReadCSVValues}) accepts the same way. Lines are joined at a trailing backslash and tokenized as
 * {@code tokenize_auth_file} and {@code next_token} in hba.c do. The value of each ldapbindpasswd
 * option is replaced, and every other character is kept.
 *
 * <p>Where the password may extend past what Postgres reads as its value (a later token that isn't
 * a known option), the line is redacted from there to its end, along with the lines after it that
 * don't start a rule. A comment on a line with a password is redacted too. Postgres rejects or
 * ignores these, so nothing it uses is hidden. ldapbindpasswd text that isn't an option name is
 * also redacted to the end of the line, even inside a value Postgres uses: it may be a password
 * quoted into another option, as in "ldapsearchfilter=(x) ldapbindpasswd=secret".
 */
public final class HbaConfRedactor {

  private HbaConfRedactor() {}

  private static final String KEY = "ldapbindpasswd";
  private static final String OPTION_PREFIX = KEY + "=";
  private static final String REPLACEMENT = RedactingService.SECRET_REPLACEMENT;
  private static final String GFLAG_LINE_PREFIX = "--" + GFlagsUtil.YSQL_HBA_CONF_CSV + "=";

  /** Connection types parse_hba_line in hba.c accepts. */
  private static final Set<String> CONNECTION_TYPES =
      Set.of("local", "host", "hostssl", "hostnossl", "hostgssenc", "hostnogssenc");

  /** Option names parse_hba_auth_opt in hba.c accepts. */
  static final Set<String> HBA_OPTION_NAMES =
      Set.of(
          "map",
          "clientcert",
          "clientname",
          "pamservice",
          "pam_use_hostname",
          "ldapurl",
          "ldaptls",
          "ldapscheme",
          "ldapserver",
          "ldapport",
          "ldapbinddn",
          "ldapbindpasswd",
          "ldapsearchattribute",
          "ldapsearchfilter",
          "ldapbasedn",
          "ldapprefix",
          "ldapsuffix",
          "krb_realm",
          "include_realm",
          "compat_realm",
          "upn_username",
          "radiusservers",
          "radiusports",
          "radiussecrets",
          "radiusidentifiers",
          "jwt_jwks_path",
          "jwt_jwks_url",
          "jwt_audiences",
          "jwt_issuers",
          "jwt_matching_claim_key");

  /** An hba.conf line, with the field and field offset each character came from. */
  record Line(String text, int[] field, int[] offset) {}

  /** A token as next_token returns it, with the line position of each character of its text. */
  record Token(int start, int end, String text, int[] pos, boolean[] inQuotes) {}

  /** The tokens of a line, and where its comment starts (-1 if none). */
  record Tokens(List<Token> tokens, int commentStart) {}

  private record Edit(int start, int end, String replacement) {}

  /** Redacts every ldapbindpasswd value in a ysql_hba_conf_csv value. */
  public static String redact(String value) {
    // Matched loosely: lines are not joined yet, and a continuation may split the name.
    if (value == null || indexOfKey(value, 0, true) < 0) {
      return value;
    }
    List<CsvField> fields =
        value.indexOf('\n') < 0 && value.indexOf('\r') < 0 ? CsvFieldSplitter.split(value) : null;
    if (fields == null) {
      // The DB rejects the whole value, and there are no fields to stop at.
      return redactToEnd(value, true);
    }
    List<List<Edit>> fieldEdits = new ArrayList<>();
    fields.forEach(f -> fieldEdits.add(new ArrayList<>()));
    // After a line redacted to its end, lines that don't start a rule may be the rest of the
    // password, split off by an unquoted comma. Postgres rejects or ignores them.
    boolean afterRedactedEnd = false;
    for (Line line : joinLines(fields)) {
      String text = line.text();
      List<Edit> edits;
      if (indexOfKey(text, 0, false) >= 0) {
        edits = lineEdits(text);
        afterRedactedEnd = !edits.isEmpty() && edits.get(edits.size() - 1).end() == text.length();
      } else if (afterRedactedEnd && !startsRule(text)) {
        edits =
            text.chars().allMatch(c -> isBlank((char) c))
                ? List.of()
                : List.of(new Edit(0, text.length(), REPLACEMENT));
      } else {
        afterRedactedEnd = false;
        continue;
      }
      for (Edit edit : edits) {
        addFieldEdits(line, edit, fieldEdits);
      }
    }
    return splice(value, fields, fieldEdits);
  }

  /** Whether text may hold an ldapbindpasswd, in any case and however quoted or escaped. */
  public static boolean mentionsLdapBindPasswd(String text) {
    return text != null && indexOfKey(text, 0, true) >= 0;
  }

  /**
   * Redacts ldapbindpasswd in text with no structure to rely on, such as logs and command output:
   * from the name to the end of the line, and the lines after it that are indented with more
   * spaces, because Helm folds long YAML values onto those. A line that doesn't start a YAML or
   * JSON entry may itself be folded from the nearest line above it with less indent, and the lines
   * after it at its own indent continue the same value, so those are redacted too. A {@code
   * --ysql_hba_conf_csv=} gflag line is redacted like the gflag value, and folds the same way when
   * that redaction reaches the end of the line.
   */
  public static String redactFreeText(String text) {
    if (text == null || indexOfKey(text, 0, true) < 0) {
      return text;
    }
    StringBuilder out = new StringBuilder(text.length());
    // Indents of earlier lines, each less than the one pushed after it.
    Deque<Integer> indents = new ArrayDeque<>();
    int foldIndent = -1;
    int p = 0;
    while (p < text.length()) {
      int next = text.indexOf('\n', p);
      next = next < 0 ? text.length() : next + 1;
      int end = next;
      while (end > p && (text.charAt(end - 1) == '\n' || text.charAt(end - 1) == '\r')) {
        end--;
      }
      int indent = 0;
      while (p + indent < end && text.charAt(p + indent) == ' ') {
        indent++;
      }
      boolean blank = p + indent == end;
      int parentIndent = -1;
      if (!blank) {
        while (!indents.isEmpty() && indents.peek() >= indent) {
          indents.pop();
        }
        parentIndent = indents.isEmpty() ? -1 : indents.peek();
        indents.push(indent);
      }
      if (foldIndent >= 0 && indent > foldIndent && !blank) {
        out.append(text, p, p + indent).append(REPLACEMENT);
      } else {
        foldIndent = -1;
        String line = text.substring(p, end);
        int k = 0;
        while (k < line.length() && isBlank(line.charAt(k))) {
          k++;
        }
        if (line.startsWith(GFLAG_LINE_PREFIX, k)) {
          int valueStart = k + GFLAG_LINE_PREFIX.length();
          String value = line.substring(valueStart);
          String redacted = redact(value);
          if (!redacted.equals(value) && endsRedacted(redacted)) {
            foldIndent = indent;
          }
          out.append(line, 0, valueStart).append(redacted);
        } else {
          String redacted = redactToEnd(line, true);
          if (!redacted.equals(line)) {
            foldIndent = parentIndent >= 0 && !startsEntry(line, k) ? parentIndent : indent;
          }
          out.append(redacted);
        }
      }
      out.append(text, end, next);
      p = next;
    }
    return out.toString();
  }

  /** Whether s ends with REDACTED, past any closing quotes. */
  private static boolean endsRedacted(String s) {
    int end = s.length();
    while (end > 0 && s.charAt(end - 1) == '"') {
      end--;
    }
    return s.startsWith(REPLACEMENT, end - REPLACEMENT.length());
  }

  /** Whether line starts at k with a YAML or JSON key and its ':', or a YAML sequence item. */
  private static boolean startsEntry(String line, int k) {
    int n = line.length();
    if (k < n && line.charAt(k) == '-' && (k + 1 == n || isBlank(line.charAt(k + 1)))) {
      return true;
    }
    boolean quoted = k < n && line.charAt(k) == '"';
    int j = k;
    if (quoted) {
      j = line.indexOf('"', k + 1) + 1;
      if (j == 0) {
        return false;
      }
    } else {
      while (j < n && isKeyChar(line.charAt(j))) {
        j++;
      }
      if (j == k) {
        return false;
      }
    }
    while (j < n && isBlank(line.charAt(j))) {
      j++;
    }
    // A plain YAML key needs a blank after its ':', so that "host:port" isn't one.
    return j < n && line.charAt(j) == ':' && (quoted || j + 1 == n || isBlank(line.charAt(j + 1)));
  }

  private static boolean isKeyChar(char c) {
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9')
        || c == '_'
        || c == '-'
        || c == '.';
  }

  /** Joins fields into lines the way tokenize_auth_file joins lines ending in a backslash. */
  static List<Line> joinLines(List<CsvField> fields) {
    List<Line> lines = new ArrayList<>();
    int total = fields.stream().mapToInt(f -> f.text().length()).sum();
    StringBuilder text = new StringBuilder();
    int[] field = new int[total];
    int[] offset = new int[total];
    int lastBackslash = 0;
    for (int k = 0; k < fields.size(); k++) {
      String t = fields.get(k).text();
      for (int j = 0; j < t.length(); j++) {
        field[text.length()] = k;
        offset[text.length()] = j;
        text.append(t.charAt(j));
      }
      // The last field would join the lines the DB generates, which leaves it invalid; its
      // backslash stays in the line so that a value ending in one is redacted whole.
      int len = text.length();
      if (k + 1 < fields.size() && len > lastBackslash && text.charAt(len - 1) == '\\') {
        text.setLength(len - 1);
        lastBackslash = len - 1;
        continue;
      }
      lines.add(
          new Line(
              text.toString(),
              Arrays.copyOf(field, text.length()),
              Arrays.copyOf(offset, text.length())));
      text.setLength(0);
      lastBackslash = 0;
    }
    return lines;
  }

  /** Tokenizes one line as next_token in hba.c does. */
  static Tokens tokenize(String line) {
    List<Token> tokens = new ArrayList<>();
    int n = line.length();
    int[] pos = new int[n];
    boolean[] inQuotes = new boolean[n];
    StringBuilder text = new StringBuilder();
    int i = 0;
    while (i < n) {
      while (i < n && (isBlank(line.charAt(i)) || line.charAt(i) == ',')) {
        i++;
      }
      int start = i;
      text.setLength(0);
      boolean inQuote = false;
      boolean wasQuote = false;
      boolean sawQuote = false;
      while (i < n && (!isBlank(line.charAt(i)) || inQuote)) {
        char c = line.charAt(i);
        if (!inQuote && c == '#') {
          if (sawQuote || text.length() > 0) {
            tokens.add(token(start, i, text, pos, inQuotes));
          }
          return new Tokens(tokens, i);
        }
        if (!inQuote && c == ',') {
          break;
        }
        if (c != '"' || wasQuote) {
          pos[text.length()] = i;
          inQuotes[text.length()] = inQuote;
          text.append(c);
        }
        wasQuote = inQuote && c == '"' && !wasQuote;
        if (c == '"') {
          inQuote = !inQuote;
          sawQuote = true;
        }
        i++;
      }
      if (sawQuote || text.length() > 0) {
        tokens.add(token(start, i, text, pos, inQuotes));
      }
    }
    return new Tokens(tokens, -1);
  }

  private static Token token(
      int start, int end, StringBuilder text, int[] pos, boolean[] inQuotes) {
    return new Token(
        start,
        end,
        text.toString(),
        Arrays.copyOf(pos, text.length()),
        Arrays.copyOf(inQuotes, text.length()));
  }

  /** Edits for one line, in line positions. */
  private static List<Edit> lineEdits(String line) {
    Tokens tokens = tokenize(line);
    List<Token> list = tokens.tokens();
    List<Token> options = new ArrayList<>();
    List<Edit> exact = new ArrayList<>();
    int toEnd = Integer.MAX_VALUE;
    for (int k = 0; k < list.size(); k++) {
      Token token = list.get(k);
      if (!token.text().startsWith(OPTION_PREFIX)) {
        continue;
      }
      options.add(token);
      int valueStart = token.pos()[KEY.length()] + 1;
      if (onlyOptionsFollow(list, k)) {
        exact.add(new Edit(valueStart, token.end(), replacement(line, token, valueStart)));
      } else {
        toEnd = Math.min(toEnd, valueStart);
      }
    }
    // The comment may be the rest of a password with an unquoted '#'.
    if (!options.isEmpty() && tokens.commentStart() >= 0) {
      toEnd = Math.min(toEnd, tokens.commentStart() + 1);
    }
    for (int from = indexOfKey(line, 0, false); from >= 0; ) {
      int[] match = matchKey(line, from, false);
      int at = from;
      if (at < toEnd && options.stream().noneMatch(t -> t.start() <= at && at < t.end())) {
        toEnd = Math.min(toEnd, keptEnd(line, match[1]));
      }
      from = indexOfKey(line, match[1], false);
    }
    List<Edit> edits = new ArrayList<>();
    for (Edit edit : exact) {
      if (edit.start() >= toEnd) {
        break;
      }
      if (edit.end() > toEnd) {
        toEnd = edit.start();
        break;
      }
      edits.add(edit);
    }
    if (toEnd != Integer.MAX_VALUE) {
      edits.add(new Edit(toEnd, line.length(), REPLACEMENT));
    }
    return edits;
  }

  private static boolean startsRule(String line) {
    List<Token> tokens = tokenize(line).tokens();
    return !tokens.isEmpty() && CONNECTION_TYPES.contains(tokens.get(0).text());
  }

  /** Whether every token after tokens[k] is a name=value option Postgres knows. */
  private static boolean onlyOptionsFollow(List<Token> tokens, int k) {
    for (int j = k + 1; j < tokens.size(); j++) {
      String s = tokens.get(j).text();
      int eq = s.indexOf('=');
      if (eq < 0 || !HBA_OPTION_NAMES.contains(s.substring(0, eq))) {
        return false;
      }
    }
    return true;
  }

  /**
   * The replacement for a value, quoted so that the token stays well formed. A value wrapped in
   * empty quoted strings, ""secret"", keeps that shape: it is how values written as if CSV-escaped
   * read, and earlier releases redacted them to ""REDACTED"", which clients may send back.
   */
  private static String replacement(String line, Token token, int valueStart) {
    if (token.inQuotes()[KEY.length()]) {
      return REPLACEMENT + "\"";
    }
    String raw = line.substring(valueStart, token.end());
    if (raw.length() >= 4 && raw.startsWith("\"\"") && raw.endsWith("\"\"")) {
      return "\"\"" + REPLACEMENT + "\"\"";
    }
    if (raw.startsWith("\"")) {
      return "\"" + REPLACEMENT + "\"";
    }
    return REPLACEMENT;
  }

  /** Redacts from the first ldapbindpasswd in s to the end of s. */
  private static String redactToEnd(String s, boolean freeText) {
    int at = indexOfKey(s, 0, freeText);
    if (at < 0) {
      return s;
    }
    return s.substring(0, keptEnd(s, matchKey(s, at, freeText)[1])) + REPLACEMENT;
  }

  /** End of what is kept after ldapbindpasswd text ending at nameEnd: blanks and an '=', if any. */
  private static int keptEnd(String s, int nameEnd) {
    int j = nameEnd;
    while (j < s.length() && isBlank(s.charAt(j))) {
      j++;
    }
    return j < s.length() && s.charAt(j) == '=' ? j + 1 : nameEnd;
  }

  /**
   * Index of the next ldapbindpasswd at or after from, ignoring case and any quotes inside it (and,
   * for free text, the backslashes and commas that escaping or a continuation adds), or -1.
   */
  private static int indexOfKey(String s, int from, boolean freeText) {
    for (int i = from; i < s.length(); i++) {
      if (Character.toLowerCase(s.charAt(i)) == 'l' && matchKey(s, i, freeText) != null) {
        return i;
      }
    }
    return -1;
  }

  /** {start, end} of ldapbindpasswd starting at i, or null. */
  private static int[] matchKey(String s, int i, boolean freeText) {
    int j = i;
    for (int k = 0; k < KEY.length(); k++) {
      while (k > 0 && j < s.length() && isSkipped(s.charAt(j), freeText)) {
        j++;
      }
      if (j >= s.length() || Character.toLowerCase(s.charAt(j)) != KEY.charAt(k)) {
        return null;
      }
      j++;
    }
    return new int[] {i, j};
  }

  private static boolean isSkipped(char c, boolean freeText) {
    return c == '"' || (freeText && (c == '\\' || c == ','));
  }

  /** Blank as in pg_isblank. */
  private static boolean isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
  }

  /** Maps a line edit onto the fields it covers; only the first gets the replacement. */
  private static void addFieldEdits(Line line, Edit edit, List<List<Edit>> fieldEdits) {
    if (edit.start() == edit.end()) {
      // A value or line end is always preceded by at least the option name.
      int before = edit.start() - 1;
      int offset = line.offset()[before] + 1;
      fieldEdits.get(line.field()[before]).add(new Edit(offset, offset, edit.replacement()));
      return;
    }
    String replacement = edit.replacement();
    int i = edit.start();
    while (i < edit.end()) {
      int field = line.field()[i];
      int j = i;
      while (j + 1 < edit.end() && line.field()[j + 1] == field) {
        j++;
      }
      fieldEdits.get(field).add(new Edit(line.offset()[i], line.offset()[j] + 1, replacement));
      replacement = "";
      i = j + 1;
    }
  }

  /** Applies field edits to the raw value, re-encoding replacements for quoted fields. */
  private static String splice(String value, List<CsvField> fields, List<List<Edit>> fieldEdits) {
    List<Edit> rawEdits = new ArrayList<>();
    for (int k = 0; k < fields.size(); k++) {
      CsvField field = fields.get(k);
      for (Edit edit : fieldEdits.get(k)) {
        int rawStart =
            edit.start() < field.text().length()
                ? field.rawIndex()[edit.start()]
                : CsvFieldSplitter.rawEnd(field, edit.start() - 1);
        int rawEnd =
            edit.end() > edit.start() ? CsvFieldSplitter.rawEnd(field, edit.end() - 1) : rawStart;
        String replacement =
            field.quoted() ? edit.replacement().replace("\"", "\"\"") : edit.replacement();
        rawEdits.add(new Edit(rawStart, rawEnd, replacement));
      }
    }
    if (rawEdits.isEmpty()) {
      return value;
    }
    rawEdits.sort(Comparator.comparingInt(Edit::start));
    StringBuilder out = new StringBuilder(value.length());
    int last = 0;
    for (Edit edit : rawEdits) {
      out.append(value, last, edit.start()).append(edit.replacement());
      last = edit.end();
    }
    return out.append(value, last, value.length()).toString();
  }
}
