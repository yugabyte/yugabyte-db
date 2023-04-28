package com.yugabyte.yw.common.password;

import static org.junit.Assert.assertEquals;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.audit.AuditService;
import org.junit.Test;
import play.libs.Json;

public class RedactingServiceTest {
  private final ObjectMapper mapper = Json.mapper();

  @Test
  public void testFilterSecretFields() {
    ObjectNode jsonNode = getJsonNode();
    JsonNode redactedJson = RedactingService.filterSecretFields(jsonNode);

    assertEquals(AuditService.SECRET_REPLACEMENT, redactedJson.get("ysqlPassword").asText());
    assertEquals("Test Name", redactedJson.get("name").asText());
    assertEquals(AuditService.SECRET_REPLACEMENT, redactedJson.get("ysqlCurrentPassword").asText());
    assertEquals("AW*************ID", redactedJson.get("awsAccessKeyID").asText());

    JsonNode details = redactedJson.get("details");
    assertEquals(
        AuditService.SECRET_REPLACEMENT,
        details.get("cloudInfo").get("aws").get("awsAccessKeySecret").asText());
    assertEquals("test_project", details.get("cloudInfo").get("gcp").get("gceProject").asText());
    assertEquals(
        AuditService.SECRET_REPLACEMENT,
        details.get("cloudInfo").get("gcp").get("gceApplicationCredentials").asText());
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
