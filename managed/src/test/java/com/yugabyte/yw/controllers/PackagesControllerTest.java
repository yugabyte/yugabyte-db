// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsBytes;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.OsType;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.PackagesRequestParams;
import com.yugabyte.yw.forms.PackagesRequestParams.ArchitectureType;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class PackagesControllerTest extends PlatformGuiceApplicationBaseTest {

  @Mock Config mockConfig;
  @Mock Commissioner mockCommissioner;
  private static String fakeYbcReleasesPath = "/tmp/yugaware_tests/ybc/releases";

  @Override
  protected Application provideApplication() {
    when(mockConfig.getString("ybc.releases.path")).thenReturn(fakeYbcReleasesPath);
    return new GuiceApplicationBuilder()
        .configure(testDatabase())
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(HealthChecker.class).toInstance(mock(HealthChecker.class)))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .build();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeYbcReleasesPath));
  }

  private Result fetchPackage(ObjectNode body) {
    String uri = "/api/fetch_package";
    return doRequestWithBodyAndWithoutAuthToken("POST", uri, body);
  }

  @Test
  public void testFetchYbcPackages() throws Exception {
    String buildNumber = "2.15.0.0-b18";
    String fakeYbcPackagePath = "ybc-" + buildNumber + "-linux-x86_64.tar.gz";

    String fakeYbcReleasePath =
        createTempFile(
            fakeYbcReleasesPath + "/" + buildNumber, fakeYbcPackagePath, "test-ybc-content");

    PackagesRequestParams params = new PackagesRequestParams();
    params.buildNumber = buildNumber;
    params.osType = OsType.LINUX;
    params.architectureType = ArchitectureType.X86_64;
    params.packageName = "ybc";
    params.archiveType = "tar.gz";

    ObjectMapper mapper = new ObjectMapper();
    ObjectNode body = mapper.valueToTree(params);
    Result result = fetchPackage(body);
    assertEquals(OK, result.status());
    try {
      // Read the byte array received from the server and compare with original
      byte[] fakeYbcContent = Files.readAllBytes(new File(fakeYbcReleasePath).toPath());
      byte[] actualYbcContent = contentAsBytes(result, mat).toArray();
      assertArrayEquals(fakeYbcContent, actualYbcContent);
    } catch (Exception e) {
      throw e;
    }
  }
}
