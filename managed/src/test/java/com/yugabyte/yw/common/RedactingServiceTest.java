package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;

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
