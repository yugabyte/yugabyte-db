package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.LdapBindPasswdHbaFormat.QuoteStyle;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import org.junit.Test;
import play.libs.Json;

public class RedactingServiceTest {

  private final ObjectMapper mapper = Json.mapper();

  @Test
  public void testFilterSecretFieldsForLogs() {
    ObjectNode jsonNode = getJsonNode();
    JsonNode redactedJson =
        RedactingService.filterSecretFields(jsonNode, RedactionTarget.LOGS, null, null, true);

    assertEquals(SECRET_REPLACEMENT, redactedJson.get("ysqlPassword").asText());
    assertEquals("Test Name", redactedJson.get("name").asText());
    assertEquals(SECRET_REPLACEMENT, redactedJson.get("ysqlCurrentPassword").asText());
    assertEquals(SECRET_REPLACEMENT, redactedJson.get("awsAccessKeyID").asText());

    JsonNode details = redactedJson.get("details");
    assertEquals(
        SECRET_REPLACEMENT, details.get("cloudInfo").get("aws").get("awsAccessKeySecret").asText());
    assertEquals("test_project", details.get("cloudInfo").get("gcp").get("gceProject").asText());
    assertEquals(
        SECRET_REPLACEMENT,
        details.get("cloudInfo").get("gcp").get("gceApplicationCredentials").asText());
    assertEquals(
        SECRET_REPLACEMENT,
        details.get("cloudInfo").get("oci").get("ociPrivateKeyContent").asText());
    assertEquals(
        SECRET_REPLACEMENT,
        details.get("cloudInfo").get("oci").get("OCI_PRIVATE_KEY_CONTENT").asText());
    assertEquals(
        SECRET_REPLACEMENT, details.get("cloudInfo").get("oci").get("ociFingerprint").asText());
    assertEquals(
        SECRET_REPLACEMENT, details.get("cloudInfo").get("oci").get("OCI_FINGERPRINT").asText());
  }

  @Test
  public void testFilterSecretFieldsForApis() {
    ObjectNode jsonNode = getJsonNode();
    JsonNode redactedJson =
        RedactingService.filterSecretFields(jsonNode, RedactionTarget.APIS, null, null, true);

    assertEquals(SECRET_REPLACEMENT, redactedJson.get("ysqlPassword").asText());
    assertEquals("Test Name", redactedJson.get("name").asText());
    assertEquals(SECRET_REPLACEMENT, redactedJson.get("ysqlCurrentPassword").asText());
    assertEquals("AW*************ID", redactedJson.get("awsAccessKeyID").asText());

    JsonNode details = redactedJson.get("details");
    assertEquals(
        "AWS_ACCESS_KEY_SECRET",
        details.get("cloudInfo").get("aws").get("awsAccessKeySecret").asText());
    assertEquals("test_project", details.get("cloudInfo").get("gcp").get("gceProject").asText());
    assertEquals(
        "Test_Credentials",
        details.get("cloudInfo").get("gcp").get("gceApplicationCredentials").asText());
    assertEquals(
        "-----BEGIN PRIVATE KEY-----\nabc\n-----END PRIVATE KEY-----",
        details.get("cloudInfo").get("oci").get("ociPrivateKeyContent").asText());
    assertEquals(
        "-----BEGIN PRIVATE KEY-----\ndef\n-----END PRIVATE KEY-----",
        details.get("cloudInfo").get("oci").get("OCI_PRIVATE_KEY_CONTENT").asText());
    assertEquals(
        "11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff:00",
        details.get("cloudInfo").get("oci").get("ociFingerprint").asText());
    assertEquals(
        "aa:bb:cc:dd:ee:ff:00:11:22:33:44:55:66:77:88:99",
        details.get("cloudInfo").get("oci").get("OCI_FINGERPRINT").asText());
  }

  @Test
  public void testApiYcqlLdapBindPasswdRedactionFollowsGflagsSensitiveDataConfig() {
    ObjectNode jsonNode = mapper.createObjectNode();
    jsonNode.put("ycql_ldap_bind_passwd", "ycql-secret");
    jsonNode.put("ysqlPassword", "ysql-secret");

    JsonNode disabled =
        RedactingService.filterSecretFields(jsonNode, RedactionTarget.APIS, null, null, false);
    assertEquals("ycql-secret", disabled.get("ycql_ldap_bind_passwd").asText());
    assertEquals(SECRET_REPLACEMENT, disabled.get("ysqlPassword").asText());

    JsonNode enabled =
        RedactingService.filterSecretFields(jsonNode, RedactionTarget.APIS, null, null, true);
    assertEquals(SECRET_REPLACEMENT, enabled.get("ycql_ldap_bind_passwd").asText());
    assertEquals(SECRET_REPLACEMENT, enabled.get("ysqlPassword").asText());
  }

  @Test
  public void testLogsYcqlLdapBindPasswdRedactionIgnoresApiConfig() {
    ObjectNode jsonNode = mapper.createObjectNode();
    jsonNode.put("ycql_ldap_bind_passwd", "ycql-secret");

    JsonNode redacted =
        RedactingService.filterSecretFields(jsonNode, RedactionTarget.LOGS, null, null, false);
    assertEquals(SECRET_REPLACEMENT, redacted.get("ycql_ldap_bind_passwd").asText());
  }

