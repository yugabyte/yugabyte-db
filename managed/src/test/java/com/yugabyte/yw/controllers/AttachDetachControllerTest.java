// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static com.yugabyte.yw.common.ModelFactory.testUser;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.METHOD_NOT_ALLOWED;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsBytes;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.UniverseSpec;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.apache.pekko.stream.javadsl.FileIO;
import org.apache.pekko.stream.javadsl.Source;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes;
import org.yb.Schema;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

public class AttachDetachControllerTest extends FakeDBApplication {

  private RuntimeConfGetter confGetter;
  private Customer customer;
  private Users user;
  private String authToken;
  private String detachEndpoint;
  private String attachEndpoint;
  private String mainUniverseName;
  private UUID mainUniverseUUID;
  private Universe mainUniverse;

  private Provider defaultProvider;
  private Region defaultRegion;
  private AvailabilityZone defaultAZ1;
  private AvailabilityZone defaultAZ2;
  private AvailabilityZone defaultAZ3;
  private AccessKey defaultAccessKey;

  // Xcluster test setup.
  private String namespace1Name;
  private String namespace1Id;
  private String exampleTableID1;
  private String exampleTable1Name;
  private Set<String> exampleTables;
  private ObjectNode createXClusterRequestParams;

  @Before
  public void setUp() throws Exception {
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    customer = testCustomer();
    user = testUser(customer, Users.Role.SuperAdmin);
    authToken = user.createAuthToken();

    mainUniverseName = "AttachDetachController-test-universe";
    mainUniverse = createUniverse(mainUniverseName, customer.getId());
    mainUniverseUUID = mainUniverse.getUniverseUUID();
    detachEndpoint =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + mainUniverse.getUniverseUUID().toString()
            + "/export";

    attachEndpoint =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + mainUniverse.getUniverseUUID().toString()
            + "/import";

    String storagePath = confGetter.getStaticConf().getString("yb.storage.path");

    // Creating provider.
    ProviderDetails providerDetails = new ProviderDetails();
    defaultProvider =
        Provider.create(
            customer.getUuid(),
            UUID.randomUUID(),
            Common.CloudType.aws,
            "aws-test-provider",
            providerDetails);
    defaultRegion = Region.create(defaultProvider, "region-1", "region-1", "default-image");
    defaultAZ1 = AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone", "subnet-1");
    defaultAZ2 = AvailabilityZone.createOrThrow(defaultRegion, "az-3", "B Zone", "subnet-2");
    defaultAZ3 = AvailabilityZone.createOrThrow(defaultRegion, "az-2", "C Zone", "subnet-3");
    providerDetails = defaultProvider.getDetails();
    providerDetails.setCloudInfo(new ProviderDetails.CloudInfo());
    AWSCloudInfo awsCloudInfo = new AWSCloudInfo();
    awsCloudInfo.awsAccessKeyID = "awsAccessKeySecretDummyValue";
    awsCloudInfo.awsAccessKeySecret = "awsAccessKeySecretDummyValue";
    providerDetails.getCloudInfo().setAws(awsCloudInfo);
    defaultProvider.getRegions().add(defaultRegion);
    defaultProvider.save();
    defaultRegion.getZones().add(defaultAZ2);
    defaultRegion.getZones().add(defaultAZ3);
    defaultRegion.getZones().add(defaultAZ1);
    defaultRegion.save();

    // Create access key.
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    String accessKeyDir = String.format("%s/keys/%s", storagePath, defaultProvider.getUuid());
    keyInfo.privateKey =
        String.format("%s/keys/%s/private-key.pem", storagePath, defaultProvider.getUuid());
    keyInfo.publicKey =
        String.format("%s/keys/%s/public-key.pem", storagePath, defaultProvider.getUuid());
    keyInfo.vaultFile =
        String.format("%s/keys/%s/key.vault", storagePath, defaultProvider.getUuid());
    keyInfo.vaultPasswordFile =
        String.format("%s/keys/%s/key.vault_password", storagePath, defaultProvider.getUuid());
    Files.createDirectories(Paths.get(accessKeyDir));
    File privateKeyFile = new File(keyInfo.privateKey);
    privateKeyFile.createNewFile();
    File publicKeyFile = new File(keyInfo.publicKey);
    publicKeyFile.createNewFile();
    File vaultFile = new File(keyInfo.vaultFile);
    vaultFile.createNewFile();
    File vaultPasswordFile = new File(keyInfo.vaultPasswordFile);
    vaultPasswordFile.createNewFile();
    defaultAccessKey =
        AccessKey.create(defaultProvider.getUuid(), "key-aws-" + mainUniverseName, keyInfo);

    defaultProvider.getAllAccessKeys().add(defaultAccessKey);
    defaultProvider.save();

    String host = "1.2.3.4";
    HostAndPort hostAndPort = HostAndPort.fromParts(host, 9000);
    when(mockYBClient.getLeaderMasterHostAndPort()).thenReturn(hostAndPort);
    when(mockService.getClient(any(), any())).thenReturn(mockYBClient);
    doNothing().when(mockSwamperHelper).writeUniverseTargetJson(mainUniverse);
  }

