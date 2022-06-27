// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.Environment;

@RunWith(JUnitParamsRunner.class)
public class JsonFieldsValidatorTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock private Environment environment;

  private JsonFieldsValidator jsonFieldsValidator;

  @Before
  public void setup() throws IOException {
    List<String> acceptableKeys = new ArrayList<>();
    acceptableKeys.add("provider:");
    acceptableKeys.add("  azu:");
    acceptableKeys.add("    - AZURE_TENANT_ID");
    acceptableKeys.add("    - AWS_ACCESS_KEY_ID");
    acceptableKeys.add("");
    acceptableKeys.add("entity:");
    acceptableKeys.add("  entity_type:");
    acceptableKeys.add("    - key");
    when(environment.resourceAsStream(JsonFieldsValidator.ACCEPTABLE_KEYS_RESOURCE_NAME))
        .thenReturn(asStream(acceptableKeys));
    jsonFieldsValidator =
        new JsonFieldsValidator(app.injector().instanceOf(BeanValidator.class), environment);
  }

  @Test
  @Parameters({
    "provider:azu, AZURE_TENANT_ID;AWS_ACCESS_KEY_ID, false",
    "provider:azu, AZURE_TENANT_ID;AWS_ACCESS_KEY_ID;unknown, true",
    "provider:azu, AZURE_TENANT_ID;AWS_ACCESS_KEY_ID;key, true",
    "entity:entity_type, AWS_ACCESS_KEY_ID;key, true",
    "entity:entity_type, key, false",
  })
  public void testValidateFields(String masterKey, String keys, boolean isError) {
    String[] dataKeys = keys.split(";");
    Map<String, String> map = new HashMap<>();
    int index = 0;
    for (String key : dataKeys) {
      map.put(key, "value" + (index + 1));
    }
    if (isError) {
      assertThat(
          () -> jsonFieldsValidator.validateFields(masterKey, map),
          thrown(PlatformServiceException.class));
    } else {
      jsonFieldsValidator.validateFields(masterKey, map);
    }
  }

  private InputStream asStream(List<String> items) throws IOException {
    String fileName = createTempFile("file.txt", String.join("\n", items));
    File initialFile = new File(fileName);
    return new FileInputStream(initialFile);
  }
}