  // Test for API responses YSQL HBA conf csv parsing and redaction through regexes
  @Test
  public void testFilterSecretFieldsApisYsqlHbaConfLdapbindpasswd() {
    ObjectNode root = mapper.createObjectNode();
    String hbaEarlyPasswd =
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=Two Word Secret ldapbinddn=\"\"CN=x,DC=y\"\""
            + " ldapserver=ldap.example.com ldaptls=0";
    root.put("ysql_hba_conf_csv", hbaEarlyPasswd);
    JsonNode out =
        RedactingService.filterSecretFields(root, RedactionTarget.APIS, null, null, true);
    String redacted = out.get("ysql_hba_conf_csv").asText();
    assertFalse(redacted.contains("Two Word Secret"));
    assertTrue(redacted.contains("ldapbinddn"));
    assertTrue(redacted.contains("CN=x"));
    assertTrue(redacted.contains("ldapserver=ldap.example.com"));

    // Password last on the line (still under ysql_hba_conf_csv)
    ObjectNode root2 = mapper.createObjectNode();
    String hbaPasswdLast =
        "host all all 0.0.0.0/0 ldap ldapserver=h ldapbinddn=\"\"CN=u\"\" ldapbindpasswd=LastOnly";
    root2.put("ysql_hba_conf_csv", hbaPasswdLast);
    JsonNode out2 =
        RedactingService.filterSecretFields(root2, RedactionTarget.APIS, null, null, true);
    String redacted2 = out2.get("ysql_hba_conf_csv").asText();
    assertFalse(redacted2.contains("LastOnly"));
    assertTrue(redacted2.contains("ldapserver=h"));
    assertTrue(redacted2.contains("ldapbinddn"));
  }

  /**
   * Doubled-quote hba must not use the unquoted ldapbindpasswd path (no double REDACTED fragments).
   */
  @Test
  public void testApiYsqlHbaDoubledQuoteLdapbindpasswdStable() {
    ObjectNode root = mapper.createObjectNode();
    String hba =
        "host all all 0.0.0.0/0 ldap ldapserver=s ldapbinddn=\"\"CN=x\"\""
            + " ldapbindpasswd=\"\"myRealSecret\"\" ldapsearchattribute=\"\"all\"\" ldaptls=0";
    root.put("ysql_hba_conf_csv", hba);
    JsonNode out =
        RedactingService.filterSecretFields(root, RedactionTarget.APIS, null, null, true);
    String redacted = out.get("ysql_hba_conf_csv").asText();
    assertFalse(redacted.contains("myRealSecret"));
    assertTrue(redacted.contains("ldapbindpasswd=\"\"" + SECRET_REPLACEMENT + "\"\""));
    assertFalse(
        "Corrupted double-redaction fragment",
        redacted.contains("REDACTED\"REDACTED") || redacted.contains("REDACTED\"REDACTED\"\""));

    ObjectNode again = mapper.createObjectNode();
    again.put("ysql_hba_conf_csv", redacted);
    JsonNode out2 =
        RedactingService.filterSecretFields(again, RedactionTarget.APIS, null, null, true);
    String secondPass = out2.get("ysql_hba_conf_csv").asText();
    assertTrue(secondPass.contains("ldapbindpasswd=\"\"" + SECRET_REPLACEMENT + "\"\""));
    assertFalse(secondPass.contains("REDACTED\"REDACTED"));
  }

  @Test
  public void testPrivateKeyObfuscation() {
    JsonNode formData =
        new ObjectMapper()
            .createObjectNode()
            .put("certStart", "1658833887000")
            .put("customServerCertData.serverCertContent", "...")
            .put("certType", "CustomServerCert")
            .put("customServerCertData.serverKeyContent", "to-be-redacted")
            .put("certContent", "...")
            .put("certExpiry", "1690369886000")
            .put("label", "b4hmtemimzdr5eslzbv2kxz6z4-fbLbNlu8");
    JsonNode redacted =
        RedactingService.filterSecretFields(formData, RedactionTarget.LOGS, null, null, true);
    assertEquals(
        SECRET_REPLACEMENT, redacted.get("customServerCertData.serverKeyContent").asText());
    assertEquals(SECRET_REPLACEMENT, redacted.get("certContent").asText());
  }

  @Test
  public void testDoubleEscapedJsonRedaction() {
    String doubleEscapedJson =
        "\"universeDetailsJson\":\"{\\\"tserverGFlags\\\":{\\\"ycql_ldap_bind_passwd\\\":\\\"password321\\\",\\\"ysql_hba_conf_csv\\\":\\\"\\\\\\\"ldapbinddn=admint\\\\\\\",\\\\\\\"ldapbindpasswd=REDACTED\\\\\\\",\\\\\\\"ldapBindPassword=REDACTED\\\\\\\"\\\"}}\"";

    String redacted = RedactingService.redactSensitiveInfoInString(doubleEscapedJson);

    assertNotNull(redacted);
    assertTrue(redacted.length() > 0);
    assertFalse(redacted.contains("password321"));
    assertFalse(redacted.contains("secret123"));
    assertTrue(redacted.contains("ycql_ldap_bind_passwd"));
    assertTrue(redacted.contains("REDACTED"));
  }