  @Test
  public void testInvalidXClusterDetach() {
    UUID taskUUID = buildTaskInfo(null, TaskType.EditXClusterConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    // Set up simple xcluster config.
    String targetUniverseName = "AttachDetachController-test-universe-2";
    UUID targetUniverseUUID = UUID.randomUUID();
    Universe targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    String configName = "XClusterConfig";
    createXClusterRequestParams =
        Json.newObject()
            .put("name", configName)
            .put("sourceUniverseUUID", mainUniverseUUID.toString())
            .put("targetUniverseUUID", targetUniverseUUID.toString());

    namespace1Name = "ycql-namespace1";
    namespace1Id = UUID.randomUUID().toString();
    exampleTableID1 = "000030af000030008000000000004000";
    exampleTable1Name = "exampleTable1";
    exampleTables = new HashSet<>();
    exampleTables.add(exampleTableID1);

    ArrayNode tables = Json.newArray();
    for (String table : exampleTables) {
      tables.add(table);
    }
    createXClusterRequestParams.putArray("tables").addAll(tables);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient mockClient = mock(YBClient.class);
    when(mockService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate))
        .thenReturn(mockClient);

    GetTableSchemaResponse mockTableSchemaResponseTable1 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(Collections.emptyList()),
            namespace1Name,
            "exampleTableID1",
            exampleTableID1,
            null,
            true,
            CommonTypes.TableType.YQL_TABLE_TYPE,
            Collections.emptyList(),
            false);

    try {
      lenient()
          .when(mockClient.getTableSchemaByUUID(exampleTableID1))
          .thenReturn(mockTableSchemaResponseTable1);
    } catch (Exception ignored) {
    }

    String xClusterApiEndpoint = "/api/customers/" + customer.getUuid() + "/xcluster_configs";

