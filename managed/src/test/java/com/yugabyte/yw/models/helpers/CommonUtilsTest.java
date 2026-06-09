// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.not;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class CommonUtilsTest {

  @Test
  public void maskConfigWithSensitiveData() {
    ObjectNode config = prepareConfig();
    JsonNode maskedData = CommonUtils.maskConfig(config);
    assertValue(maskedData, "SOME_KEY", "SE**********TA");
    assertValue(maskedData, "KEY_DATA", "SE**********TA");
    assertValue(maskedData, "MY_KEY_DATA", "SE**********TA");
    assertValue(maskedData, "SOME_SECRET", "SE**********TA");
    assertValue(maskedData, "SECRET_DATA", "SE**********TA");
    assertValue(maskedData, "MY_SECRET_DATA", "SE**********TA");
    assertValue(maskedData, "MY_PASSWORD", "********");
    assertValue(maskedData, "DATA", "VALUE");
    assertValue(maskedData, "MY_KEY", "********");
  }

  private ObjectNode prepareConfig() {
    ObjectNode config = Json.newObject();
    config.put("SOME_KEY", "SENSITIVE_DATA");
    config.put("KEY_DATA", "SENSITIVE_DATA");
    config.put("MY_KEY_DATA", "SENSITIVE_DATA");
    config.put("SOME_SECRET", "SENSITIVE_DATA");
    config.put("SECRET_DATA", "SENSITIVE_DATA");
    config.put("MY_SECRET_DATA", "SENSITIVE_DATA");
    config.put("MY_PASSWORD", "SENSITIVE_DATA"); // Strong sensitive, complete masking.
    config.put("MY_KEY", "DATA"); // Sensitive, complete masking as the lenght is less than 5.
    config.put("DATA", "VALUE");
    return config;
  }

  @Test
  public void testMaskConfigWithoutSensitiveData() {
    ObjectNode config = Json.newObject();
    config.put("SOME_DATA", "VISIBLE_DATA");
    JsonNode maskedData = CommonUtils.maskConfig(config);
    assertValue(maskedData, "SOME_DATA", "VISIBLE_DATA");
  }

  @Test
  public void testMaskBearerTokenNestedObject() {
    // The inner `token` string (exact field name) must be masked.
    // The `bearerToken` container object must remain an object - not collapsed to "********".
    // `authTokenIssueDate` must NOT be masked (contains "token" but is a date, not a secret).
    ObjectNode bearerTokenNode = Json.newObject();
    bearerTokenNode.put("token", "6203effbbc01ebef9ec79f3e75de5a2bFFFFNRAL");
    ObjectNode config = Json.newObject();
    config.set("bearerToken", bearerTokenNode);
    config.put("authType", "BearerToken");
    config.put("authTokenIssueDate", "2024-01-01T00:00:00Z");

    JsonNode maskedData = CommonUtils.maskConfig(config);

    assertThat(maskedData.get("bearerToken").isObject(), is(true));
    assertThat(
        maskedData.get("bearerToken").get("token").asText(),
        not("6203effbbc01ebef9ec79f3e75de5a2bFFFFNRAL"));
    assertValue(maskedData, "authType", "BearerToken");
    assertValue(maskedData, "authTokenIssueDate", "2024-01-01T00:00:00Z");
  }

  @Test
  public void testMaskSplunkToken() {
    // A top-level field named exactly `token` must be masked (exact-name match).
    ObjectNode config = Json.newObject();
    config.put("token", "splunk-secret-token-value");
    config.put("endpoint", "https://splunk.example.com");

    JsonNode maskedData = CommonUtils.maskConfig(config);

    assertThat(maskedData.get("token").asText(), not("splunk-secret-token-value"));
    assertValue(maskedData, "endpoint", "https://splunk.example.com");
  }

  @Test
  public void testMaskConfigWithNullData() {
    JsonNode maskedData = CommonUtils.maskConfig(null);
    assertThat(maskedData.size(), is(0));
    assertThat(maskedData, notNullValue());
  }

  @Test
  public void testGetNodeProperty() {
    String propertyPath = "data.test.foo";
    JsonNode nullNode = CommonUtils.getNodeProperty(null, propertyPath);
    assertThat(nullNode, nullValue());
    JsonNode testObject = Json.newObject();
    nullNode = CommonUtils.getNodeProperty(testObject, propertyPath);
    ObjectMapper mapper = new ObjectMapper();
    ObjectNode rootNode = mapper.createObjectNode();
    ObjectNode dataNode = rootNode.putObject("data");
    dataNode.put("foo", "fail");
    ObjectNode testNode = dataNode.putObject("test");
    testNode.put("foo", "success");
    JsonNode result = CommonUtils.getNodeProperty(rootNode, propertyPath);
    assertThat(result.asText(), is("success"));
  }

  @Test
  public void testUnmaskConfig() {
    ObjectNode config = prepareConfig();
    ObjectNode maskedData = CommonUtils.maskConfig(config);

    // 1. No changes.
    JsonNode unmaskedData = CommonUtils.unmaskJsonObject(config, maskedData);
    assertThat(unmaskedData, equalTo(config));

    // 2. Two fields changed.
    maskedData.put("MY_KEY_DATA", "SENSITIVE_DATA_2");
    maskedData.put("MY_PASSWORD", "SENSITIVE_DATA_2");
    unmaskedData = CommonUtils.unmaskJsonObject(config, maskedData);

    assertValue(unmaskedData, "SOME_KEY", "SENSITIVE_DATA");
    assertValue(unmaskedData, "KEY_DATA", "SENSITIVE_DATA");
    assertValue(unmaskedData, "SOME_SECRET", "SENSITIVE_DATA");
    assertValue(unmaskedData, "SECRET_DATA", "SENSITIVE_DATA");
    assertValue(unmaskedData, "MY_SECRET_DATA", "SENSITIVE_DATA");
    assertValue(unmaskedData, "DATA", "VALUE");
    assertValue(unmaskedData, "MY_KEY_DATA", "SENSITIVE_DATA_2");
    assertValue(unmaskedData, "MY_PASSWORD", "SENSITIVE_DATA_2");
  }

  @Test
  public void testUnmaskConfigTreatsMaskPlaceholderIncludingArrays() {
    ObjectNode original = Json.newObject();
    original.put("AZURE_STORAGE_SAS_TOKEN", "orig-sas-token-value");
    ArrayNode regions = Json.newArray();
    ObjectNode region0 = Json.newObject();
    region0.put("REGION", "westus2");
    region0.put("AZURE_STORAGE_SAS_TOKEN", "orig-nested-sas-token");
    regions.add(region0);
    original.set("REGION_LOCATIONS", regions);

    // Simulate a client echoing back the ****-masked values produced by maskConfig.
    String maskedTop =
        CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "orig-sas-token-value");
    String maskedNested =
        CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "orig-nested-sas-token");
    ObjectNode incoming = original.deepCopy();
    incoming.put("AZURE_STORAGE_SAS_TOKEN", maskedTop);
    ((ObjectNode) ((ArrayNode) incoming.get("REGION_LOCATIONS")).get(0))
        .put("AZURE_STORAGE_SAS_TOKEN", maskedNested);

    JsonNode merged =
        CommonUtils.unmaskJsonObject(original, incoming, CustomerConfig.ARRAY_IDENTITY_FIELDS);
    assertValue(merged, "AZURE_STORAGE_SAS_TOKEN", "orig-sas-token-value");
    assertEquals(
        "orig-nested-sas-token",
        merged.get("REGION_LOCATIONS").get(0).get("AZURE_STORAGE_SAS_TOKEN").asText());
    assertEquals("westus2", merged.get("REGION_LOCATIONS").get(0).get("REGION").asText());
  }

  @Test
  public void testUnmaskConfigNewArrayElementWithNoIdentityMatchKeptAsIs() {
    // A newly added region has no counterpart in the original; its real token must be stored as-is.
    ObjectNode original = Json.newObject();
    ArrayNode regions = Json.newArray();
    ObjectNode existingRegion = Json.newObject();
    existingRegion.put("REGION", "westus2");
    existingRegion.put("AZURE_STORAGE_SAS_TOKEN", "west-token");
    regions.add(existingRegion);
    original.set("REGION_LOCATIONS", regions);

    ObjectNode incoming = Json.newObject();
    ArrayNode incomingRegions = Json.newArray();
    ObjectNode existingIncoming = Json.newObject();
    existingIncoming.put("REGION", "westus2");
    existingIncoming.put(
        "AZURE_STORAGE_SAS_TOKEN",
        CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "west-token"));
    ObjectNode newRegion = Json.newObject();
    newRegion.put("REGION", "eastus");
    newRegion.put("AZURE_STORAGE_SAS_TOKEN", "brand-new-east-token");
    incomingRegions.add(existingIncoming);
    incomingRegions.add(newRegion);
    incoming.set("REGION_LOCATIONS", incomingRegions);

    JsonNode merged =
        CommonUtils.unmaskJsonObject(original, incoming, CustomerConfig.ARRAY_IDENTITY_FIELDS);
    JsonNode mergedRegions = merged.get("REGION_LOCATIONS");
    assertEquals("west-token", mergedRegions.get(0).get("AZURE_STORAGE_SAS_TOKEN").asText());
    // New region: no original match, real token kept as-is.
    assertEquals(
        "brand-new-east-token", mergedRegions.get(1).get("AZURE_STORAGE_SAS_TOKEN").asText());
  }

  @Test
  public void testUnmaskConfigArrayNotRestoredWithoutIdentityField() {
    // Without an identity field map, array elements are kept as-is (no restoration attempted).
    ObjectNode original = Json.newObject();
    ArrayNode regions = Json.newArray();
    ObjectNode region = Json.newObject();
    region.put("REGION", "westus2");
    region.put("AZURE_STORAGE_SAS_TOKEN", "orig-token");
    regions.add(region);
    original.set("REGION_LOCATIONS", regions);

    String masked = CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "orig-token");
    ObjectNode incoming = original.deepCopy();
    ((ObjectNode) ((ArrayNode) incoming.get("REGION_LOCATIONS")).get(0))
        .put("AZURE_STORAGE_SAS_TOKEN", masked);

    // No identity map: masked value is not restored.
    JsonNode merged = CommonUtils.unmaskJsonObject(original, incoming);
    assertEquals(
        masked, merged.get("REGION_LOCATIONS").get(0).get("AZURE_STORAGE_SAS_TOKEN").asText());
  }

  @Test
  public void testUnmaskConfigArrayReorderingDoesNotSwapTokens() {
    ObjectNode original = Json.newObject();
    ArrayNode regions = Json.newArray();
    ObjectNode east = Json.newObject();
    east.put("REGION", "eastus");
    east.put("AZURE_STORAGE_SAS_TOKEN", "east-token");
    ObjectNode west = Json.newObject();
    west.put("REGION", "westus2");
    west.put("AZURE_STORAGE_SAS_TOKEN", "west-token");
    regions.add(east);
    regions.add(west);
    original.set("REGION_LOCATIONS", regions);

    // Client reorders the array and sends ****-masked placeholders.
    ObjectNode incoming = Json.newObject();
    ArrayNode reordered = Json.newArray();
    ObjectNode incomingWest = Json.newObject();
    incomingWest.put("REGION", "westus2");
    incomingWest.put(
        "AZURE_STORAGE_SAS_TOKEN",
        CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "west-token"));
    ObjectNode incomingEast = Json.newObject();
    incomingEast.put("REGION", "eastus");
    incomingEast.put(
        "AZURE_STORAGE_SAS_TOKEN",
        CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "east-token"));
    reordered.add(incomingWest);
    reordered.add(incomingEast);
    incoming.set("REGION_LOCATIONS", reordered);

    Map<String, String> identityFields = CustomerConfig.ARRAY_IDENTITY_FIELDS;
    JsonNode merged = CommonUtils.unmaskJsonObject(original, incoming, identityFields);
    JsonNode mergedRegions = merged.get("REGION_LOCATIONS");
    // Tokens must follow REGION identity, not array position.
    assertEquals("westus2", mergedRegions.get(0).get("REGION").asText());
    assertEquals("west-token", mergedRegions.get(0).get("AZURE_STORAGE_SAS_TOKEN").asText());
    assertEquals("eastus", mergedRegions.get(1).get("REGION").asText());
    assertEquals("east-token", mergedRegions.get(1).get("AZURE_STORAGE_SAS_TOKEN").asText());
  }

  @Test
  public void testUnmaskConfigMaskPlaceholderDoesNotCrashWhenOriginalMissing() {
    ObjectNode original = Json.newObject();
    ObjectNode incoming = Json.newObject();
    String masked = CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "some-token-value");
    incoming.put("AZURE_STORAGE_SAS_TOKEN", masked);

    // Should not throw if original path doesn't exist; placeholder is kept as-is.
    JsonNode merged = CommonUtils.unmaskJsonObject(original, incoming);
    assertValue(merged, "AZURE_STORAGE_SAS_TOKEN", masked);
  }

  @Test
  public void testUnmaskConfigDoesNotRestoreWhenOriginalIsAlsoMasked() {
    // If the stored value is itself a ****-mask placeholder (e.g. previously clobbered by a bug),
    // don't restore it. Let the user provide a real token on next save.
    ObjectNode original = Json.newObject();
    String maskedPlaceholder = CommonUtils.getMaskedValue("AZURE_STORAGE_SAS_TOKEN", "old-token");
    original.put("AZURE_STORAGE_SAS_TOKEN", maskedPlaceholder);
    ObjectNode incoming = Json.newObject();
    incoming.put("AZURE_STORAGE_SAS_TOKEN", "new-real-token");

    JsonNode merged = CommonUtils.unmaskJsonObject(original, incoming);
    assertValue(merged, "AZURE_STORAGE_SAS_TOKEN", "new-real-token");
  }

  @Test
  public void testMaskComplexObject() {
    AlertChannelEmailParams params = new AlertChannelEmailParams();
    params.setRecipients(Collections.singletonList("test@test.com"));
    params.setSmtpData(EmailFixtures.createSmtpData());

    AlertChannelEmailParams maskedParams = CommonUtils.maskObject(params);
    assertThat(maskedParams, not(params));
    assertThat(maskedParams.getSmtpData().smtpPassword, not(params.getSmtpData().smtpPassword));

    AlertChannelEmailParams unmaskedParams = CommonUtils.unmaskObject(params, maskedParams);
    assertThat(unmaskedParams, not(maskedParams));
    assertThat(unmaskedParams, equalTo(params));
  }

  @Test
  public void testEncryptDecryptConfig() {
    UUID uuid = UUID.randomUUID();
    Map<String, String> config = new HashMap<>();
    config.put("key1", "value1");
    config.put("key2", "value2");
    Map<String, String> encryptedConfig = CommonUtils.encryptProviderConfig(config, uuid, "aws");
    assertTrue(encryptedConfig.containsKey("encrypted"));
    Map<String, String> decryptedConfig =
        CommonUtils.decryptProviderConfig(encryptedConfig, uuid, "aws");
    assertEquals(config, decryptedConfig);
  }

  @Test
  @Parameters({
    "1.2.3.4, 1.2.3.4, true",
    "1.2.3.4, 1.2.3.4wqerq, true",
    "1.2.3.4-b15, 1.2.3.4, true",
    "1.2.3.4-b15, 1.2.3.4wqerq, true",
    "1.2.3.4-b15, 1.2.3.4-b14, false",
    "1.2.3.4-b15, 1.2.3.4-b15, true",
    "1.2.3.4-b15, 1.2.3.4-b16, true",
    "1.2.3.3sdfdsf, 1.2.3.4wqerq, true",
    "1.2.3.5sdfdsf, 1.2.3.4wqerq, false",
    "1.2.2.6sdfdsf, 1.2.3.4wqerq, true",
    "1.2.4.1sdfdsf, 1.2.3.4wqerq, false",
    "1.2.4.1sdfdsf, asdfdsaf, true",
    "1.2.4.1-b1, 2024.1.0.0, true",
    "2024.1.0.0-b1, 2.21.0.0-b1, false",
    "2024.1.0.0-b1, 2024.1.0.0-b1, true",
    "2024.1.0.0-b1, 2024.2.0.0, true",
  })
  public void testReleaseEqualOrAfter(
      String thresholdRelease, String actualRelease, boolean result) {
    assertThat(CommonUtils.isReleaseEqualOrAfter(thresholdRelease, actualRelease), equalTo(result));
  }

  @Test
  @Parameters({
    "1.2.3.4, 1.2.3.4, false",
    "1.2.3.4, 1.2.3.4wqerq, false",
    "1.2.3.4-b15, 1.2.3.4, false",
    "1.2.3.4-b15, 1.2.3.4wqerq, false",
    "1.2.3.4-b15, 1.2.3.4-b14, true",
    "1.2.3.4-b15, 1.2.3.4-b15, false",
    "1.2.3.4-b15, 1.2.3.4-b16, false",
    "1.2.3.3sdfdsf, 1.2.3.4wqerq, false",
    "1.2.3.5sdfdsf, 1.2.3.4wqerq, true",
    "1.2.2.6sdfdsf, 1.2.3.4wqerq, false",
    "1.2.4.1sdfdsf, 1.2.3.4wqerq, true",
    "1.2.4.1sdfdsf, asdfdsaf, false",
    "2024.1.0.0-b1, 2.21.0.0-b1, true",
    "2024.1.0.0-b1, 2024.1.0.0-b1, false",
    "2024.1.0.0-b1, 2024.2.0.0, false",
  })
  public void testReleaseBefore(String thresholdRelease, String actualRelease, boolean result) {
    assertThat(CommonUtils.isReleaseBefore(thresholdRelease, actualRelease), equalTo(result));
  }

  @Abortable
  abstract static class Base {}

  @Retryable
  static class SubClass1 extends Base {}

  static class SubClass2 extends Base {}

  @Retryable(enabled = false)
  static class SubClass3 extends SubClass1 {}

  @Test
  public void testIsAnnotatedWith() {
    Optional<Abortable> op1 = CommonUtils.isAnnotatedWith(SubClass1.class, Abortable.class);
    assertEquals(true, op1.isPresent());
    assertEquals(true, op1.get().enabled());
    // On subclass
    op1 = CommonUtils.isAnnotatedWith(SubClass1.class, Abortable.class);
    assertEquals(true, op1.isPresent());
    assertEquals(true, op1.get().enabled());
    Optional<Retryable> op2 = CommonUtils.isAnnotatedWith(SubClass1.class, Retryable.class);
    assertEquals(true, op2.isPresent());
    assertEquals(true, op2.get().enabled());
    op2 = CommonUtils.isAnnotatedWith(SubClass2.class, Retryable.class);
    assertEquals(false, op2.isPresent());
    op2 = CommonUtils.isAnnotatedWith(SubClass3.class, Retryable.class);
    assertEquals(true, op2.isPresent());
    assertEquals(false, op2.get().enabled());
  }

  @Test
  public void testIsEqualIgnoringOrder() {
    assertTrue(
        CommonUtils.isEqualIgnoringOrder(
            Arrays.asList("a", "b", "c"), Arrays.asList("a", "b", "c")));
    assertTrue(
        CommonUtils.isEqualIgnoringOrder(
            Arrays.asList("a", "b", "c"), Arrays.asList("c", "a", "b")));
    assertTrue(CommonUtils.isEqualIgnoringOrder(Arrays.asList(), Arrays.asList()));
    assertTrue(CommonUtils.<List<?>>isEqualIgnoringOrder(null, null));
    assertFalse(
        CommonUtils.isEqualIgnoringOrder(Arrays.asList("a", "b", "c"), Arrays.asList("a", "b")));
    assertFalse(CommonUtils.isEqualIgnoringOrder(Arrays.asList("a", "b", "c"), null));
  }

  @Test
  public void testReplaceBeginningPathChanged() {
    String pathToModify = "/opt/yugaware/path/to/opt/yugaware/file";
    String initialRootPath = "/opt/yugaware";
    String finalRootPath = "/root/data";
    String expectedModifiedPath = "/root/data/path/to/opt/yugaware/file";

    String modifiedPath =
        CommonUtils.replaceBeginningPath(pathToModify, initialRootPath, finalRootPath);
    assertEquals(expectedModifiedPath, modifiedPath);
  }

  @Test
  public void testReplaceBeginningPathUnChanged() {
    String pathToModify = "/opt/yugaware/path/to/opt/path/file";
    String initialRootPath = "/opt/yugaware";
    String finalRootPath = "/opt/yugaware";

    String modifiedPath =
        CommonUtils.replaceBeginningPath(pathToModify, initialRootPath, finalRootPath);
    assertEquals(pathToModify, modifiedPath);
  }
}