  @Test
  public void testAllLdapPasswordRegexPatterns() {
    // Pattern 1: Escaped quotes within JSON strings (ysql_hba_conf_csv format)
    String escapedQuotesJson = "\"ysql_hba_conf_csv\":\"\\\"ldapbindpasswd=secret123\\\"\"";
    String redacted1 = RedactingService.redactSensitiveInfoInString(escapedQuotesJson);
    assertFalse(redacted1.contains("secret123"));
    assertTrue(redacted1.contains("REDACTED"));

    // Pattern 2: JSON field format "pattern": "value"
    String jsonFieldFormat =
        "\"ycql_ldap_bind_passwd\":\"password1\",\"ldapbindpasswd\":\"password321\"";
    String redacted2 = RedactingService.redactSensitiveInfoInString(jsonFieldFormat);
    assertFalse(redacted2.contains("password1"));
    assertFalse(redacted2.contains("password321"));
    assertTrue(redacted2.contains("REDACTED"));

    // JSON string values may contain escaped quotes (\"); [^"]+ used to stop early and leak tails.
    String jsonWithEscapedQuotesInPassword =
        "\"ycql_ldap_bind_passwd\":\"h*Y(^979hAB%$|~\\\"\\\"abc\\\"\\\"`P-=/.,;\","
            + "\"other\":\"x\"";
    String redactedEscapedInJson =
        RedactingService.redactSensitiveInfoInString(jsonWithEscapedQuotesInPassword);
    assertFalse(redactedEscapedInJson.contains("979hAB"));
    assertFalse(redactedEscapedInJson.contains("abc"));
    assertTrue(redactedEscapedInJson.contains("\"ycql_ldap_bind_passwd\":\"REDACTED\""));

    // Pattern 2a: Double-escaped JSON format within stringified JSON
    String doubleEscapedFormat =
        "\"universeDetailsJson\":\"{\\\"tserverGFlags\\\":{\\\"ycql_ldap_bind_passwd\\\":\\\"testpass\\\"}}\"";
    String redacted2a = RedactingService.redactSensitiveInfoInString(doubleEscapedFormat);
    assertFalse(redacted2a.contains("testpass"));
    assertTrue(redacted2a.contains("REDACTED"));

    // Pattern 2b: JSON field format "pattern": value (without quotes)
    String jsonFieldNoQuotes = "\"ycql_ldap_bind_passwd\":12345,\"ldapbindpasswd\":67890}";
    String redacted2b = RedactingService.redactSensitiveInfoInString(jsonFieldNoQuotes);
    assertFalse(redacted2b.contains("12345"));
    assertFalse(redacted2b.contains("67890"));
    assertTrue(redacted2b.contains("REDACTED"));

    // Pattern 3: Regular quotes within CSV strings
    String csvFormat = "\"ldapbindpasswd=pass123\"";
    String redacted3 = RedactingService.redactSensitiveInfoInString(csvFormat);
    assertFalse(redacted3.contains("pass123"));
    assertTrue(redacted3.contains("REDACTED"));

    // Pattern 4: Unquoted values (standalone)
    String unquotedFormat = "ldapbindpasswd=standalone123";
    String redacted4 = RedactingService.redactSensitiveInfoInString(unquotedFormat);
    assertFalse(redacted4.contains("standalone123"));
    assertTrue(redacted4.contains("REDACTED"));

    // Pattern 5: Conf file format (pattern=value)
    String confFormat = "ycql_ldap_bind_passwd=confpass123\nldapbindpasswd=confpass456";
    String redacted5 = RedactingService.redactSensitiveInfoInString(confFormat);
    assertFalse(redacted5.contains("confpass123"));
    assertFalse(redacted5.contains("confpass456"));
    assertTrue(redacted5.contains("REDACTED"));

    // Pattern 6: Quoted format (pattern="value")
    String quotedFormat =
        "ycql_ldap_bind_passwd=\"quotedpass123\"\nldapbindpasswd=\"quotedpass456\"";
    String redacted6 = RedactingService.redactSensitiveInfoInString(quotedFormat);
    assertFalse(redacted6.contains("quotedpass123"));
    assertFalse(redacted6.contains("quotedpass456"));
    assertTrue(redacted6.contains("REDACTED"));

    // Pattern 7: CLI flag format (--pattern, value)
    String cliFlagFormat = "--ycql_ldap_bind_passwd, secret456 --some_other_flag, 123";
    String cliFlagFormat_1 = "'--ycql_ldap_bind_passwd', 'secret456', '--some_other_flag', '123'";
    String redacted7 = RedactingService.redactSensitiveInfoInString(cliFlagFormat);
    String redacted7_1 = RedactingService.redactSensitiveInfoInString(cliFlagFormat_1);
    assertFalse(redacted7.contains("secret456"));
    assertFalse(redacted7_1.contains("secret456"));
    assertTrue(redacted7.contains("REDACTED"));
    assertTrue(redacted7_1.contains("REDACTED"));
    assertTrue(redacted7.contains("--ycql_ldap_bind_passwd, REDACTED"));
    assertTrue(redacted7.contains("--some_other_flag, 123"));
    assertTrue(redacted7_1.contains("'--ycql_ldap_bind_passwd', REDACTED"));
    assertTrue(redacted7_1.contains("'--some_other_flag', '123'"));

    // CLI value may contain commas, '=', '`', ';' — must not stop at first comma.
    String cliFlagWithCommaInPassword =
        "--ycql_ldap_bind_passwd, h*Y(^979hAB%$|~`P-=/.,;, --webserver_port, 9000";
    String redactedCommaPwd =
        RedactingService.redactSensitiveInfoInString(cliFlagWithCommaInPassword);
    assertFalse(redactedCommaPwd.contains("h*Y(^979"));
    assertFalse(redactedCommaPwd.contains("979hAB"));
    assertTrue(redactedCommaPwd.contains("--ycql_ldap_bind_passwd, REDACTED"));
    assertTrue(redactedCommaPwd.contains("--webserver_port, 9000"));

    // Comma-separated gflags line (not --flag, value): password may contain "" and commas; do not
    // stop at the first " or comma.
    String commaSeparatedGflags =
        "rpc_throttle_threshold_bytes=1048569,"
            + " ycql_ldap_bind_passwd=h*Y(^979hAB%$|~\"\"abc\"\"`P-=/.,;, webserver_port=9000";
    String redactedGflagLine = RedactingService.redactSensitiveInfoInString(commaSeparatedGflags);
    assertFalse(redactedGflagLine.contains("979hAB"));
    assertFalse(redactedGflagLine.contains("\"\"abc\"\""));
    assertTrue(redactedGflagLine.contains("ycql_ldap_bind_passwd=REDACTED"));
    assertTrue(redactedGflagLine.contains("webserver_port=9000"));

    String commaSeparatedNoSpaceAfterComma =
        "ycql_ldap_bind_passwd=secret,with,commas,webserver_port=9000";
    String redactedNoSpace =
        RedactingService.redactSensitiveInfoInString(commaSeparatedNoSpaceAfterComma);
    assertFalse(redactedNoSpace.contains("secret,with,commas"));
    assertTrue(redactedNoSpace.contains("ycql_ldap_bind_passwd=REDACTED"));
    assertTrue(redactedNoSpace.contains("webserver_port=9000"));

    // Pattern 8: ysql_hba_conf_csv with CSV double-quote escaping (real-world UI input)
    // This tests the exact format: ldapbinddn=""value"" ldapbindpasswd=password
    // ldapbasedn=""value""
    String csvDoubleQuoteEscaped =
        "\"host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ldapport=389 "
            + "ldapbinddn=\"\"cn=admin,dc=example,dc=com\"\" "
            + "ldapbindpasswd=MyPassword123 "
            + "ldapbasedn=\"\"dc=example,dc=com\"\" ldapsearchattribute=uid\"";
    String redacted8 = RedactingService.redactSensitiveInfoInString(csvDoubleQuoteEscaped);
    assertFalse("Password should be redacted", redacted8.contains("MyPassword123"));
    assertTrue("Should contain REDACTED", redacted8.contains("REDACTED"));
    // Crucially: ldapbasedn should NOT be eaten by the regex
    assertTrue(
        "ldapbasedn should be preserved",
        redacted8.contains("ldapbasedn=\"\"dc=example,dc=com\"\""));
    assertTrue(
        "ldapsearchattribute should be preserved", redacted8.contains("ldapsearchattribute=uid"));
  }

