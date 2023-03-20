// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.services;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.Provider;
import java.io.File;
import java.io.IOException;
import java.util.Base64;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;
import play.Application;

@RunWith(MockitoJUnitRunner.class)
public class FileDataServiceTest extends FakeDBApplication {
  String TMP_STORAGE_PATH = "/tmp/yugaware_tests/" + getClass().getSimpleName();

  FileDataService fileDataService;
  SettableRuntimeConfigFactory runtimeConfigFactory;
  Customer customer;

  @Override
  protected Application provideApplication() {
    return super.provideApplication(
        ImmutableMap.of(
            "yb.fs_stateless.max_files_count_persist",
            100,
            "yb.fs_stateless.suppress_error",
            true,
            "yb.fs_stateless.disable_sync_db_to_fs_startup",
            false,
            "yb.fs_stateless.max_file_size_bytes",
            10000));
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    fileDataService = app.injector().instanceOf(FileDataService.class);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
  }

  @Before
  public void beforeTest() throws IOException {
    new File(TMP_STORAGE_PATH).mkdirs();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  @Test
  public void testSyncFileData() throws IOException {
    Provider p = ModelFactory.awsProvider(customer);
    String[] diskFileNames = {"testFile1.txt", "testFile2", "testFile3.root.crt"};
    for (String diskFileName : diskFileNames) {
      String filePath = "/keys/" + p.uuid + "/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    for (String diskFileName : diskFileNames) {
      String filePath = "/node-agent/" + customer.uuid + "/" + UUID.randomUUID() + "/0/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);

    String[] dbFileNames = {"testFile4.txt", "testFile5", "testFile6.root.crt"};
    for (String dbFileName : dbFileNames) {
      UUID parentUUID = UUID.randomUUID();
      String filePath = "/keys/" + parentUUID + "/" + dbFileName;
      String content = Base64.getEncoder().encodeToString(UUID.randomUUID().toString().getBytes());
      FileData.create(parentUUID, filePath, dbFileName, content);
    }
    for (String dbFileName : dbFileNames) {
      UUID parentUUID = UUID.randomUUID();
      String filePath = "/node-agent/" + customer.uuid + "/" + parentUUID + "/0/" + dbFileName;
      String content = Base64.getEncoder().encodeToString(UUID.randomUUID().toString().getBytes());
      FileData.create(parentUUID, filePath, dbFileName, content);
    }

    fileDataService.syncFileData(TMP_STORAGE_PATH, true);
    List<FileData> fd = FileData.getAll();
    assertEquals(fd.size(), 12);
    Collection<File> diskFiles = FileUtils.listFiles(new File(TMP_STORAGE_PATH), null, true);
    assertEquals(diskFiles.size(), 12);
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  @Test
  public void testSyncFileDataWithSuppressException() throws IOException {
    runtimeConfigFactory.globalRuntimeConf().setValue("yb.fs_stateless.max_file_size_bytes", "0");
    Provider p = ModelFactory.awsProvider(customer);
    String[] diskFileNames = {"testFile1.txt", "testFile2", "testFile3.root.crt"};
    for (String diskFileName : diskFileNames) {
      String filePath = "/keys/" + p.uuid + "/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);

    // No Exception should be thrown.
    List<FileData> fd = FileData.getAll();
    assertEquals(fd.size(), 0);

    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.fs_stateless.max_file_size_bytes", "10000");
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);
    fd = FileData.getAll();
    assertEquals(fd.size(), 3);
  }

  @Test(expected = Exception.class)
  public void testSyncFileDataThrowException() throws IOException {
    runtimeConfigFactory.globalRuntimeConf().setValue("yb.fs_stateless.suppress_error", "false");
    runtimeConfigFactory.globalRuntimeConf().setValue("yb.fs_stateless.max_file_size_bytes", "0");
    Provider p = ModelFactory.awsProvider(customer);
    String[] diskFileNames = {"testFile1.txt", "testFile2", "testFile3.root.crt"};
    for (String diskFileName : diskFileNames) {
      String filePath = "/keys/" + p.uuid + "/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);
  }
}