    ListTablesResponse mockListTablesResponse = mock(ListTablesResponse.class);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList = new ArrayList<>();
    // Adding table 1.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table1TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table1TableInfoBuilder.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    table1TableInfoBuilder.setId(ByteString.copyFromUtf8(exampleTableID1));
    table1TableInfoBuilder.setName(exampleTable1Name);
    table1TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName(namespace1Name)
            .setId(ByteString.copyFromUtf8(namespace1Id))
            .build());
    tableInfoList.add(table1TableInfoBuilder.build());
    try {
      when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
      when(mockClient.getTablesList(eq(null), anyBoolean(), eq(null)))
          .thenReturn(mockListTablesResponse);
    } catch (Exception e) {
      e.printStackTrace();
    }

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("skipReleases", true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST", xClusterApiEndpoint, user.createAuthToken(), createXClusterRequestParams);
    assertOk(result);

    result = assertPlatformException(() -> detachUniverse(bodyJson));
    assertEquals(METHOD_NOT_ALLOWED, result.status());
  }

  @Test
  public void testInvalidUpdateInProgressDetach() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("skipReleases", true);
    Universe.saveDetails(
        mainUniverseUUID,
        (universe) -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.updateInProgress = true;
          universe.setUniverseDetails(universeDetails);
        });
    Result result = assertPlatformException(() -> detachUniverse(bodyJson));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testDetachSuccessAWS() throws Exception {

    // Add universe config as it has @JsonIgnore annotation.
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.provider = defaultProvider.getUuid().toString();
          userIntent.providerType = Common.CloudType.aws;
          universe.getUniverseDetails().upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
          universe.updateConfig(config);
        };
    mainUniverse = Universe.saveDetails(mainUniverse.getUniverseUUID(), updater);

    // Ignore software releases/ybc releases.
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("skipReleases", true);

    // Detach universe and save to tarball.
    Result result = detachUniverse(bodyJson);
    assertEquals(OK, result.status());
    byte[] detachUniverseContent = contentAsBytes(result, mat).toArray();
    String specName = UniverseSpec.generateSpecName(true);
    String tarFileBase = "/tmp/" + specName;
    String tarFileLocation = tarFileBase + ".tar.gz";
    File tarFile = new File(tarFileLocation);
    FileUtils.writeByteArrayToFile(tarFile, detachUniverseContent);
    mainUniverse = Universe.getOrBadRequest(mainUniverse.getUniverseUUID());

    // Extract tarball and validate required files exist.
    Util.extractFilesFromTarGZ(tarFile.toPath(), tarFileBase);
    File specFolder = new File(tarFileBase);
    File universeJsonFile = new File(tarFileBase + "/universe-spec.json");
    File accessKeyFolder = new File(tarFileBase + "/keys");
    assertTrue(specFolder.exists());
    assertTrue(universeJsonFile.exists());
    assertTrue(accessKeyFolder.exists() && accessKeyFolder.isDirectory());

    // Validate that universe is locked.
    assertEquals(true, mainUniverse.getUniverseDetails().updateInProgress);
    assertEquals(false, mainUniverse.getUniverseDetails().updateSucceeded);

    // Parse json file.
    ObjectMapper mapper = Json.mapper();
    JsonNode specJson = mapper.readTree(universeJsonFile);

    // Assert provider information is retained.
    JsonNode providerJson = specJson.get("provider");
    UUID expectedProviderUUID = defaultProvider.getUuid();
    AccessKey expectedAccessKey = defaultProvider.getAllAccessKeys().get(0);
    String expectedKeyCode = expectedAccessKey.getIdKey().keyCode;
    String expectedPublicKey = expectedAccessKey.getKeyInfo().publicKey;
    String expectedPrivateKey = expectedAccessKey.getKeyInfo().privateKey;
    String expectedVaultPassword = expectedAccessKey.getKeyInfo().vaultPasswordFile;
    String expectedVault = expectedAccessKey.getKeyInfo().vaultFile;
    assertEquals(expectedProviderUUID.toString(), providerJson.get("uuid").textValue());
    JsonNode cloudInfoJson = providerJson.get("details").get("cloudInfo");
    assertEquals(
        defaultProvider.getDetails().getCloudInfo().aws.awsAccessKeyID,
        cloudInfoJson.get("aws").get("awsAccessKeyID").textValue());
    assertEquals(
        defaultProvider.getDetails().getCloudInfo().aws.awsAccessKeySecret,
        cloudInfoJson.get("aws").get("awsAccessKeySecret").textValue());
    JsonNode accessKeyJson = providerJson.get("allAccessKeys").get(0);
    assertEquals(expectedKeyCode, accessKeyJson.get("idKey").get("keyCode").textValue());
    JsonNode keyInfoJson = accessKeyJson.get("keyInfo");
    assertEquals(expectedPublicKey, keyInfoJson.get("publicKey").textValue());
    assertEquals(expectedPrivateKey, keyInfoJson.get("privateKey").textValue());
    assertEquals(expectedVaultPassword, keyInfoJson.get("vaultPasswordFile").textValue());
    assertEquals(expectedVault, keyInfoJson.get("vaultFile").textValue());

    // Assert universe information.
    JsonNode universeJson = specJson.get("universe");
    assertEquals(
        mainUniverse.getUniverseUUID().toString(), universeJson.get("universeUUID").textValue());
    assertEquals(
        Boolean.valueOf(mainUniverse.getConfig().get("takeBackups")),
        specJson.get("universeConfig").get("takeBackups").asBoolean());
    assertEquals(
        mainUniverse.getUniverseDetails().updateInProgress,
        universeJson.get("universeDetails").get("updateInProgress").asBoolean());
    assertEquals(
        mainUniverse.getUniverseDetails().updateSucceeded,
        universeJson.get("universeDetails").get("updateSucceeded").asBoolean());

    // Assert source platform paths.
    JsonNode oldPlatformPaths = specJson.get("oldPlatformPaths");
    assertEquals(
        confGetter.getStaticConf().getString("yb.storage.path"),
        oldPlatformPaths.get("storagePath").textValue());
    assertEquals(
        confGetter.getStaticConf().getString("yb.releases.path"),
        oldPlatformPaths.get("releasesPath").textValue());
    assertEquals(
        confGetter.getStaticConf().getString("ybc.docker.release"),
        oldPlatformPaths.get("ybcReleasePath").textValue());
    assertEquals(
        confGetter.getStaticConf().getString("ybc.releases.path"),
        oldPlatformPaths.get("ybcReleasesPath").textValue());

    // Delete all existing entities and files.
    File expectedPublicKeyFile = new File(expectedPublicKey);
    File expectedPrivateKeyFile = new File(expectedPublicKey);
    File expectedVaultPasswordFile = new File(expectedVaultPassword);
    File expectedVaultFile = new File(expectedVault);
    expectedPublicKeyFile.delete();
    expectedPrivateKeyFile.delete();
    expectedVaultPasswordFile.delete();
    expectedVaultFile.delete();
    defaultProvider.delete();
    mainUniverse.delete();
    assertThrows(
        PlatformServiceException.class,
        () -> AccessKey.getOrBadRequest(expectedProviderUUID, expectedKeyCode));
    assertThrows(
        PlatformServiceException.class, () -> Provider.getOrBadRequest(expectedProviderUUID));
    assertThrows(PlatformServiceException.class, () -> Universe.getOrBadRequest(mainUniverseUUID));

    // Import universe spec tarball.
    List<Http.MultipartFormData.Part<Source<org.apache.pekko.util.ByteString, ?>>> bodyData =
        new ArrayList<>();
    Source<org.apache.pekko.util.ByteString, ?> uploadedFile = FileIO.fromFile(tarFile);
    bodyData.add(
        new Http.MultipartFormData.FilePart<>(
            "spec", "test.tar.gz", "application/gzip", uploadedFile));
    result = attachUniverse(bodyData);
    assertEquals(OK, result.status());

    // Verify that all files exist.
    assertTrue(expectedPublicKeyFile.exists());
    assertTrue(expectedPrivateKeyFile.exists());
    assertTrue(expectedVaultPasswordFile.exists());
    assertTrue(expectedVaultFile.exists());

    // Verify all entities exist.
    assertTrue(Universe.maybeGet(mainUniverseUUID).isPresent());
    assertTrue(Provider.maybeGet(expectedProviderUUID).isPresent());

    // Assert provider information is retained.
    Provider importedProvider = Provider.getOrBadRequest(expectedProviderUUID);
    assertEquals(importedProvider.getAllAccessKeys().size(), 1);
    AccessKey importedAccessKey = importedProvider.getAllAccessKeys().get(0);
    assertEquals(expectedKeyCode, importedAccessKey.getIdKey().keyCode);
    assertEquals(expectedPublicKey, importedAccessKey.getKeyInfo().publicKey);
    assertEquals(expectedPrivateKey, importedAccessKey.getKeyInfo().privateKey);
    assertEquals(expectedVaultPassword, importedAccessKey.getKeyInfo().vaultPasswordFile);
    assertEquals(expectedVault, importedAccessKey.getKeyInfo().vaultFile);
    // Provider sensitive information.
    assertEquals(
        defaultProvider.getDetails().getCloudInfo().aws.awsAccessKeyID,
        importedProvider.getDetails().getCloudInfo().aws.awsAccessKeyID);
    assertEquals(
        defaultProvider.getDetails().getCloudInfo().aws.awsAccessKeySecret,
        importedProvider.getDetails().getCloudInfo().aws.awsAccessKeySecret);

    // Assert universe information is retained.
    Universe importedUniverse = Universe.getOrBadRequest(mainUniverseUUID);
    assertEquals(
        Boolean.valueOf(mainUniverse.getConfig().get("takeBackups")),
        Boolean.valueOf(importedUniverse.getConfig().get("takeBackups")));
    assertEquals(false, importedUniverse.getUniverseDetails().updateInProgress);
    assertEquals(true, importedUniverse.getUniverseDetails().updateSucceeded);
  }

  private Result detachUniverse(JsonNode bodyJson) {
    return doRequestWithAuthTokenAndBody("POST", detachEndpoint, authToken, bodyJson);
  }

  private Result attachUniverse(
      List<Http.MultipartFormData.Part<Source<org.apache.pekko.util.ByteString, ?>>> bodyData) {
    return doRequestWithMultipartData("POST", attachEndpoint, bodyData, mat);
  }
}
