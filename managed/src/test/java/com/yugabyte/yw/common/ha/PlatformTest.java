/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.ha;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static junit.framework.TestCase.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.libs.Files.singletonTemporaryFileCreator;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeApi;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Users;
import io.ebean.DB;
import io.ebean.Database;
import io.ebean.MockHelper;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class PlatformTest extends FakeDBApplication {
  Customer customer;
  Users user;
  private String authToken;
  String clusterKey;
  Application remoteApp;
  PlatformInstance localInstance;
  PlatformInstance remoteInstance;
  Path backupDir;
  Path replicationDir;
  UUID localConfigUUID;

  @Rule public TemporaryFolder remoteStorage = new TemporaryFolder();

  private static final String LOCAL_ACME_ORG = "http://local.acme.org/";
  private static final String REMOTE_ACME_ORG = "http://remote.acme.org";
  private FakeApi fakeApi;
  Database localEBeanServer;

  private PlatformInstanceClientFactory mockPlatformInstanceClientFactory =
      mock(PlatformInstanceClientFactory.class);

  private ConfigHelper mockConfigHelper = mock(ConfigHelper.class);

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    return super.configureApplication(
        builder.overrides(
            bind(PlatformInstanceClientFactory.class)
                .toInstance(mockPlatformInstanceClientFactory)));
  }

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer, Users.Role.SuperAdmin);
    authToken = user.createAuthToken();
    localEBeanServer = DB.getDefault();
    fakeApi = new FakeApi(app, localEBeanServer);
    clusterKey = createClusterKey();
    localConfigUUID = createHAConfig(fakeApi, clusterKey);
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(true);
    localInstance = createPlatformInstance(localConfigUUID, LOCAL_ACME_ORG, true, true);
    remoteInstance = createPlatformInstance(localConfigUUID, REMOTE_ACME_ORG, false, false);
    backupDir =
        Paths.get(
            app.config().getString(AppConfigHelper.YB_STORAGE_PATH),
            PlatformReplicationHelper.BACKUP_DIR);
    Util.setYbaVersion("1.0.0.0-b1");
  }

  @After
  public void tearDown() throws IOException {
    com.yugabyte.yw.common.utils.FileUtils.listFiles(
            backupDir, PlatformReplicationHelper.BACKUP_FILE_PATTERN)
        .forEach(File::delete);
    backupDir.toFile().delete();
    stopRemoteApp();
  }

  // call start in test method instead of setup so that we can test cases where remote app
  // is not running
  FakeApi startRemoteApp() {
    remoteApp =
        provideApplication(
            ImmutableMap.of(
                "play.allowGlobalApplication",
                false,
                AppConfigHelper.YB_STORAGE_PATH,
                remoteStorage.getRoot().getAbsolutePath()));
    Helpers.start(remoteApp);
    mat = remoteApp.getWrappedApplication().materializer();
    Database remoteEBenServer = DB.getDefault();
    replicationDir =
        Paths.get(
            remoteApp.config().getString(AppConfigHelper.YB_STORAGE_PATH),
            PlatformReplicationHelper.REPLICATION_DIR);
    return new FakeApi(remoteApp, remoteEBenServer);
  }

  void stopRemoteApp() {
    if (remoteApp != null) {
      Helpers.stop(remoteApp);
      remoteApp = null;
    }
  }

  @Test
  public void testSendBackups() throws IOException {
    FakeApi remoteFakeApi = startRemoteApp();
    UUID remoteConfigUUID = createHAConfig(remoteFakeApi, clusterKey);
    setupProxyingApiHelper(remoteFakeApi, clusterKey);
    File fakeDump = createFakeDump();
    PlatformReplicationManager replicationManager =
        app.injector().instanceOf(PlatformReplicationManager.class);

    MockHelper.mock(localEBeanServer, true);

    assertTrue("sendBackup failed", replicationManager.sendBackup(remoteInstance));

    assertTrue(fakeDump.exists());
    assertUploadContents(fakeDump);

    Result listResult =
        remoteFakeApi.doRequest(
            "GET",
            "/api/settings/ha/config/"
                + remoteConfigUUID
                + "/backup/list?leader="
                + localInstance.getAddress());
    JsonNode jsonNode = Json.parse(contentAsString(listResult));
    assertEquals(1, jsonNode.size());
    assertEquals(fakeDump.getName(), jsonNode.get(0).asText());
  }

  private void assertUploadContents(File backupFile) throws IOException {
    String storagePath = remoteApp.config().getString(AppConfigHelper.YB_STORAGE_PATH);
    File uploadedFile =
        Paths.get(
                storagePath,
                PlatformReplicationHelper.REPLICATION_DIR,
                new URL(localInstance.getAddress()).getHost(),
                backupFile.getName())
            .toFile();
    assertTrue(uploadedFile.exists());
    String uploadedContents = FileUtils.readFileToString(uploadedFile, Charset.defaultCharset());
    assertTrue("Actual:" + uploadedContents, FileUtils.contentEquals(backupFile, uploadedFile));
  }

  private File createFakeDump() throws IOException {
    Random rng = new Random();
    backupDir.toFile().mkdirs();
    File file = backupDir.resolve("backup_" + Instant.now().toEpochMilli() + ".tgz").toFile();
    try (FileWriter out = new FileWriter(file)) {
      int sz = 1024 * 1024;
      byte[] arr = new byte[sz];
      rng.nextBytes(arr);
      out.write(new String(arr, StandardCharsets.UTF_8));
    }
    return file;
  }

  private PlatformInstance createPlatformInstance(
      UUID configUUID, String remoteAcmeOrg, boolean isLocal, boolean isLeader) {
    String uri = "/api/settings/ha/config/" + configUUID.toString() + "/instance";
    JsonNode body =
        Json.newObject()
            .put("address", remoteAcmeOrg)
            .put("is_local", isLocal)
            .put("is_leader", isLeader);
    Result createResult = fakeApi.doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    UUID instanceUUID = UUID.fromString(instanceJson.get("uuid").asText());
    return PlatformInstance.get(instanceUUID).orElse(null);
  }

  private String createClusterKey() {
    Result createClusterKeyResult = fakeApi.doRequest("GET", "/api/settings/ha/generate_key");
    assertOk(createClusterKeyResult);

    return Json.parse(contentAsString(createClusterKeyResult)).get("cluster_key").asText();
  }

  private UUID createHAConfig(FakeApi fakeApi, String clusterKey) {
    String uri = "/api/settings/ha/config";
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = fakeApi.doRequestWithBody("POST", uri, body);
    assertOk(createResult);
    JsonNode haConfigJson = Json.parse(contentAsString(createResult));
    return UUID.fromString(haConfigJson.get("uuid").asText());
  }

  private void setupProxyingApiHelper(FakeApi remoteFakeApi, String clusterKey) {
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(
            new PlatformInstanceClient(
                mockApiHelper, clusterKey, REMOTE_ACME_ORG, mockConfigHelper));
    when(mockApiHelper.multipartRequest(anyString(), anyMap(), anyList()))
        .thenAnswer(
            invocation -> {
              String url = invocation.getArgument(0);
              if (url.matches("(http://|https://).*//.*")) {
                throw new MalformedURLException("URL contains double slashes: " + url);
              }
              Map<String, String> headers = invocation.getArgument(1);
              List<Http.MultipartFormData.Part<Source<ByteString, ?>>> parts =
                  invocation.getArgument(2);
              Result result = sendBackupSyncRequest(remoteFakeApi, url, headers, parts);
              String strResult = Helpers.contentAsString(result);
              try {
                return new ObjectMapper().readTree(strResult);
              } catch (IOException ioException) {
                throw new RuntimeException(strResult);
              }
            });
  }

  private Result sendBackupSyncRequest(
      FakeApi remoteFakeApi,
      String uri,
      Map<String, String> headers,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> parts) {
    if (!uri.contains(REMOTE_ACME_ORG)) {
      throw new RuntimeException(uri);
    }
    uri = uri.replaceFirst(".*" + REMOTE_ACME_ORG, "");
    Http.RequestBuilder requestBuilder = fakeRequest().method("POST").uri(uri);
    headers.forEach(requestBuilder::header);
    requestBuilder.bodyMultipart(parts, singletonTemporaryFileCreator(), mat);
    return remoteFakeApi.route(requestBuilder);
  }
}
