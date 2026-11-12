// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.HbaConfRedactor.CsvField;
import com.yugabyte.yw.common.HbaConfRedactor.Token;
import com.yugabyte.yw.common.HbaConfRedactor.Tokens;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.apache.commons.csv.CSVFormat;
import org.apache.commons.csv.CSVParser;
import org.apache.commons.csv.CSVRecord;
import org.junit.Test;
import play.libs.Json;

public class HbaConfRedactorTest {

  private static final String LDAP = "host all all 0.0.0.0/0 ldap ldapserver=h ldapbasedn=x ";

  // Independent of HbaConfRedactor: ldapbindpasswd in any case with quotes inside, then blanks
  // and an '=' if there is one.
  private static final Pattern KEPT_KEY =
      Pattern.compile("(?i)l\"*d\"*a\"*p\"*b\"*i\"*n\"*d\"*p\"*a\"*s\"*s\"*w\"*d(?:[ \\t\\r]*=)?");

  private static void assertRedacted(String expected, String value) {
    String redacted = HbaConfRedactor.redact(value);
    assertEquals(expected, redacted);
    assertEquals("redaction must be stable", redacted, HbaConfRedactor.redact(redacted));
  }

  @Test
  public void testNoPassword() {
    assertNull(HbaConfRedactor.redact(null));
    String value = "host all all 0.0.0.0/0 md5,local all yugabyte trust";
    assertSame(value, HbaConfRedactor.redact(value));
  }

  @Test
  public void testUnquotedPassword() {
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED ldaptls=1", LDAP + "ldapbindpasswd=secret ldaptls=1");
    assertRedacted(
        "\"" + LDAP + "ldapbindpasswd=REDACTED\",local all all trust",
        "\"" + LDAP + "ldapbindpasswd=secret\",local all all trust");
  }

  @Test
  public void testCommaSeparatedOptionsInQuotedField() {
    assertRedacted(
        "\"" + LDAP + "ldapserver=h,ldapbindpasswd=REDACTED,ldaptls=1\"",
        "\"" + LDAP + "ldapserver=h,ldapbindpasswd=secret,ldaptls=1\"");
  }

  @Test
  public void testQuotedPasswordKeepsItsQuotes() {
    assertRedacted(
        "\"" + LDAP + "ldapbindpasswd=\"\"REDACTED\"\" ldaptls=1\"",
        "\"" + LDAP + "ldapbindpasswd=\"\"pa ss, #x\"\" ldaptls=1\"");
    assertRedacted(
        LDAP + "ldapbindpasswd=\"REDACTED\",local all all trust",
        LDAP + "ldapbindpasswd=\"pa ss\",local all all trust");
    // The password a"b: "" inside the HBA quotes, doubled again by CSV.
    assertRedacted(
        "\"" + LDAP + "ldapbindpasswd=\"\"REDACTED\"\"\"",
        "\"" + LDAP + "ldapbindpasswd=\"\"a\"\"\"\"b\"\"\"");
    assertRedacted(
        LDAP + "ldapbindpasswd=\"REDACTED\" ldaptls=1", LDAP + "ldapbindpasswd=\"\" ldaptls=1");
    assertRedacted(
        "\"" + LDAP + "ldapbindpasswd=\"\"REDACTED\"\"\"  ,local all all trust",
        "\"" + LDAP + "ldapbindpasswd=\"\"pa ss\"\"\"  ,local all all trust");
  }

  @Test
  public void testValueWrappedInEmptyQuotesKeepsItsShape() {
    assertRedacted(
        LDAP + "ldapbindpasswd=\"\"REDACTED\"\" ldaptls=1",
        LDAP + "ldapbindpasswd=\"\"secret\"\" ldaptls=1");
    assertRedacted(LDAP + "ldapbindpasswd=\"REDACTED\"", LDAP + "ldapbindpasswd=\"\"");
  }