  @Test
  public void testSupportBundleUniverseDetailsJsonRedaction() {
    String universeDetailsJson =
        "{\"tserverGFlags\":{\"ycql_ldap_bind_passwd\":\"password1\",\"ysql_hba_conf_csv\":\"\\\"ldapbinddn=admint\\\",\\\"ldapbindpasswd=REDACTED\\\",\\\"ldapBindPassword=REDACTED\\\"\"}}";

    String jsonWithUniverseDetails =
        "\"universeDetailsJson\":\"" + universeDetailsJson.replace("\"", "\\\"") + "\"";
    String redacted = RedactingService.redactSensitiveInfoInString(jsonWithUniverseDetails);

    assertFalse(redacted.contains("password1"));
    assertFalse(redacted.contains("secret123"));
    assertTrue(redacted.contains("REDACTED"));
    assertTrue(redacted.contains("ycql_ldap_bind_passwd"));

    String redactedDirect = RedactingService.redactSensitiveInfoInString(universeDetailsJson);
    assertFalse(redactedDirect.contains("password1"));
    assertTrue(redactedDirect.contains("REDACTED"));
  }

  @Test
  public void testMixedContentRedaction() {
    String mixedContent =
        "{\"config\":\"ldapbindpasswd=mixed123\",\"gflags\":{\"ycql_ldap_bind_passwd\":\"mixed456\"}}";

    String redacted = RedactingService.redactSensitiveInfoInString(mixedContent);

    assertFalse(redacted.contains("mixed123"));
    assertFalse(redacted.contains("mixed456"));
    assertTrue(redacted.contains("REDACTED"));
  }

  @Test
  public void testLdapBindPasswdDoubleDoubleQuotesInYsqlHbaConfCsv() {
    // PostgreSQL hba / ysql_hba_conf_csv often wraps LDAP attribute values as ""..."" .
    String hbaCsv =
        "host all all 0.0.0.0/0 ldap ldapserver=server.server.com ldapbasedn=\"\"OU=TRY,DC=try\""
            + " ldapbinddn=\"\"CN=user,OU=TRY,DC=try\"\""
            + " ldapbindpasswd=\"\")L=1AW*&@|~]))\"\""
            + " ldapsearchattribute=\"\"all\"\" ldaptls=0";
    String redacted = RedactingService.redactSensitiveInfoInString(hbaCsv);
    assertFalse(redacted.contains(")L=1AW*&@|~]))"));
    assertTrue(redacted.contains("ldapbindpasswd=\"\"" + SECRET_REPLACEMENT + "\"\""));
    assertTrue(redacted.contains("OU=TRY")); // non-secret doubled-quote fields unchanged
  }

  /** Password with embedded quotes encoded as doubled CSV. */
  @Test
  public void testLdapBindPasswdWithEmbeddedQuotesDoubledCsv() {
    // Logical password: p"a"ss; hba encodes each " as ""
    String hba =
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=\"\"p\"\"a\"\"ss\"\" ldapsearchattribute=x";
    String redacted = RedactingService.redactSensitiveInfoInString(hba);
    assertFalse(redacted.contains("p\"a\"ss"));
    assertTrue(redacted.contains("ldapbindpasswd=\"\"" + SECRET_REPLACEMENT + "\"\""));
    assertTrue(redacted.contains("ldapsearchattribute=x"));
  }

  @Test
  public void testLdapBindPasswdSingleQuotedWithCsvEscapedQuotes() {
    // Logical password p"a"ss between one pair of outer double quotes
    String hba = "host all all 0.0.0.0/0 ldap ldapbindpasswd=\"p\"\"a\"\"ss\" ldapport=636";
    String redacted = RedactingService.redactSensitiveInfoInString(hba);
    assertFalse(redacted.contains("p\"a\"ss"));
    assertTrue(redacted.contains("ldapbindpasswd=\"" + SECRET_REPLACEMENT + "\""));
    assertTrue(redacted.contains("ldapport=636"));
  }

  @Test
  public void testLdapBindPasswdUnquotedWithCommasAndTrailingQuoteChars() {
    String pwd = "h*Y(^979hAB%$|~abc`P-=/.,;'";
    String hba =
        "host all all 0.0.0.0/0 ldap ldapserver=server.server.com ldapbinddn=\"\"CN=u\"\" "
            + "ldapbindpasswd="
            + pwd
            + " ldapsearchattribute=\"\"all\"\" ldaptls=0";
    String redacted = RedactingService.redactSensitiveInfoInString(hba);
    assertFalse(redacted.contains(pwd));
    assertTrue(redacted.contains("ldapbindpasswd=" + SECRET_REPLACEMENT));
    assertTrue(redacted.contains("ldapsearchattribute"));
  }

