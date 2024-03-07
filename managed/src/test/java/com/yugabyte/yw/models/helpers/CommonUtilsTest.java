// Copyright (c) YugaByte, Inc.

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
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
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
