// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.YugawareProperty;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Base64;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.error.YAMLException;
import play.Application;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ConfigHelperTest extends FakeDBApplication {
  String TMP_STORAGE_PATH = "/tmp/yugaware_tests/" + getClass().getSimpleName();

  @InjectMocks ConfigHelper configHelper;

  @Mock Util util;

  @Mock Application application;

  Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  @Before
  public void beforeTest() throws IOException {
    new File(TMP_STORAGE_PATH).mkdirs();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  private InputStream asYamlStream(Map<String, Object> map) throws IOException {
    Yaml yaml = new Yaml();
    String fileName = createTempFile(TMP_STORAGE_PATH, "file.yml", yaml.dump(map));
    File initialFile = new File(fileName);
    return new FileInputStream(initialFile);
  }

  private InputStream asJsonStream(Map<String, Object> map) throws IOException {
    JsonNode jsonNode = Json.toJson(map);
    String fileName = createTempFile(TMP_STORAGE_PATH, "file.json", jsonNode.toString());
    File initialFile = new File(fileName);
    return new FileInputStream(initialFile);
  }

  @Test
  public void testloadSoftwareVersiontoDB() throws IOException {
    String configFile = "version_metadata.json";
    Map<String, Object> jsonMap = new HashMap();
    jsonMap.put("version_number", "1.1.1.1");
    jsonMap.put("build_number", "12345");
    when(application.resourceAsStream(configFile)).thenReturn(asJsonStream(jsonMap));
    configHelper.loadSoftwareVersiontoDB(application);
    assertEquals(
        ImmutableMap.of("version", "1.1.1.1-b12345"),
        configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion));
  }

  @Test
  public void testLoadConfigsToDBWithFile() throws IOException {
    Map<String, Object> map = new HashMap();
    map.put("config-1", "foo");
    map.put("config-2", "bar");

    for (ConfigHelper.ConfigType configType : ConfigHelper.ConfigType.values()) {
      when(application.classloader()).thenReturn(ClassLoader.getSystemClassLoader());
      when(application.resourceAsStream(configType.getConfigFile())).thenReturn(asYamlStream(map));
    }
    configHelper.loadConfigsToDB(application);

    for (ConfigHelper.ConfigType configType : ConfigHelper.ConfigType.values()) {
      if (configType.getConfigFile() != null) {
        assertEquals(map, configHelper.getConfig(configType));
      } else {
        assertTrue(configHelper.getConfig(configType).isEmpty());
      }
    }
  }

  @Test(expected = YAMLException.class)
  public void testLoadConfigsToDBWithoutFile() {
    when(application.classloader()).thenReturn(ClassLoader.getSystemClassLoader());
    configHelper.loadConfigsToDB(application);
  }

  @Test
  public void testLoadConfigToDB() {
    Map<String, Object> map = new HashMap();
    map.put("config-1", "foo");
    map.put("config-2", "bar");
    ConfigHelper.ConfigType configType = ConfigHelper.ConfigType.SoftwareReleases;
    configHelper.loadConfigToDB(configType, map);
    assertEquals(map, configHelper.getConfig(configType));
  }

  @Test
  public void testGetConfigWithData() {
    Map<String, Object> map = new HashMap();
    map.put("foo", "bar");
    ConfigHelper.ConfigType testConfig = ConfigHelper.ConfigType.AWSInstanceTypeMetadata;
    YugawareProperty.addConfigProperty(
        testConfig.toString(), Json.toJson(map), testConfig.getDescription());

    Map<String, Object> data = configHelper.getConfig(testConfig);
    assertThat(data.get("foo"), allOf(notNullValue(), equalTo("bar")));
  }

  @Test
  public void testGetConfigWithoutData() {
    ConfigHelper.ConfigType testConfig = ConfigHelper.ConfigType.AWSInstanceTypeMetadata;
    Map<String, Object> data = configHelper.getConfig(testConfig);
    assertTrue(data.isEmpty());
  }

  @Test
  public void testGetConfigWithNullValue() {
    ConfigHelper.ConfigType testConfig = ConfigHelper.ConfigType.AWSInstanceTypeMetadata;
    YugawareProperty.addConfigProperty(testConfig.toString(), null, testConfig.getDescription());
    Map<String, Object> data = configHelper.getConfig(testConfig);
    assertTrue(data.isEmpty());
  }

  @Test
  public void testGetRegionMetadata() {
    ConfigHelper.ConfigType awsRegionType = ConfigHelper.ConfigType.AWSRegionMetadata;
    YugawareProperty.addConfigProperty(
        awsRegionType.toString(),
        Json.parse("{\"region\": \"aws-data\"}"),
        awsRegionType.getDescription());
    ConfigHelper.ConfigType gcpRegionType = ConfigHelper.ConfigType.GCPRegionMetadata;
    YugawareProperty.addConfigProperty(
        gcpRegionType.toString(),
        Json.parse("{\"region\": \"gcp-data\"}"),
        gcpRegionType.getDescription());
    ConfigHelper.ConfigType dockerRegionType = ConfigHelper.ConfigType.DockerRegionMetadata;
    YugawareProperty.addConfigProperty(
        dockerRegionType.toString(),
        Json.parse("{\"region\": \"docker-data\"}"),
        dockerRegionType.getDescription());

    assertThat(
        configHelper.getRegionMetadata(Common.CloudType.aws).get("region"),
        allOf(notNullValue(), equalTo("aws-data")));
    assertThat(
        configHelper.getRegionMetadata(Common.CloudType.gcp).get("region"),
        allOf(notNullValue(), equalTo("gcp-data")));
    assertThat(
        configHelper.getRegionMetadata(Common.CloudType.docker).get("region"),
        allOf(notNullValue(), equalTo("docker-data")));
    assertTrue(configHelper.getRegionMetadata(Common.CloudType.onprem).isEmpty());
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
    configHelper.syncFileData(TMP_STORAGE_PATH, false);

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

    configHelper.syncFileData(TMP_STORAGE_PATH, true);
    List<FileData> fd = FileData.getAll();
    assertEquals(fd.size(), 12);
    Collection<File> diskFiles = FileUtils.listFiles(new File(TMP_STORAGE_PATH), null, true);
    assertEquals(diskFiles.size(), 12);
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }
}
