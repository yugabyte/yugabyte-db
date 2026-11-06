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
  public void redactAllValuesKeepsQuoteInsideUnquotedValue() {
    String in = "host all all 0.0.0.0/0 ldap ldapbindpasswd=se\"cret ldaptls=0";
    assertEquals(
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + SECRET_REPLACEMENT + " ldaptls=0",
        LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));
  }

  @Test
  public void redactAllValuesKeepsQuotedCommaInsideUnquotedValue() {
    // HBA reads se",cret" as se,cret.
    String in = "host all all 0.0.0.0/0 ldap ldapbindpasswd=se\",cret\" ldaptls=0";
    assertEquals(
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + SECRET_REPLACEMENT + " ldaptls=0",
        LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));

    String endOfCsvRecordInJson =
        "{\"ysql_hba_conf_csv\":\"\\\"host all all 0.0.0.0/0 ldap ldapbindpasswd=se\\\"\\\",cret"
            + "\\\"\\\"\\\",local all all trust\",\"other\":\"keep\"}";
    String out = LdapBindPasswdHbaFormat.redactAllValues(endOfCsvRecordInJson, SECRET_REPLACEMENT);
    assertFalse(out.contains("cret"));
    assertTrue(Json.parse(out).get("ysql_hba_conf_csv").asText().endsWith(",local all all trust"));
    assertEquals("keep", Json.parse(out).get("other").asText());
  }

  @Test
  public void redactAllValuesKeepsQuotedPunctuationInsideUnquotedValue() {
    // HBA reads pa","ss as pa,ss and ab"}cd" as ab}cd.
    for (String password : new String[] {"pa\",\"ss", "ab\"}cd\"", "ab\"]cd\""}) {
      String in = "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + password + " ldaptls=0";
      assertEquals(
          "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + SECRET_REPLACEMENT + " ldaptls=0",
          LdapBindPasswdHbaFormat.redactAllValues(in, SECRET_REPLACEMENT));
    }

    String endOfCsvRecordInJson =
        "{\"ysql_hba_conf_csv\":\"\\\"host all all 0.0.0.0/0 ldap"
            + " ldapbindpasswd=pa\\\"\\\",\\\"\\\"ss\\\",local all all trust\",\"other\":\"keep\"}";
    String out = LdapBindPasswdHbaFormat.redactAllValues(endOfCsvRecordInJson, SECRET_REPLACEMENT);
    assertEquals(
        "\"host all all 0.0.0.0/0 ldap ldapbindpasswd="
            + SECRET_REPLACEMENT
            + "\",local all all trust",
        Json.parse(out).get("ysql_hba_conf_csv").asText());
    assertEquals("keep", Json.parse(out).get("other").asText());
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