  /** Comma in unquoted password must not eat the next record (hostssl, hostnossl, ...). */
  @Test
  public void testLdapBindPasswdUnquotedCommaThenHostsslRecordBoundary() {
    String pwd = "a,b";
    String hba =
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + pwd + ",hostssl all all 127.0.0.1/32 trust";
    String redacted = RedactingService.redactSensitiveInfoInString(hba);
    assertFalse(redacted.contains("a,b"));
    assertTrue(redacted.contains("ldapbindpasswd=" + SECRET_REPLACEMENT));
    assertTrue(redacted.contains("hostssl all all 127.0.0.1/32 trust"));
  }

  @Test
  public void testLdapBindPasswdEscapedDoubleDoubleQuotes() {
    // As it appears inside a JSON string (one backslash before each quote).
    String jsonEscaped =
        "host all yugabyte all ldap ldapbindpasswd=\\\"\\\"secretPwd123\\\"\\\" ldaptls=0";
    String redacted = RedactingService.redactSensitiveInfoInString(jsonEscaped);
    assertFalse(redacted.contains("secretPwd123"));
    assertTrue(redacted.contains("ldapbindpasswd=\\\"\\\"" + SECRET_REPLACEMENT + "\\\"\\\""));
  }

  @Test
  public void testLdapBindPasswdDeepEscapedDoubleDoubleQuotes() {
    // Nested JSON: each \" from the inner string becomes \\\" in the outer string.
    String deep =
        "e,DC=two,DC=three ldapbindpasswd=\\\\\"\\\\\"nestedSecret\\\\\"\\\\\" ldapport=696";
    String redacted = RedactingService.redactSensitiveInfoInString(deep);
    assertFalse(redacted.contains("nestedSecret"));
    assertTrue(
        redacted.contains("ldapbindpasswd=\\\\\"\\\\\"" + SECRET_REPLACEMENT + "\\\\\"\\\\\""));
  }

  /** Redacted JSON must still parse (no broken backslashes in string values). */
  @Test
  public void testRedactLdapInJsonEscapedDoubleQuotesStillParses() {
    String json =
        "{\"clusters\":[{\"userIntent\":{\"tserverGFlags\":{\"ysql_hba_conf_csv\":\"host all"
            + " ldapbindpasswd=\\\"\\\"secret123\\\"\\\"\"}}}]}";
    String redacted = RedactingService.redactSensitiveInfoInString(json);
    assertFalse(redacted.contains("secret123"));
    Json.parse(redacted); // must remain valid JSON
  }

  /** Deep JSON escaping of doubled-csv hba inside ysql_hba_conf_csv still redacts and parses. */
  @Test
  public void testRedactLdapDoubleEmbeddedInJsonStringStillParses() {
    String innerEscapedPair = "\\" + "\"" + "\\" + "\"";
    String hbaValue =
        "local trust,ldapbindpasswd="
            + innerEscapedPair
            + ")L=1AW*&@|~]))"
            + innerEscapedPair
            + " ldapsearchattribute=x";
    StringBuilder json = new StringBuilder("{\"ysql_hba_conf_csv\":\"");
    for (char c : hbaValue.toCharArray()) {
      if (c == '\\') {
        json.append("\\\\");
      } else if (c == '"') {
        json.append("\\\"");
      } else {
        json.append(c);
      }
    }
    json.append("\"}");
    String jsonStr = json.toString();

    String redacted = RedactingService.redactSensitiveInfoInString(jsonStr);
    assertFalse(redacted.contains(")L=1AW*&@|~]))"));
    assertTrue(redacted.contains(SECRET_REPLACEMENT));
    Json.parse(redacted);
  }

  /**
   * Passwords containing literal double-quote characters must be fully redacted even when the field
   * appears in conf-file format (key=value, no surrounding JSON quotes).
   */
  /**
   * In CLI command-array logging ([--flag=val, --flag=val, ...]) the next item starts with "--",
   * not an identifier. Without a ", --" stop the scanner consumes all subsequent flags.
   */
  @Test
  public void testYcqlLdapBindPasswdInCliArrayFormatDoesNotConsumeNextFlags() {
    // Simulates: [..., --ycql_ldap_bind_passwd=secret, --webserver_port=9000, ...]
    String cliArray =
        "[--cql_proxy_bind_address=10.0.0.1:9042, "
            + "--ycql_ldap_bind_passwd=h*Y(^979hAB%$|~\"abc\"`P-=/.,;'123, "
            + "--webserver_port=9000, "
            + "--ysql_hba_conf_csv=local all yugabyte trust]";
    String redacted = RedactingService.redactSensitiveInfoInString(cliArray);
    assertFalse("Password must be fully redacted", redacted.contains("979hAB"));
    assertFalse(redacted.contains("abc"));
    assertTrue(redacted.contains(SECRET_REPLACEMENT));
    // Subsequent flags must NOT be consumed into the redacted value.
    assertTrue("webserver_port must survive", redacted.contains("--webserver_port=9000"));
    assertTrue(
        "ysql_hba_conf_csv must survive", redacted.contains("--ysql_hba_conf_csv=local all"));
  }

