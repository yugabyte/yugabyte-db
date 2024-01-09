// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.io.IOException;
import java.nio.file.Paths;
import java.text.ParseException;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YbaMetadataComponentTest extends FakeDBApplication {
  @Mock public BaseTaskDependencies mockBaseTaskDependencies;
  @Mock public Config mockConfig;

  @Mock ConfigHelper configHelper;
  @Mock ApiHelper apiHelper;

  private Universe universe;
  private Customer customer;
  public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-application_logs/";
  //   private String fakeSourceLogsPath = fakeSupportBundleBasePath + "logs/";
  private String fakeBundlePath =
      fakeSupportBundleBasePath + "yb-support-bundle-test-20220308000000.000-logs";

  @Before
  public void setUp() {
    // Setup fake temp log files, universe, customer
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    // List<String> fakeLogsList =
    //     Arrays.asList(
    //         "application-log-2022-03-05.gz",
    //         "application-log-2022-03-06.gz",
    //         "application-log-2022-03-07.gz",
    //         "application-log-2022-03-08.gz",
    //         "application.log");
    // for (String fileName : fakeLogsList) {
    //   File fakeFile = new File(fakeSourceLogsPath + fileName);
    //   if (!fakeFile.exists()) {
    //     createTempFile(fakeSourceLogsPath, fileName, "test-application-logs-content");
    //   }
    // }

    // Mock all the config invocations with fake data
    when(mockBaseTaskDependencies.getConfig()).thenReturn(mockConfig);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  @Test
  public void testDownloadComponentBetweenDates() throws IOException, ParseException {
    // Calling the download function
    YbaMetadataComponent ybaMetadataComponent =
        new YbaMetadataComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    ybaMetadataComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), null, null, null);

    // Checking if the directory has some files.
    File[] files = new File(fakeBundlePath + "/metadata/").listFiles();
    assertTrue(files.length > 0);
  }
}
