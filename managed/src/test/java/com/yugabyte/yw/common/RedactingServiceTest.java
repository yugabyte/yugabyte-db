package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import org.junit.Test;
import play.libs.Json;

public class RedactingServiceTest {

  private final ObjectMapper mapper = Json.mapper();

  @Test
  public void testFilterSecretFieldsForLogs() {
    ObjectNode jsonNode = getJsonNode();
    JsonNode redactedJson = RedactingService.filterSecretFields(jsonNode, RedactionTarget.LOGS);

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
  }

  @Test
  public void testFilterSecretFieldsForApis() {
    ObjectNode jsonNode = getJsonNode();
    JsonNode redactedJson = RedactingService.filterSecretFields(jsonNode, RedactionTarget.APIS);

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
    JsonNode redacted = RedactingService.filterSecretFields(formData, RedactionTarget.LOGS);
    assertEquals(
        SECRET_REPLACEMENT, redacted.get("customServerCertData.serverKeyContent").asText());
    assertEquals(SECRET_REPLACEMENT, redacted.get("certContent").asText());
  }

  @Test
  public void testDoubleEscapedJsonRedaction() {
    String doubleEscapedJson =
        "\"universeDetailsJson\":\"{\\\"platformVersion\\\":\\\"2.27.0.0-PRE_RELEASE\\\",\\\"tserverGFlags\\\":{\\\"ycql_ldap_bind_passwd\\\":\\\"password321\\\",\\\"ysql_hba_conf_csv\\\":\\\"\\\\\\\"ldapbinddn=admint\\\\\\\",\\\\\\\"ldapbindpasswd=secret123\\\\\\\"\\\"}}\"";

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
        "{\"platformVersion\":\"2.27.0.0-PRE_RELEASE\",\"tserverGFlags\":{\"ycql_ldap_bind_passwd\":\"password1\",\"ysql_hba_conf_csv\":\"\\\"ldapbinddn=admint\\\",\\\"ldapbindpasswd=secret123\\\"\"}}";

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

    JsonNode redacted = RedactingService.filterSecretFields(jsonNode, RedactionTarget.LOGS);

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
    cloudInfo.set("aws", aws);
    cloudInfo.set("gcp", gcp);
    details.set("cloudInfo", cloudInfo);
    jsonNode.set("details", details);
    jsonNode.put("awsAccessKeyID", "AW*************ID");

    return jsonNode;
  }
}
