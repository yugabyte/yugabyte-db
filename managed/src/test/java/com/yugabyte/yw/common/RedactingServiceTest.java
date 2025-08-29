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
    // Test the specific case from the logs where ycql_ldap_bind_passwd appears in double-escaped
    // JSON
    String doubleEscapedJson =
        "\"universeDetailsJson\":\"{\\\"platformVersion\\\":\\\"2.27.0.0-PRE_RELEASE\\\",\\\"tserverGFlags\\\":{\\\"ycql_ldap_bind_passwd\\\":\\\"password321\\\",\\\"ysql_hba_conf_csv\\\":\\\"\\\\\\\"ldapbinddn=admint\\\\\\\",\\\\\\\"ldapbindpasswd=REDACTED\\\\\\\",\\\\\\\"ldapBindPassword=REDACTED\\\\\\\"\\\"}}\"";

    String redacted = RedactingService.redactSensitiveInfoInString(doubleEscapedJson);

    assertNotNull(redacted);
    assertTrue(redacted.length() > 0);
    // The ycql_ldap_bind_passwd value should be redacted
    assertFalse(redacted.contains("password321"));
    // The string should still contain the field name
    assertTrue(redacted.contains("ycql_ldap_bind_passwd"));
    // The string should contain REDACTED
    assertTrue(redacted.contains("REDACTED"));
  }

  @Test
  public void testAllLdapPasswordRegexPatterns() {
    // Test all the different regex patterns for LDAP password redaction

    // Pattern 1: Escaped quotes within JSON strings
    String escapedQuotesJson =
        "\"ysql_hba_conf_csv\":\"\\\"ldapbindpasswd=secret123\\\",\\\"ldapBindPassword=secret456\\\"\"";
    String redacted1 = RedactingService.redactSensitiveInfoInString(escapedQuotesJson);
    assertFalse(redacted1.contains("secret123"));
    assertFalse(redacted1.contains("secret456"));
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

    // Pattern 2b: JSON field format "pattern": value (without quotes around value) - fixed to match
    // regex
    String jsonFieldNoQuotes = "\"ycql_ldap_bind_passwd\":12345,\"ldapbindpasswd\":67890}";
    String redacted2b = RedactingService.redactSensitiveInfoInString(jsonFieldNoQuotes);
    assertFalse(redacted2b.contains("12345"));
    assertFalse(redacted2b.contains("67890"));
    assertTrue(redacted2b.contains("REDACTED"));

    // Pattern 3: Regular quotes within CSV strings
    String csvFormat = "\"ldapbindpasswd=pass123,ldapBindPassword=pass456\"";
    String redacted3 = RedactingService.redactSensitiveInfoInString(csvFormat);
    assertFalse(redacted3.contains("pass123"));
    assertFalse(redacted3.contains("pass456"));
    assertTrue(redacted3.contains("REDACTED"));

    // Pattern 4: Unquoted values (standalone) - fixed to avoid comma interference
    String unquotedFormat = "ldapbindpasswd=standalone123 ldapBindPassword=standalone456";
    String redacted4 = RedactingService.redactSensitiveInfoInString(unquotedFormat);
    assertFalse(redacted4.contains("standalone123"));
    assertFalse(redacted4.contains("standalone456"));
    assertTrue(redacted4.contains("REDACTED"));

    // Pattern 5: Conf file format (pattern=value) - fixed to match start of line
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

    // Pattern 7: CLI flag format (--pattern, value) and ('--pattern', 'value')
    String cliFlagFormat = "--ycql_ldap_bind_passwd, secret456 --some_other_flag, 123";
    String cliFlagFormat_1 = "'--ycql_ldap_bind_passwd', 'secret456', '--some_other_flag', '123'";
    String redacted7 = RedactingService.redactSensitiveInfoInString(cliFlagFormat);
    String redacted7_1 = RedactingService.redactSensitiveInfoInString(cliFlagFormat_1);
    assertFalse(redacted7.contains("secret456"));
    assertFalse(redacted7_1.contains("secret456"));
    assertTrue(redacted7.contains("REDACTED"));
    assertTrue(redacted7_1.contains("REDACTED"));
    // Verify the flag names and commas are preserved
    assertTrue(redacted7.contains("--ycql_ldap_bind_passwd, REDACTED"));
    assertTrue(redacted7.contains("--some_other_flag, 123"));
    assertTrue(redacted7_1.contains("'--ycql_ldap_bind_passwd', REDACTED"));
    assertTrue(redacted7_1.contains("'--some_other_flag', '123'"));
  }

  @Test
  public void testSupportBundleUniverseDetailsJsonRedaction() {
    // Test the exact format from support bundle logs
    String universeDetailsJson =
        "{\"platformVersion\":\"2.27.0.0-PRE_RELEASE\",\"tserverGFlags\":{\"ycql_ldap_bind_passwd\":\"password1\",\"ysql_hba_conf_csv\":\"\\\"ldapbinddn=admint\\\",\\\"ldapbindpasswd=REDACTED\\\",\\\"ldapBindPassword=REDACTED\\\"\"}}";

    // Test as a JSON field value
    String jsonWithUniverseDetails =
        "\"universeDetailsJson\":\"" + universeDetailsJson.replace("\"", "\\\"") + "\"";
    String redacted = RedactingService.redactSensitiveInfoInString(jsonWithUniverseDetails);

    assertFalse(redacted.contains("password1"));
    assertTrue(redacted.contains("REDACTED"));
    assertTrue(redacted.contains("ycql_ldap_bind_passwd"));

    // Test the universeDetailsJson directly
    String redactedDirect = RedactingService.redactSensitiveInfoInString(universeDetailsJson);
    assertFalse(redactedDirect.contains("password1"));
    assertTrue(redactedDirect.contains("REDACTED"));
  }

  @Test
  public void testMixedContentRedaction() {
    // Test mixed content with various LDAP password formats
    String mixedContent =
        "{\"config\":\"ldapbindpasswd=mixed123\",\"gflags\":{\"ycql_ldap_bind_passwd\":\"mixed456\"},\"details\":\"\\\"ldapBindPassword=mixed789\\\"\"}";

    String redacted = RedactingService.redactSensitiveInfoInString(mixedContent);

    assertFalse(redacted.contains("mixed123"));
    assertFalse(redacted.contains("mixed456"));
    assertFalse(redacted.contains("mixed789"));
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