  @Test
  public void testYcqlLdapBindPasswdWithEmbeddedQuotesInConfFormat() {
    // Password with embedded " chars in conf-file format (as seen in server logs).
    String confLine =
        "rpc_bind_addresses=10.0.0.1:9100,"
            + " ycql_ldap_bind_passwd=h*Y(^979hAB%$|~\"abc\"`P-=/.,;'123, webserver_port=9000";
    String redacted = RedactingService.redactSensitiveInfoInString(confLine);
    assertFalse("Password with embedded quotes must be fully redacted", redacted.contains("abc"));
    assertFalse(redacted.contains("979hAB"));
    assertTrue(redacted.contains(SECRET_REPLACEMENT));
    assertTrue(redacted.contains("rpc_bind_addresses=10.0.0.1:9100"));
    assertTrue(redacted.contains("webserver_port=9000"));

    // Same password at end of string (no trailing comma).
    String confLineEnd = "ycql_ldap_bind_passwd=h*Y(^979hAB%$|~\"abc\"`P-=/.,;'123";
    String redactedEnd = RedactingService.redactSensitiveInfoInString(confLineEnd);
    assertFalse(redactedEnd.contains("abc"));
    assertTrue(redactedEnd.contains(SECRET_REPLACEMENT));

    // JSON key-value form must still redact correctly (regression guard).
    String jsonForm = "{\"ycql_ldap_bind_passwd\":\"simplepass\"}";
    String redactedJson = RedactingService.redactSensitiveInfoInString(jsonForm);
    assertFalse(redactedJson.contains("simplepass"));
    assertTrue(redactedJson.contains(SECRET_REPLACEMENT));
  }

  @Test
  public void testEdgeCasesAndBoundaries() {
    // Test edge cases and boundary conditions

    // Empty strings
    String empty = "";
    String redactedEmpty = RedactingService.redactSensitiveInfoInString(empty);
    assertEquals(empty, redactedEmpty);

    // Strings with no LDAP passwords
    String noLdap = "{\"name\":\"test\",\"value\":123}";
    String redactedNoLdap = RedactingService.redactSensitiveInfoInString(noLdap);
    assertEquals(noLdap, redactedNoLdap);

    // Strings with partial matches (should not redact)
    String partialMatch = "ldapbindpasswd_partial=value";
    String redactedPartial = RedactingService.redactSensitiveInfoInString(partialMatch);
    assertTrue(redactedPartial.contains("value")); // Should not be redacted
  }

  @Test
  public void testJsonPathRedactionWithLdapPasswords() {
    // Test that JSONPath redaction works correctly with LDAP passwords
    ObjectNode jsonNode = mapper.createObjectNode();
    jsonNode.put("ycql_ldap_bind_passwd", "jsonpath123"); // Should be redacted by JSONPath
    jsonNode.put("config", "ldapbindpasswd=regex123"); // Should be redacted by regex

    JsonNode redacted =
        RedactingService.filterSecretFields(jsonNode, RedactionTarget.LOGS, null, null, true);

    assertNotNull(redacted);
    String redactedString = redacted.toString();
    assertFalse(redactedString.contains("jsonpath123"));
    assertFalse(redactedString.contains("regex123"));
    assertTrue(redactedString.contains("REDACTED"));
  }

  @Test
  public void testShellProcessHandlerLogsRedaction() {
    // Test that logs of the form (item={'key': 'ycql_ldap_bind_passwd', 'value': 'password'}) gets
    // redacted
    String logLineFromShell =
        "ok: [SERVER_ID] => (item={'key': 'ycql_ldap_bind_passwd', 'value': 'passw123'})";
    String redactedLogLine = RedactingService.redactSensitiveInfoInString(logLineFromShell);
    assertFalse(redactedLogLine.contains("passw123"));
    assertTrue(redactedLogLine.contains("REDACTED"));
  }

  @Test
  public void testAuditAdditionalDetailsGflagsRedaction() {
    // Test redaction of sensitive gflags in audit additionalDetails section
    // This is the structure returned by the audit API for GFlags upgrade tasks
    ObjectNode auditEntry = mapper.createObjectNode();
    ObjectNode additionalDetails = mapper.createObjectNode();
    ObjectNode gflags = mapper.createObjectNode();

    // Create tserver gflags array with sensitive flag
    ObjectNode sensitiveFlag = mapper.createObjectNode();
    sensitiveFlag.put("name", "ycql_ldap_bind_passwd");
    sensitiveFlag.putNull("old");
    sensitiveFlag.put("new", "test1231");
    sensitiveFlag.put("default", "");

    ObjectNode normalFlag = mapper.createObjectNode();
    normalFlag.put("name", "some_normal_flag");
    normalFlag.put("old", "oldvalue");
    normalFlag.put("new", "newvalue");
    normalFlag.put("default", "defaultvalue");

    gflags.putArray("tserver").add(sensitiveFlag).add(normalFlag);
    gflags.putArray("master");
    additionalDetails.set("gflags", gflags);

    ObjectNode readonlyGflags = mapper.createObjectNode();
    ObjectNode readonlySensitive = mapper.createObjectNode();
    readonlySensitive.put("name", "ycql_ldap_bind_passwd");
    readonlySensitive.putNull("old");
    readonlySensitive.put("new", "readonly-secret");
    readonlySensitive.put("default", "");
    ObjectNode readonlyNormal = mapper.createObjectNode();
    readonlyNormal.put("name", "rpc_throttle_threshold_bytes");
    readonlyNormal.putNull("old");
    readonlyNormal.put("new", "1048569");
    readonlyNormal.put("default", "1048576");
    readonlyGflags.putArray("tserver").add(readonlySensitive).add(readonlyNormal);
    readonlyGflags.putArray("master");
    additionalDetails.set("readonly_cluster_gflags", readonlyGflags);

    auditEntry.set("additionalDetails", additionalDetails);

    // Test single audit entry (non-array case)
    JsonNode redacted = RedactingService.redactAuditAdditionalDetails(auditEntry, null, null);

    // Verify the sensitive flag values are redacted
    JsonNode redactedTserverFlags = redacted.get("additionalDetails").get("gflags").get("tserver");
    JsonNode redactedSensitiveFlag = redactedTserverFlags.get(0);
    assertEquals("ycql_ldap_bind_passwd", redactedSensitiveFlag.get("name").asText());
    assertEquals(SECRET_REPLACEMENT, redactedSensitiveFlag.get("new").asText());
    // null values should remain null (not be redacted)
    assertTrue(redactedSensitiveFlag.get("old").isNull());
    // Empty string should NOT be redacted (it means "no value", not a hidden secret)
    assertEquals("", redactedSensitiveFlag.get("default").asText());

    // Verify the normal flag values are NOT redacted
    JsonNode redactedNormalFlag = redactedTserverFlags.get(1);
    assertEquals("some_normal_flag", redactedNormalFlag.get("name").asText());
    assertEquals("oldvalue", redactedNormalFlag.get("old").asText());
    assertEquals("newvalue", redactedNormalFlag.get("new").asText());
    assertEquals("defaultvalue", redactedNormalFlag.get("default").asText());

    JsonNode redactedReadonly =
        redacted.get("additionalDetails").get("readonly_cluster_gflags").get("tserver");
    assertEquals(SECRET_REPLACEMENT, redactedReadonly.get(0).get("new").asText());
    assertEquals("1048569", redactedReadonly.get(1).get("new").asText());
  }