  @Test
  public void testLinesSplitOffThePassword() {
    // An unquoted comma ends the CSV field, so the rest of the password becomes lines that
    // Postgres rejects or ignores.
    assertRedacted(
        LDAP
            + "ldapbindpasswd=REDACTED,REDACTED, ,REDACTED,local all all trust,host all all ::1/128"
            + " trust",
        LDAP + "ldapbindpasswd=pa,ss word, ,# x,local all all trust,host all all ::1/128 trust");
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED ldaptls=1,more",
        LDAP + "ldapbindpasswd=secret ldaptls=1,more");
  }

  @Test
  public void testPartlyQuotedPassword() {
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED ldaptls=1",
        LDAP + "ldapbindpasswd=pa\"s s\"word ldaptls=1");
    assertRedacted(LDAP + "\"ldapbindpasswd=REDACTED\"", LDAP + "\"ldapbindpasswd=pa ss\"");
    assertRedacted(LDAP + "\"ldapbindpasswd=REDACTED\"", LDAP + "\"ldapbindpasswd=pa\"ss");
    // An unclosed quote runs to the end of the line, all of it the password.
    assertRedacted(LDAP + "ldapbindpasswd=\"REDACTED\"", LDAP + "ldapbindpasswd=\"pa ss ldaptls=1");
  }

  @Test
  public void testQuotedOptionName() {
    assertRedacted(LDAP + "\"ldapbindpasswd\"=REDACTED", LDAP + "\"ldapbindpasswd\"=secret");
    assertRedacted(LDAP + "ldap\"bind\"passwd=REDACTED", LDAP + "ldap\"bind\"passwd=secret");
  }

  @Test
  public void testEveryPasswordIsRedacted() {
    assertRedacted(
        LDAP
            + "ldapbindpasswd=REDACTED ldapbindpasswd=REDACTED,"
            + LDAP
            + "ldapbindpasswd=REDACTED,local all all trust",
        LDAP
            + "ldapbindpasswd=one ldapbindpasswd=two,"
            + LDAP
            + "ldapbindpasswd=three,local all all trust");
  }

  @Test
  public void testInvalidRecordIsRedactedToItsEnd() {
    String rest = ",local all all trust";
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED" + rest,
        LDAP + "ldapbindpasswd=Two Word Secret ldaptls=1" + rest);
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED" + rest, LDAP + "ldapbindpasswd=secret key=abc" + rest);
    assertRedacted(LDAP + "ldapbindpasswd=REDACTED" + rest, LDAP + "ldapbindpasswd= secret" + rest);
    assertRedacted(
        "\"" + LDAP + "ldapbindpasswd=REDACTED\"" + rest,
        "\"" + LDAP + "ldapbindpasswd=pa,ss ldaptls=1\"" + rest);
    assertRedacted(LDAP + "LdapBindPasswd=REDACTED" + rest, LDAP + "LdapBindPasswd=secret" + rest);
  }

  @Test
  public void testCommentAfterPassword() {
    assertRedacted(LDAP + "ldapbindpasswd=REDACTED#REDACTED", LDAP + "ldapbindpasswd=pa#ss");
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED ldaptls=1 #REDACTED",
        LDAP + "ldapbindpasswd=pa ldaptls=1 # x");
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED#REDACTED,REDACTED", LDAP + "ldapbindpasswd=p#a,ss");
    assertRedacted(
        "local all all trust # ldapbindpasswd=REDACTED,local all all md5",
        "local all all trust # ldapbindpasswd=secret,local all all md5");
    assertRedacted(LDAP + "\"ldapbindpasswd=REDACTED\"", LDAP + "\"ldapbindpasswd=pa#ss\"");
  }

  @Test
  public void testTextOutsideAnOptionIsRedactedToTheEnd() {
    // A space before the quote makes the whole quoted record one HBA token: the connection type.
    assertRedacted(
        "local all yugabyte trust, \"host all all 0.0.0.0/0 ldap ldapbindpasswd=REDACTED",
        "local all yugabyte trust, \"host all all 0.0.0.0/0 ldap ldapbindpasswd=x\"");
    assertRedacted(
        LDAP + "ldapsearchfilter=\"(cn=ldapbindpasswd=REDACTED",
        LDAP + "ldapsearchfilter=\"(cn=ldapbindpasswd=x)\" ldapbindpasswd=secret");
    // Postgres reads one valid filter here, so the password would otherwise show.
    assertRedacted(
        LDAP + "\"ldapsearchfilter=(x) ldapbindpasswd=REDACTED",
        LDAP + "\"ldapsearchfilter=(x) ldapbindpasswd=secret\"");
  }

  @Test
  public void testValueTheCsvReaderRejects() {
    assertRedacted(
        "\"" + LDAP + "ldapbindpasswd=REDACTED", "\"" + LDAP + "ldapbindpasswd=secret ldaptls=1");
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED", LDAP + "ldapbindpasswd=\"pa\nss\",local all all trust");
  }

  @Test
  public void testContinuationLines() {
    assertRedacted(
        LDAP + "\\,ldapbindpasswd=REDACTED ldaptls=1", LDAP + "\\,ldapbindpasswd=secret ldaptls=1");
    // Postgres reads "secret" across the two fields.
    assertRedacted(
        LDAP + "ldapbindpasswd=REDACTED\\, ldaptls=1", LDAP + "ldapbindpasswd=sec\\,ret ldaptls=1");
    assertRedacted(LDAP + "ldapbindpasswd=REDACTED\\,", LDAP + "ldapbindpasswd=sec\\,ret junk");
    assertRedacted(LDAP + "ldapbind\\,passwd=REDACTED", LDAP + "ldapbind\\,passwd=secret");
    assertRedacted(
        "\"" + LDAP + "ldap\\\",bindpasswd=REDACTED", "\"" + LDAP + "ldap\\\",bindpasswd=secret");
    // The DB joins the last field with the lines it generates.
    assertRedacted(LDAP + "ldapbindpasswd=REDACTED", LDAP + "ldapbindpasswd=abc\\");
  }

  @Test
  public void testFreeText() {
    assertEquals(
        "x ldapbindpasswd=REDACTED\nnext line",
        HbaConfRedactor.redactFreeText("x ldapbindpasswd=secret ldaptls=1\nnext line"));
    assertEquals(
        "{\\\"ysql_hba_conf_csv\\\":\\\"host ldap ldapbindpasswd=REDACTED",
        HbaConfRedactor.redactFreeText(
            "{\\\"ysql_hba_conf_csv\\\":\\\"host ldap ldapbindpasswd=\\\"pa ss\\\""
                + " ldaptls=1\\\"}"));
    assertEquals(
        "ldap\\\"bind\\\"passwd=REDACTED",
        HbaConfRedactor.redactFreeText("ldap\\\"bind\\\"passwd=x"));
    assertEquals(
        "ldapbind\\,passwd=REDACTED", HbaConfRedactor.redactFreeText("ldapbind\\,passwd=x"));
    assertEquals(
        "--ysql_hba_conf_csv="
            + LDAP
            + "ldapbindpasswd=REDACTED ldaptls=1,local all all trust\r\n"
            + "--max_log_size=256",
        HbaConfRedactor.redactFreeText(
            "--ysql_hba_conf_csv="
                + LDAP
                + "ldapbindpasswd=secret ldaptls=1,local all all trust\r\n"
                + "--max_log_size=256"));
  }

  @Test
  public void testFreeTextFoldedYaml() {
    String yaml =
        "  gflags:\n"
            + "    ysql_hba_conf_csv: "
            + LDAP
            + "ldapbindpasswd=\"long pass\n"
            + "      phrase tail\"\n"
            + "    max_log_size: 256\n";
    assertEquals(
        "  gflags:\n"
            + "    ysql_hba_conf_csv: "
            + LDAP
            + "ldapbindpasswd=REDACTED\n"
            + "      REDACTED\n"
            + "    max_log_size: 256\n",
        HbaConfRedactor.redactFreeText(yaml));
    // Java stack frames are indented with a tab, not spaces.
    assertEquals(
        "error: ldapbindpasswd=REDACTED\n\tat com.yugabyte.Foo",
        HbaConfRedactor.redactFreeText("error: ldapbindpasswd=x\n\tat com.yugabyte.Foo"));
  }

  @Test
  public void testSplitCsvMatchesCommonsCsv() {
    CSVFormat format = CSVFormat.DEFAULT.builder().setRecordSeparator("").build();
    char[] alphabet = {'a', ',', '"', ' ', '\t'};
    int checked = 0;
    for (int len = 1; len <= 7; len++) {
      int[] digits = new int[len];
      while (true) {
        StringBuilder sb = new StringBuilder();
        for (int d : digits) {
          sb.append(alphabet[d]);
        }
        String value = sb.toString();
        List<String> expected;
        try (CSVParser parser = CSVParser.parse(value, format)) {
          List<CSVRecord> records = parser.getRecords();
          assertEquals(value, 1, records.size());
          expected = records.get(0).toList();
        } catch (IOException | UncheckedIOException e) {
          expected = null;
        }
        List<CsvField> fields = HbaConfRedactor.splitCsv(value);
        assertEquals(
            value, expected, fields == null ? null : fields.stream().map(CsvField::text).toList());
        if (fields != null) {
          for (CsvField field : fields) {
            for (int i = 0; i < field.text().length(); i++) {
              assertEquals(value, field.text().charAt(i), value.charAt(field.rawIndex()[i]));
            }
          }
        }
        checked++;
        int k = len - 1;
        while (k >= 0 && ++digits[k] == alphabet.length) {
          digits[k--] = 0;
        }
        if (k < 0) {
          break;
        }
      }
    }
    assertTrue(checked > 90_000);
  }

  /**
   * Checks the redactor against how YB's Postgres parses each line of hba_pg_rules.jsonl: [line,
   * "valid" | "invalid" | "none" (no rule, e.g. a comment), the ldapbindpasswd Postgres read]. The
   * file comes from loading the lines into a ysql_hba.conf and reading pg_hba_file_rules (see
   * gen_hba_pg_rules.py next to it).
   */
  @Test
  public void testAgreesWithPostgres() {
    int checked = 0;
    for (String row : TestUtils.readResource("redaction/hba_pg_rules.jsonl").split("\n")) {
      JsonNode r = Json.parse(row);
      String line = r.get(0).asText();
      String status = r.get(1).asText();
      String passwd = r.get(2).isNull() ? null : r.get(2).asText();
      checkAgainstPostgres(line, status, passwd, "\"" + line.replace("\"", "\"\"") + "\"");
      if (line.indexOf(',') < 0 && !line.startsWith("\"")) {
        checkAgainstPostgres(line, status, passwd, line);
      }
      checked++;
    }
    assertTrue(checked > 900);
  }

  private static void checkAgainstPostgres(
      String line, String status, String passwd, String value) {
    List<CsvField> fields = HbaConfRedactor.splitCsv(HbaConfRedactor.redact(value));
    assertEquals(line, 1, fields.size());
    String redacted = fields.get(0).text();
    Tokens tokens = HbaConfRedactor.tokenize(line);
    if (status.equals("valid") && passwd != null) {
      String last = null;
      for (Token token : tokens.tokens()) {
        if (token.text().startsWith("ldapbindpasswd=")) {
          last = token.text().substring("ldapbindpasswd=".length());
        }
      }
      assertEquals(line, passwd, last);
      List<String> expected =
          tokens.tokens().stream()
              .map(
                  t ->
                      t.text().startsWith("ldapbindpasswd=") ? "ldapbindpasswd=REDACTED" : t.text())
              .toList();
      Tokens actual = HbaConfRedactor.tokenize(redacted);
      assertEquals(line, expected, actual.tokens().stream().map(Token::text).toList());
      if (tokens.commentStart() >= 0) {
        assertEquals(line, "#REDACTED", redacted.substring(actual.commentStart()));
      }
      return;
    }
    Matcher m = KEPT_KEY.matcher(line);
    assertTrue(line, m.find());
    assertEquals(line, line.substring(0, m.end()) + "REDACTED", redacted);
  }
}
