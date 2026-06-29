// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.LdapBindPasswdHbaFormat.QuoteStyle;
import org.junit.Test;

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
