// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.LdapBindPasswdHbaFormat.QuoteStyle;
import org.junit.Test;
import play.libs.Json;

/** Unit tests for {@link LdapBindPasswdHbaFormat} (redact and encode). */
public class LdapBindPasswdHbaFormatTest {

  @Test
  public void redactAllValuesRedactsMultipleLdapbindpasswd() {
    String in = "a ldapbindpasswd=one ldapport=1 b ldapbindpasswd=two ldapport=2";
    String out = LdapBindPasswdHbaFormat.redactAllValues(in, "[X]");
    assertFalse(out.contains("one"));
    assertFalse(out.contains("two"));
    assertTrue(out.contains("ldapbindpasswd=[X]"));
    assertTrue(out.contains("ldapport=1"));
    assertTrue(out.contains("ldapport=2"));
  }

  @Test
  public void redactAllValuesNullAndEmptyUnchanged() {
    assertNull(LdapBindPasswdHbaFormat.redactAllValues(null, SECRET_REPLACEMENT));
    assertEquals("", LdapBindPasswdHbaFormat.redactAllValues("", SECRET_REPLACEMENT));
  }

  @Test
  public void redactAllValuesPreservesDoubledCsvQuoting() {
    String in = "host ldap ldapbindpasswd=\"\"p\"\"a\"\"ss\"\" ldapsearch=x";
    String out = LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT);
    assertFalse(out.contains("p\"a\"ss"));
    assertTrue(out.contains("ldapbindpasswd=\"\"" + SECRET_REPLACEMENT + "\"\""));
  }

  @Test
  public void redactAllValuesPreservesEscapedDoubledForm() {
    String in = "host ldap ldapbindpasswd=\\\"\\\"sec\\\"\\\" ldaptls=0";
    String out = LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT);
    assertFalse(out.contains("sec"));
    assertTrue(out.contains("ldapbindpasswd=\\\"\\\"" + SECRET_REPLACEMENT + "\\\"\\\""));
  }

  @Test
  public void redactAllValuesStopsUnquotedValueAtEndOfJsonString() {
    String in =
        "{\"ysql_hba_conf_csv\":\"host all all 0.0.0.0/0 ldap ldapbindpasswd=secret\","
            + "\"other\":\"keep\"}";
    assertEquals(
        "{\"ysql_hba_conf_csv\":\"host all all 0.0.0.0/0 ldap ldapbindpasswd="
            + SECRET_REPLACEMENT
            + "\",\"other\":\"keep\"}",
        LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));
  }

  @Test
  public void redactAllValuesKeepsClosingQuoteOfCsvQuotedRecord() {
    String in =
        "local all yugabyte trust,\"host all all 0.0.0.0/0 ldap"
            + " ldapbasedn=\"\"dc=example,dc=com\"\" ldapbindpasswd=secret\"";
    assertEquals(
        "local all yugabyte trust,\"host all all 0.0.0.0/0 ldap"
            + " ldapbasedn=\"\"dc=example,dc=com\"\" ldapbindpasswd="
            + SECRET_REPLACEMENT
            + "\"",
        LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));
  }

  @Test
  public void redactAllValuesKeepsEscapedClosingQuoteOfCsvQuotedRecordInJson() {
    String in =
        "{\"ysql_hba_conf_csv\":\"\\\"host all all 0.0.0.0/0 ldap ldapbindpasswd=secret\\\"\","
            + "\"other\":\"keep\"}";
    assertEquals(
        "{\"ysql_hba_conf_csv\":\"\\\"host all all 0.0.0.0/0 ldap ldapbindpasswd="
            + SECRET_REPLACEMENT
            + "\\\"\",\"other\":\"keep\"}",
        LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));
  }

  @Test
  public void redactAllValuesStopsUnquotedValueAtLineBreak() {
    for (String lineBreak : new String[] {"\n", "\r\n"}) {
      String in =
          "--ysql_hba_conf_csv=host all all 0.0.0.0/0 ldap ldapbindpasswd=secret"
              + lineBreak
              + "--placement_cloud=aws"
              + lineBreak;
      assertEquals(
          "--ysql_hba_conf_csv=host all all 0.0.0.0/0 ldap ldapbindpasswd="
              + SECRET_REPLACEMENT
              + lineBreak
              + "--placement_cloud=aws"
              + lineBreak,
          LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));
    }
  }

  @Test
  public void redactAllValuesRedactsDoublyEscapedQuotedPasswordEndingJsonString() {
    // ldapbindpasswd=\"\"secret\"\" serialized into a JSON string.
    String in =
        "{\"ysql_hba_conf_csv\":\"host ldap"
            + " ldapbindpasswd=\\\\\\\"\\\\\\\"secret\\\\\\\"\\\\\\\"\"}";
    String out = LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT);
    assertFalse(out.contains("secret"));
    assertTrue(Json.parse(out).get("ysql_hba_conf_csv").asText().contains(SECRET_REPLACEMENT));
  }

  @Test
  public void redactAllValuesRedactsQuotedPasswordEndingCsvRecordInJson() {
    String hba =
        "\"host all all 0.0.0.0/0 ldap ldapbasedn=\"\"dc=example,dc=com\"\""
            + " ldapbindpasswd=\"\"secret\"\"\",local all all trust";
    String in = Json.newObject().put("ysql_hba_conf_csv", hba).put("other", "keep").toString();
    String out = LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT);
    assertEquals(
        hba.replace("secret", SECRET_REPLACEMENT),
        Json.parse(out).get("ysql_hba_conf_csv").asText());
    assertEquals("keep", Json.parse(out).get("other").asText());
  }

  @Test
  public void redactAllValuesRedactsUnquotedPasswordWithPunctuationInJson() {
    for (String password : new String[] {"se\\cret", "ab}cd", "ab]cd", "p#w=x"}) {
      String hba =
          "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + password + ",local all all trust";
      String in = Json.newObject().put("ysql_hba_conf_csv", hba).toString();
      String out = LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT);
      assertEquals(
          "host all all 0.0.0.0/0 ldap ldapbindpasswd="
              + SECRET_REPLACEMENT
              + ",local all all trust",
          Json.parse(out).get("ysql_hba_conf_csv").asText());
    }
  }

  @Test
  public void redactAllValuesEndsQuotedPasswordAtAnyOption() {
    String hba =
        "\"hostssl all all 0.0.0.0/0 ldap ldapserver=x ldapbindpasswd=\"\"secret\"\""
            + " clientcert=verify-full\",local all all trust";
    String expected = hba.replace("secret", SECRET_REPLACEMENT);
    assertEquals(expected, LdapBindPasswdHbaFormat.redactAllValues(hba, SECRET_REPLACEMENT));
    String in = Json.newObject().put("ysql_hba_conf_csv", hba).toString();
    assertEquals(
        expected,
        Json.parse(LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT))
            .get("ysql_hba_conf_csv")
            .asText());
    // Without whitespace before it, name= after a quote is still password.
    String quoteInPassword = "host ldap ldapbindpasswd=\"\"a\"\"\"\"b=c\"\" clientcert=verify-full";
    assertEquals(
        "host ldap ldapbindpasswd=\"\"" + SECRET_REPLACEMENT + "\"\" clientcert=verify-full",
        LdapBindPasswdHbaFormat.redactAllValues(quoteInPassword, SECRET_REPLACEMENT));
  }

  // The four P1-04 forms, each as the CSV field of a quoted record: se",cret" pa","ss "pa"ss
  // se"cret
  private static final String[] PARTLY_QUOTED_CSV_PASSWORDS = {
    "se\"\",cret\"\"", "pa\"\",\"\"ss", "\"\"pa\"\"ss", "se\"\"cret"
  };

  @Test
  public void redactAllValuesRedactsPartlyQuotedPasswordToEndOfField() {
    for (String password : PARTLY_QUOTED_CSV_PASSWORDS) {
      String hba =
          "\"host all all 0.0.0.0/0 ldap ldapserver=x ldapbindpasswd="
              + password
              + "\",local all all trust";
      String expected =
          "\"host all all 0.0.0.0/0 ldap ldapserver=x ldapbindpasswd="
              + SECRET_REPLACEMENT
              + "\",local all all trust";
      assertEquals(
          password, expected, LdapBindPasswdHbaFormat.redactAllValues(hba, SECRET_REPLACEMENT));
      String in = Json.newObject().put("ysql_hba_conf_csv", hba).put("other", "keep").toString();
      String out = LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT);
      assertEquals(password, expected, Json.parse(out).get("ysql_hba_conf_csv").asText());
      assertEquals("keep", Json.parse(out).get("other").asText());
    }
  }

  @Test
  public void redactAllValuesRedactsPartlyQuotedPasswordEndingString() {
    assertEquals(
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + SECRET_REPLACEMENT,
        LdapBindPasswdHbaFormat.redactAllValues(
            "host all all 0.0.0.0/0 ldap ldapbindpasswd=\"pa\"ss", SECRET_REPLACEMENT));
  }

  @Test
  public void redactAllValuesKeepsEmptyValue() {
    String in = "host all all 0.0.0.0/0 ldap ldapbindpasswd= ldapserver=x,local all all trust";
    assertEquals(in, LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));
  }

  @Test
  public void encodeNullDecodedReturnsEmpty() {
    assertEquals("", LdapBindPasswdHbaFormat.encode(QuoteStyle.UNQUOTED, null));
    assertEquals("", LdapBindPasswdHbaFormat.encode(QuoteStyle.SINGLE_CSV, null));
  }

  @Test
  public void encodeUnquotedUnchanged() {
    assertEquals("plain", LdapBindPasswdHbaFormat.encode(QuoteStyle.UNQUOTED, "plain"));
  }

  @Test
  public void encodeSingleCsvDoublesInternalQuotes() {
    assertEquals("\"a\"\"b\"", LdapBindPasswdHbaFormat.encode(QuoteStyle.SINGLE_CSV, "a\"b"));
  }

  @Test
  public void encodeDoubledCsvDoublesInternalQuotes() {
    // Outer "" + body x""y"" + closing ""
    assertEquals(
        "\"\"x\"\"y\"\"\"\"", LdapBindPasswdHbaFormat.encode(QuoteStyle.DOUBLED_CSV, "x\"y\""));
  }

  @Test
  public void encodeEscapedDoubledCsv() {
    assertEquals(
        "\\\"\\\"p\\\"\\\"q\\\"\\\"",
        LdapBindPasswdHbaFormat.encode(QuoteStyle.ESCAPED_DOUBLED_CSV, "p\"q"));
  }

  @Test
  public void encodeDeepEscapedDoubledCsv() {
    assertEquals(
        "\\\\\"\\\\\"r\\\\\"\\\\\"s\\\\\"\\\\\"",
        LdapBindPasswdHbaFormat.encode(QuoteStyle.DEEP_ESCAPED_DOUBLED_CSV, "r\"s"));
  }
}