  @Test
  public void testAuditAdditionalDetailsGflagsRedactionArray() {
    // Test redaction when input is an array of audit entries
    ObjectNode auditEntry1 = mapper.createObjectNode();
    ObjectNode additionalDetails1 = mapper.createObjectNode();
    ObjectNode gflags1 = mapper.createObjectNode();

    ObjectNode sensitiveFlag1 = mapper.createObjectNode();
    sensitiveFlag1.put("name", "ycql_ldap_bind_passwd");
    sensitiveFlag1.put("old", "oldpassword");
    sensitiveFlag1.put("new", "newpassword");
    gflags1.putArray("tserver").add(sensitiveFlag1);
    additionalDetails1.set("gflags", gflags1);
    auditEntry1.set("additionalDetails", additionalDetails1);

    // Create array of audit entries
    JsonNode auditArray = mapper.createArrayNode().add(auditEntry1);

    JsonNode redacted = RedactingService.redactAuditAdditionalDetails(auditArray, null, null);

    // Verify the sensitive flag values are redacted
    JsonNode redactedEntry = redacted.get(0);
    JsonNode redactedFlag =
        redactedEntry.get("additionalDetails").get("gflags").get("tserver").get(0);
    assertEquals("ycql_ldap_bind_passwd", redactedFlag.get("name").asText());
    assertEquals(SECRET_REPLACEMENT, redactedFlag.get("old").asText());
    assertEquals(SECRET_REPLACEMENT, redactedFlag.get("new").asText());
  }

  /** Verifies ldapbindpasswd redaction for various password shapes and API JSON responses. */
  @Test
  public void testLdapBindPasswdRedactVariousPasswordPatterns() {
    String[] doubledCsvPasswords = {
      "h*Y(^979hAB%$|~\"abc\"`P-=/.,;'",
      "Xv|k)=4#|Z{n1Q@rp",
      "cost$1\\2",
      "",
      "a",
      "weird\"pwd\"",
      "Two Word Secret",
      "\t",
      "\\\\",
      "密码🔐",
      "\"\"",
      "a,b,cd",
      "h*Y(^979hAB%$|~\"\"abc\"\"`P-=/.,;’",
      "line1\nline2",
    };

    for (String password : doubledCsvPasswords) {
      String encoded = LdapBindPasswdHbaFormat.encode(QuoteStyle.DOUBLED_CSV, password);
      String hba =
          "host all all 0.0.0.0/0 ldap ldapserver=s.example.com ldapbindpasswd="
              + encoded
              + " ldapport=389 ldaptls=0";

      String redacted = RedactingService.redactAllLdapBindPasswdHbaFormattedValues(hba);
      assertTrue(
          "redacted hba must contain placeholder: " + safePreview(password),
          redacted.contains(SECRET_REPLACEMENT));
    }

    // Unquoted hba (no spaces/commas that break the unquoted lexer in this line).
    String[] unquotedSafe = {"Xv|k)=4#|Z{n1Q@rp", "simpleAlnum9", "p\"q"};
    for (String password : unquotedSafe) {
      String hba = "host ldap ldapbindpasswd=" + password + " ldapport=1 ldaptls=0";
      String redacted = RedactingService.redactAllLdapBindPasswdHbaFormattedValues(hba);
      assertTrue(redacted.contains(SECRET_REPLACEMENT));
    }

    // API-shaped JSON: ysql_hba_conf_csv must redact and remain valid JSON.
    String tricky = doubledCsvPasswords[0];
    String encodedTricky = LdapBindPasswdHbaFormat.encode(QuoteStyle.DOUBLED_CSV, tricky);
    String hbaForJson =
        "host all all 0.0.0.0/0 ldap ldapbindpasswd=" + encodedTricky + " ldapport=1";
    ObjectNode root = mapper.createObjectNode();
    root.put("ysql_hba_conf_csv", hbaForJson);
    JsonNode out =
        RedactingService.filterSecretFields(root, RedactionTarget.APIS, null, null, true);
    String redactedCsv = out.get("ysql_hba_conf_csv").asText();
    assertFalse(redactedCsv.contains(tricky));
    assertTrue(redactedCsv.contains(SECRET_REPLACEMENT));
    root.put("ysql_hba_conf_csv", redactedCsv);
    Json.parse(root.toString());

    // ycql_ldap_bind_passwd in JSON (gflag-style) must redact without leaving secrets in the text.
    for (String pwd :
        new String[] {
          "h*Y(^979hAB%$|~\"abc\"`P-=/.,;'", "Xv|k)=4#|Z{n1Q@rp", "cost$1\\2",
        }) {
      ObjectNode gflagJson = mapper.createObjectNode();
      gflagJson.put("ycql_ldap_bind_passwd", pwd);
      gflagJson.put("webserver_port", "9000");
      String raw = gflagJson.toString();
      String redactedJson = RedactingService.redactSensitiveInfoInString(raw);
      assertFalse(
          "ycql JSON redaction leaked password: " + safePreview(pwd), redactedJson.contains(pwd));
      assertTrue(redactedJson.contains(SECRET_REPLACEMENT));
      assertTrue(redactedJson.contains("webserver_port"));
      Json.parse(redactedJson);
    }
  }

