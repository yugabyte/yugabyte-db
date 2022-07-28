package com.yugabyte.yw.common.audit;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.Test;

import static com.yugabyte.yw.common.audit.AuditService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;

public class AuditServiceTest {
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
    JsonNode redacted = AuditService.filterSecretFields(formData);
    assertEquals(
        SECRET_REPLACEMENT, redacted.get("customServerCertData.serverKeyContent").asText());
  }
}