  /**
   * Passwords containing literal backslashes (e.g. cost$1\2) must be fully redacted. Previously the
   * scanner stopped at the backslash and left the tail of the password visible in logs.
   */
  @Test
  public void testYcqlLdapBindPasswdWithLiteralBackslashIsFullyRedacted() {
    // Conf-file / Combined-gflags format: key=value pairs separated by ", name=".
    String confLine =
        "cql_proxy_bind_address=10.0.0.1:9042, "
            + "ycql_ldap_bind_passwd=cost$1\\2secret, "
            + "webserver_port=9000";
    String redacted = RedactingService.redactSensitiveInfoInString(confLine);
    assertFalse(
        "Backslash-containing password must be fully redacted", redacted.contains("2secret"));
    assertFalse(redacted.contains("cost$1"));
    assertTrue(redacted.contains(SECRET_REPLACEMENT));
    assertTrue("Non-sensitive flags must survive", redacted.contains("webserver_port=9000"));

    // JSON gflag value with a backslash in the password.
    ObjectNode gflagJson = mapper.createObjectNode();
    gflagJson.put("ycql_ldap_bind_passwd", "cost$1\\2secret");
    gflagJson.put("webserver_port", "9000");
    String rawJson = gflagJson.toString();
    String redactedJson = RedactingService.redactSensitiveInfoInString(rawJson);
    assertFalse(redactedJson.contains("2secret"));
    assertFalse(redactedJson.contains("cost$1"));
    assertTrue(redactedJson.contains(SECRET_REPLACEMENT));
    assertTrue(redactedJson.contains("webserver_port"));
  }

  /**
   * Passwords that contain DN-component-like substrings (e.g. ",OU=x,DC=y") must be fully redacted.
   * Previously the scanner stopped at the first ",OU=" or ",DC=" (treating the 2-char identifier as
   * a key boundary) and leaked the rest of the password. The fix requires at least 3 chars before
   * "=" to count as a key boundary, excluding all standard 2-char DN component codes (OU, DC, CN,
   * etc.) while still stopping at real gflag names like "webserver_port". Note: ycql_ldap_bind_dn
   * itself benefits from the same fix at runtime when GFlagsValidation provides it as a sensitive
   * field; here we verify the scanner logic via ycql_ldap_bind_passwd.
   */
  @Test
  public void testPasswordContainingDnComponentsIsFullyRedacted() {
    // Simulate a password that looks like a Distinguished Name value with short name= segments.
    String dnPassword = "yugabyte123,OU=SA,OU=CROW,DC=try,DC=one,DC=two,DC=three";

    // Combined gflags conf format — scanner must cross every ",OU=" and ",DC=" without stopping.
    String confLine =
        "cql_proxy_bind_address=10.0.0.1:9042, "
            + "ycql_ldap_bind_passwd="
            + dnPassword
            + ", webserver_port=9000";
    String redacted = RedactingService.redactSensitiveInfoInString(confLine);
    assertFalse("Password start must be redacted", redacted.contains("yugabyte123"));
    assertFalse("OU= component inside password must not leak", redacted.contains("OU=SA"));
    assertFalse("DC= component inside password must not leak", redacted.contains("DC=try"));
    assertTrue(redacted.contains(SECRET_REPLACEMENT));
    assertTrue("Non-sensitive gflags must survive", redacted.contains("webserver_port=9000"));

    // CLI array format (--version command logging).
    String cliArray =
        "[--cql_proxy_bind_address=10.0.0.1:9042, "
            + "--ycql_ldap_bind_passwd="
            + dnPassword
            + ", --webserver_port=9000]";
    String redactedCli = RedactingService.redactSensitiveInfoInString(cliArray);
    assertFalse(redactedCli.contains("yugabyte123"));
    assertFalse(redactedCli.contains("OU=SA"));
    assertTrue(redactedCli.contains(SECRET_REPLACEMENT));
    assertTrue(redactedCli.contains("--webserver_port=9000"));
  }

  private static String safePreview(String password) {
    if (password.length() <= 24) {
      return password.replace("\n", "\\n").replace("\r", "\\r");
    }
    return password.substring(0, 24).replace("\n", "\\n") + "...";
  }

  private ObjectNode getJsonNode() {
    ObjectNode jsonNode = mapper.createObjectNode();
    jsonNode.put("name", "Test Name");
    jsonNode.put("ysqlPassword", "YSQL#123");
    jsonNode.put("ysqlCurrentPassword", "YSQL$123");
    ObjectNode details = mapper.createObjectNode();
    ObjectNode cloudInfo = mapper.createObjectNode();
    ObjectNode aws = mapper.createObjectNode();
    aws.put("awsAccessKeySecret", "AWS_ACCESS_KEY_SECRET");
    ObjectNode gcp = mapper.createObjectNode();
    gcp.put("gceProject", "test_project");
    gcp.put("gceApplicationCredentials", "Test_Credentials");
    ObjectNode oci = mapper.createObjectNode();
    oci.put("ociPrivateKeyContent", "-----BEGIN PRIVATE KEY-----\nabc\n-----END PRIVATE KEY-----");
    oci.put(
        "OCI_PRIVATE_KEY_CONTENT", "-----BEGIN PRIVATE KEY-----\ndef\n-----END PRIVATE KEY-----");
    oci.put("ociFingerprint", "11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff:00");
    oci.put("OCI_FINGERPRINT", "aa:bb:cc:dd:ee:ff:00:11:22:33:44:55:66:77:88:99");
    cloudInfo.set("aws", aws);
    cloudInfo.set("gcp", gcp);
    cloudInfo.set("oci", oci);
    details.set("cloudInfo", cloudInfo);
    jsonNode.set("details", details);
    jsonNode.put("awsAccessKeyID", "AW*************ID");

    return jsonNode;
  }
}
