// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static junit.framework.TestCase.assertNull;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.amazonaws.services.ec2.model.Image;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.certmgmt.CertificateHelperTest;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundle.ImageBundleType;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RegionDetails;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class CloudProviderEditTest extends CommissionerBaseTest {

  private Provider provider;
  private Users user;

  private Result editProvider(Provider provider, boolean validate) {
    return editProvider(provider, validate, false);
  }

  private Result editProvider(Provider provider, boolean validate, boolean ignoreValidationErrors) {
    return editProvider(Json.toJson(provider), validate, ignoreValidationErrors);
  }

  private Result editProvider(JsonNode providerJson, boolean validate) {
    return editProvider(providerJson, validate, false);
  }

  private Result editProvider(
      JsonNode providerJson, boolean validate, boolean ignoreValidationErrors) {
    String uuidStr = providerJson.get("uuid").asText();
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "PUT",
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + uuidStr
            + String.format(
                "/edit?validate=%s&ignoreValidationErrors=%s", validate, ignoreValidationErrors),
        user.createAuthToken(),
        providerJson);
  }

  private Result getProvider(UUID providerUUID) {
    return FakeApiHelper.doRequestWithAuthToken(
        app,
        "GET",
        "/api/customers/" + defaultCustomer.getUuid() + "/providers/" + providerUUID,
        user.createAuthToken());
  }

  private Result editRegion(UUID providerUUID, UUID regionUUID, JsonNode body) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/regions/%s",
            defaultCustomer.getUuid(), providerUUID, regionUUID);
    return doRequestWithBody("PUT", uri, body);
  }

  private Result editRegionV2(UUID providerUUID, UUID regionUUID, JsonNode body) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/provider_regions/%s",
            defaultCustomer.getUuid(), providerUUID, regionUUID);
    return doRequestWithBody("PUT", uri, body);
  }

  private Result addRegion(UUID providerUUID, JsonNode regionJson) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + defaultCustomer.getUuid() + "/providers/" + providerUUID + "/regions",
        user.createAuthToken(),
        regionJson);
  }

  private Result addRegionV2(UUID providerUUID, JsonNode regionJson) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + providerUUID
            + "/provider_regions",
        user.createAuthToken(),
        regionJson);
  }

  @Override
  public void setUp() {
    super.setUp();
    user = ModelFactory.testUser(defaultCustomer);
    factory.globalRuntimeConf().setValue(GlobalConfKeys.enableVMOSPatching.getKey(), "true");
    provider =
        Provider.create(
            defaultCustomer.getUuid(), Common.CloudType.aws, "test", new ProviderDetails());
    AccessKey.create(
        provider.getUuid(), AccessKey.getDefaultKeyCode(provider), new AccessKey.KeyInfo());
    Region region = Region.create(provider, "us-west-1", "us-west-1", "yb-image1");
    AvailabilityZone.createOrThrow(region, "r1z1", "zone 1 reg 1", "subnet 1");
    provider = Provider.getOrBadRequest(provider.getUuid());
    provider.setUsabilityState(Provider.UsabilityState.READY);
    provider.setLastValidationErrors(null);
    provider.save();
    Map<String, Object> regionMetadata =
        ImmutableMap.<String, Object>builder()
            .put("name", "Mock Region")
            .put("latitude", 36.778261)
            .put("longitude", -119.417932)
            .build();

    when(mockConfigHelper.getRegionMetadata(Common.CloudType.aws))
        .thenReturn(
            ImmutableMap.of(
                // AWS regions to use.
                "us-west-1", regionMetadata,
                "us-west-2", regionMetadata,
                "us-east-1", regionMetadata));

    when(mockConfigHelper.getConfig(ConfigHelper.ConfigType.GKEKubernetesRegionMetadata))
        .thenReturn(
            ImmutableMap.of(
                // GCP regions to use.
                "us-west1", regionMetadata,
                "us-west2", regionMetadata,
                "us-east1", regionMetadata));

    // If we don't use this the value is set to null, and we actually
    // depend on this value being non-null
    when(mockAccessManager.createKubernetesConfig(anyString(), anyMap(), anyBoolean()))
        .thenReturn("/tmp/some-fake-path-here/kubernetes.conf");
    when(mockCloudQueryHelper.getDefaultImage(any(Region.class), any()))
        .thenReturn("ybImage-default");
  }

  private void setUpCredsValidation(boolean valid) {
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPI.isValidCreds(any())).thenReturn(valid);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);
  }

  @Test
  public void testAddRegion() throws InterruptedException {
    setUpCredsValidation(true);
    Provider editProviderReq = Provider.getOrBadRequest(provider.getUuid());
    Region region = new Region();
    region.setProviderCode(provider.getCloudCode().name());
    region.setCode("us-west-2");
    region.setName("us-west-2");
    region.setVnetName("vnet");
    region.setSecurityGroupId("sg-1");
    region.setYbImage("yb-image2");
    AvailabilityZone zone = new AvailabilityZone();
    zone.setCode("z1 r2");
    zone.setName("zone 2");
    zone.setSubnet("subnet 2");
    region.setZones(Collections.singletonList(zone));
    editProviderReq.getRegions().add(region);

    UUID taskUUID = doEditProvider(editProviderReq, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Provider resultProvider = Provider.getOrBadRequest(provider.getUuid());
    assertEquals(
        new HashSet<>(Arrays.asList("us-west-1", "us-west-2")),
        resultProvider.getRegions().stream().map(r -> r.getCode()).collect(Collectors.toSet()));
    for (Region reg : resultProvider.getRegions()) {
      assertEquals(1, reg.getZones().size());
    }
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CloudRegionSetup);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CloudAccessKeySetup);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CloudInitializer);
  }

  @Test
  public void testAddAz() throws InterruptedException {
    setUpCredsValidation(true);
    Provider editProviderReq = Provider.getOrBadRequest(provider.getUuid());
    Region region = editProviderReq.getRegions().get(0);
    AvailabilityZone zone = new AvailabilityZone();
    zone.setCode("z2r1");
    zone.setName("zone 2");
    zone.setSubnet("subnet 2");
    region.getZones().add(zone);
    UUID taskUUID = doEditProvider(editProviderReq, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Provider resultProvider = Provider.getOrBadRequest(provider.getUuid());
    assertEquals(2, resultProvider.getRegions().get(0).getZones().size());
  }

  @Test
  public void testVersionFail() {
    setUpCredsValidation(true);
    Provider editProviderReq = Provider.getOrBadRequest(provider.getUuid());
    editProviderReq.setVersion(editProviderReq.getVersion() - 1);
    Region region = editProviderReq.getRegions().get(0);
    AvailabilityZone zone = new AvailabilityZone();
    zone.setCode("z2 r1");
    zone.setName("zone 2");
    zone.setSubnet("subnet 2");
    region.getZones().add(zone);
    verifyEditError(editProviderReq, false, "Provider has changed, please refresh and try again");
  }

  @Test
  public void testEditProviderDeleteRegion() throws InterruptedException {
    Provider editProviderReq = Provider.getOrBadRequest(provider.getUuid());
    editProviderReq.getRegions().get(0).setActive(false);
    UUID taskUUID = doEditProvider(editProviderReq, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Provider resultProvider = Provider.getOrBadRequest(provider.getUuid());
    assertTrue("Region is deleted", resultProvider.getRegions().isEmpty());
  }

  @Test
  public void testEditProviderModifyAZs() throws InterruptedException {
    Provider editProviderReq = Provider.getOrBadRequest(provider.getUuid());
    editProviderReq.getRegions().get(0).getZones().get(0).setSubnet("subnet-changed");
    UUID taskUUID = doEditProvider(editProviderReq, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Provider resultProvider = Provider.getOrBadRequest(provider.getUuid());
    assertEquals(
        "subnet-changed", resultProvider.getRegions().get(0).getZones().get(0).getSubnet());
  }

  @Test
  public void testAwsProviderDetailsEdit() throws InterruptedException {
    ProviderDetails details = provider.getDetails();
    details.sshUser = "test-user";
    details.setCloudInfo(new ProviderDetails.CloudInfo());
    details.getCloudInfo().aws = new AWSCloudInfo();
    details.getCloudInfo().aws.awsAccessKeyID = "Test AWS Access Key ID";
    details.getCloudInfo().aws.awsAccessKeySecret = "Test AWS Access Key Secret";
    provider.save();
    Result providerRes = getProvider(this.provider.getUuid());
    JsonNode providerJson = Json.parse(contentAsString(providerRes));
    ObjectNode detailsJson = (ObjectNode) providerJson.get("details");
    detailsJson.put("sshUser", "modified-ssh-user");
    ((ObjectNode) providerJson).set("details", detailsJson);
    Result result = editProvider(providerJson, false);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(this.provider.getUuid(), UUID.fromString(json.get("resourceUUID").asText()));
    waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    this.provider = Provider.getOrBadRequest(this.provider.getUuid());
    assertEquals("modified-ssh-user", this.provider.getDetails().sshUser);
    assertEquals(
        "Test AWS Access Key ID", this.provider.getDetails().getCloudInfo().aws.awsAccessKeyID);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testEditProviderKubernetesConfigEdit() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();
    k8sProvider.getDetails().getCloudInfo().kubernetes.setKubeConfigName("test2.conf");
    k8sProvider
        .getDetails()
        .getCloudInfo()
        .kubernetes
        .setKubeConfigContent(TestUtils.readResource("test-kubeconfig-updated.conf"));
    Result result = editProvider(k8sProvider, false);

    assertOk(result);
    assertAuditEntry(2, defaultCustomer.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(k8sProvider.getUuid(), UUID.fromString(json.get("resourceUUID").asText()));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    k8sProvider = Provider.getOrBadRequest(k8sProvider.getUuid());
    KubernetesInfo k8sInfo = CloudInfoInterface.get(k8sProvider);
    assertEquals("https://5.6.7.8", k8sInfo.getApiServerEndpoint());

    // Creat provider
    verify(mockAccessManager, times(1)).createKubernetesConfig(anyString(), anyMap(), eq(false));
    verify(mockAccessManager, times(2))
        .createKubernetesAuthDataFile(anyString(), anyString(), anyString(), eq(false));
    // Edit provider
    verify(mockAccessManager, times(1)).createKubernetesConfig(anyString(), anyMap(), eq(true));
    verify(mockAccessManager, times(2))
        .createKubernetesAuthDataFile(anyString(), anyString(), anyString(), eq(true));

    // Gets called twice as we do both create  and edit
    verify(mockPrometheusConfigManager, times(2)).updateK8sScrapeConfigs();
  }

  @Test
  public void testEditProviderWithAWSProviderType() throws InterruptedException {
    Result providerRes = getProvider(provider.getUuid());
    JsonNode bodyJson = Json.parse(contentAsString(providerRes));
    Provider provider = Json.fromJson(bodyJson, Provider.class);
    provider.getDetails().getCloudInfo().aws.awsHostedZoneId = "1234";
    mockDnsManagerListSuccess("test");
    Result result = editProvider(Json.toJson(provider), false);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(provider.getUuid(), UUID.fromString(json.get("resourceUUID").asText()));
    waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    verify(mockDnsManager, times(1)).listDnsRecord(any(), any());
    provider = Provider.getOrBadRequest(provider.getUuid());
    assertEquals("1234", provider.getDetails().getCloudInfo().aws.getAwsHostedZoneId());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testEditProviderKubernetes() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();
    k8sProvider.getDetails().getCloudInfo().kubernetes.setKubernetesStorageClass("slow");
    // Remove the hidden/masked fields which UI won't be sending us.
    k8sProvider.getDetails().getCloudInfo().kubernetes.setApiServerEndpoint(null);

    Result result = editProvider(k8sProvider, false);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(k8sProvider.getUuid(), UUID.fromString(json.get("resourceUUID").asText()));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(p);
    assertEquals("slow", config.get("STORAGE_CLASS"));
    assertAuditEntry(2, defaultCustomer.getUuid());

    // Shouldn't be any modification to kubeconfig and related fields
    verify(mockAccessManager, times(0)).createKubernetesConfig(anyString(), anyMap(), eq(true));
    verify(mockAccessManager, times(0))
        .createKubernetesAuthDataFile(anyString(), anyString(), anyString(), eq(true));
    KubernetesInfo k8sInfo = CloudInfoInterface.get(p);
    assertEquals("https://1.2.3.4", k8sInfo.getApiServerEndpoint());
  }

  @Test
  public void testModifyGCPProviderCredentials()
      throws InterruptedException, JsonProcessingException {
    ObjectMapper mapper = Json.mapper();
    Provider gcpProvider =
        Provider.create(
            defaultCustomer.getUuid(), Common.CloudType.gcp, "test", new ProviderDetails());

    gcpProvider.getDetails().setCloudInfo(new ProviderDetails.CloudInfo());
    GCPCloudInfo gcp = new GCPCloudInfo();
    gcpProvider.getDetails().getCloudInfo().setGcp(gcp);
    gcp.setGceProject("gce_proj");
    JsonNode gceAppicationCredentials = Json.newObject().put("GCE_EMAIL", "test@yugabyte.com");
    gcp.setGceApplicationCredentials(mapper.writeValueAsString(gceAppicationCredentials));
    gcpProvider.save();
    ((ObjectNode) gceAppicationCredentials).put("client_id", "Client ID");
    gcpProvider
        .getDetails()
        .getCloudInfo()
        .getGcp()
        .setGceApplicationCredentials(mapper.writeValueAsString(gceAppicationCredentials));
    UUID taskUUID = doEditProvider(gcpProvider, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    gcpProvider = Provider.getOrBadRequest(gcpProvider.getUuid());
    JsonNode creds =
        mapper.readTree(
            gcpProvider.getDetails().getCloudInfo().getGcp().getGceApplicationCredentials());

    assertEquals("Client ID", creds.get("client_id").asText());
  }

  @Test
  public void testEditProviderValidationFail() throws InterruptedException {
    setUpCredsValidation(false);
    verifyEditError(provider, true, "Invalid AWS Credentials");
  }

  @Test
  public void testEditProviderValidationOk() throws InterruptedException {
    provider.setLastValidationErrors(Json.newObject().put("error", "something wrong"));
    provider.setUsabilityState(Provider.UsabilityState.ERROR);
    provider.save();
    provider.setName("new name");
    provider.setAllAccessKeys(createTempAccesskeys());
    Image image = new Image();
    image.setArchitecture("x86_64");
    image.setRootDeviceType("ebs");
    image.setPlatformDetails("linux/UNIX");
    when(mockAWSCloudImpl.describeImageOrBadRequest(any(), any(), anyString())).thenReturn(image);
    UUID taskUUID = doEditProvider(provider, true);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());
    Provider newProvider = Provider.getOrBadRequest(provider.getUuid());
    assertEquals("new name", newProvider.getName());
    assertEquals(Provider.UsabilityState.READY, newProvider.getUsabilityState());
    assertNull(newProvider.getLastValidationErrors());
  }

  @Test
  public void testValidationOKToError() throws InterruptedException {
    assertEquals(Provider.UsabilityState.READY, provider.getUsabilityState());
    assertNull(provider.getLastValidationErrors());
    provider.setName("new name");
    createImageBundle(provider, null);
    when(mockAWSCloudImpl.describeImageOrBadRequest(any(), any(), anyString()))
        .thenThrow(
            new PlatformServiceException(BAD_REQUEST, "AMI details extraction failed: Not found"));
    UUID taskUUID = doEditProvider(provider, true, true);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());
    provider = Provider.getOrBadRequest(provider.getUuid());
    assertNotNull(provider.getLastValidationErrors());
    assertEquals(
        Json.parse("[\"AMI details extraction failed: Not found\"]"),
        provider.getLastValidationErrors().get("error").get("REGION.us-west-1.IMAGE.test-image"));
    assertEquals(Provider.UsabilityState.READY, provider.getUsabilityState());
    assertEquals("new name", provider.getName());
  }

  @Test
  public void testAwsProviderDetailsEditMaskedKeys() throws InterruptedException {
    ProviderDetails details = provider.getDetails();
    details.sshUser = "test-user";
    details.setCloudInfo(new ProviderDetails.CloudInfo());
    details.getCloudInfo().aws = new AWSCloudInfo();
    details.getCloudInfo().aws.awsAccessKeyID = "Test AWS Access Key ID";
    details.getCloudInfo().aws.awsAccessKeySecret = "Test AWS Access Key Secret";
    provider.save();
    Provider editProviderReq = Provider.getOrBadRequest(provider.getUuid());
    JsonNode providerJson = Json.toJson(editProviderReq);
    ObjectNode detailsJson = (ObjectNode) providerJson.get("details");
    ObjectNode cloudInfo = (ObjectNode) detailsJson.get("cloudInfo");
    ObjectNode aws = (ObjectNode) cloudInfo.get("aws");
    cloudInfo.set("aws", aws);
    detailsJson.set("cloudInfo", cloudInfo);
    aws.put("awsAccessKeyID", "Updated AWS Access Key ID");
    ((ObjectNode) providerJson).set("details", detailsJson);
    Result result = editProvider(providerJson, false);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    UUID taskUUID = UUID.fromString(json.get("taskUUID").asText());
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    provider = Provider.getOrBadRequest(provider.getUuid());
    assertEquals(
        "Updated AWS Access Key ID", provider.getDetails().getCloudInfo().aws.awsAccessKeyID);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testK8sProviderEditDetails() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    p.getDetails().getCloudInfo().getKubernetes().setKubernetesProvider("GKE2");

    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    p = Provider.getOrBadRequest(p.getUuid());
    assertEquals(p.getDetails().getCloudInfo().getKubernetes().getKubernetesProvider(), "GKE2");
  }

  @Test
  public void testK8sProviderEditAddZone() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    ObjectNode providerJson = (ObjectNode) Json.toJson(p);
    JsonNode region = providerJson.get("regions").get(0);
    ArrayNode zones = (ArrayNode) region.get("zones");

    ObjectNode zone = Json.newObject();
    zone.put("name", "Zone 2");
    zone.put("code", "zone-2");
    zones.add(zone);
    ((ObjectNode) region).set("zones", zones);
    ArrayNode regionsNode = Json.newArray();
    regionsNode.add(region);
    providerJson.set("regions", regionsNode);
    p = Provider.getOrBadRequest(k8sProvider.getUuid());
    assertEquals(1, p.getRegions().get(0).getZones().size());

    Result result = editProvider(providerJson, false);
    JsonNode json = Json.parse(contentAsString(result));
    UUID taskUUID = UUID.fromString(json.get("taskUUID").asText());
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    p = Provider.getOrBadRequest(p.getUuid());

    assertEquals(2, p.getRegions().get(0).getZones().size());
    assertEquals("zone-2", p.getRegions().get(0).getZones().get(1).getCode());

    // Gets called twice as we do both create  and edit
    verify(mockPrometheusConfigManager, times(2)).updateK8sScrapeConfigs();
  }

  @Test
  public void testK8sProviderEditModifyZone() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    assertEquals(1, p.getRegions().get(0).getZones().size());
    AvailabilityZone az = p.getRegions().get(0).getZones().get(0);
    AvailabilityZoneDetails details = az.getDetails();
    details.setCloudInfo(new AvailabilityZoneDetails.AZCloudInfo());
    details.getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    details.getCloudInfo().getKubernetes().setKubernetesStorageClass("Storage class");

    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    p = Provider.getOrBadRequest(p.getUuid());

    assertEquals(1, p.getRegions().get(0).getZones().size());
    assertEquals(
        "Storage class",
        p.getRegions()
            .get(0)
            .getZones()
            .get(0)
            .getDetails()
            .getCloudInfo()
            .getKubernetes()
            .getKubernetesStorageClass());
  }

  @Test
  public void testK8sProviderDeleteZone() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    assertEquals(1, p.getRegions().get(0).getZones().size());
    AvailabilityZone az = p.getRegions().get(0).getZones().get(0);
    az.setActive(false);

    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    p = Provider.getOrBadRequest(p.getUuid());

    List<AvailabilityZone> azs = AvailabilityZone.getAZsForRegion(p.getRegions().get(0).getUuid());

    assertEquals(0, azs.size());
  }

  @Test
  public void testK8sProviderAddRegion() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    ObjectNode providerJson = (ObjectNode) Json.toJson(p);
    ArrayNode regions = (ArrayNode) providerJson.get("regions");

    ObjectNode region = Json.newObject();
    region.put("name", "Region 2");
    region.put("code", "us-west2");

    ArrayNode zones = Json.newArray();
    ObjectNode zone = Json.newObject();
    zone.put("name", "Zone 2");
    zone.put("code", "zone-2");
    zones.add(zone);
    region.set("zones", zones);

    regions.add(region);
    providerJson.set("regions", regions);

    Result result = editProvider(providerJson, false);
    JsonNode json = Json.parse(contentAsString(result));
    UUID taskUUID = UUID.fromString(json.get("taskUUID").asText());
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    p = Provider.getOrBadRequest(p.getUuid());

    assertEquals(2, p.getRegions().size());
    assertEquals("us-west2", p.getRegions().get(1).getCode());
    assertEquals("zone-2", p.getRegions().get(1).getZones().get(0).getCode());
  }

  @Test
  public void testK8sProviderEditModifyRegion() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();
    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    Region region = p.getRegions().get(0);
    region.setDetails(new RegionDetails());
    region.getDetails().setCloudInfo(new RegionDetails.RegionCloudInfo());
    region.getDetails().getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    region
        .getDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("Updating storage class");

    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    p = Provider.getOrBadRequest(p.getUuid());
    assertEquals(1, p.getRegions().size());
    assertEquals(
        "Updating storage class",
        p.getRegions()
            .get(0)
            .getDetails()
            .getCloudInfo()
            .getKubernetes()
            .getKubernetesStorageClass());
  }

  @Test
  public void testK8sProviderConfigAtMultipleLevels() {
    Provider k8sProvider = createK8sProvider();

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    p.getDetails().getCloudInfo().getKubernetes().setKubeConfigName("Test-1");
    AvailabilityZoneDetails details = p.getRegions().get(0).getZones().get(0).getDetails();

    details.setCloudInfo(new AvailabilityZoneDetails.AZCloudInfo());
    details.getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    details.getCloudInfo().getKubernetes().setKubeConfigName("Test-2");

    verifyEditError(p, false, "Kubeconfig can't be at two levels");
  }

  @Test
  public void testK8sProviderConfigEditAtZoneLevel() throws InterruptedException {
    Provider k8sProvider = createK8sProvider(false);
    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    KubernetesRegionInfo k8sRegInfo =
        p.getRegions().get(0).getZones().get(0).getDetails().getCloudInfo().getKubernetes();
    k8sRegInfo.setKubeConfigName("test2.conf");
    k8sRegInfo.setKubeConfigContent(TestUtils.readResource("test-kubeconfig-updated.conf"));

    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    p = Provider.getOrBadRequest(p.getUuid());

    k8sRegInfo =
        p.getRegions().get(0).getZones().get(0).getDetails().getCloudInfo().getKubernetes();

    assertNull(k8sRegInfo.getKubeConfigName());
    assertNotNull(k8sRegInfo.getKubeConfig());
    assertEquals("https://5.6.7.8", k8sRegInfo.getApiServerEndpoint());

    verify(mockAccessManager, times(1)).createKubernetesConfig(anyString(), anyMap(), eq(true));
    verify(mockAccessManager, times(2))
        .createKubernetesAuthDataFile(anyString(), anyString(), anyString(), eq(true));
  }

  @Test
  public void testK8sProviderConfigEditAtProviderLevel() throws InterruptedException {
    Provider k8sProvider = createK8sProvider();

    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    p.getDetails().getCloudInfo().getKubernetes().setKubernetesStorageClass("Test-2");

    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    p = Provider.getOrBadRequest(p.getUuid());
    assertEquals(
        "Test-2", p.getDetails().getCloudInfo().getKubernetes().getKubernetesStorageClass());
  }

  @Test
  public void testK8sProviderEditMaskedFieldsAtZone() throws InterruptedException {
    Provider k8sProvider = createK8sProvider(false);

    // Add config at zone
    Provider p = Provider.getOrBadRequest(k8sProvider.getUuid());
    KubernetesRegionInfo k8sRegInfo =
        p.getRegions().get(0).getZones().get(0).getDetails().getCloudInfo().getKubernetes();
    k8sRegInfo.setKubeConfigName("test1.conf");
    k8sRegInfo.setKubeConfigContent(TestUtils.readResource("test-kubeconfig.conf"));
    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    // Modify the zone
    p = Provider.getOrBadRequest(p.getUuid());
    k8sRegInfo =
        p.getRegions().get(0).getZones().get(0).getDetails().getCloudInfo().getKubernetes();
    k8sRegInfo.setKubernetesStorageClass("new-sc");
    // Remove the hidden/masked fields which UI won't be sending us.
    k8sRegInfo.setApiServerEndpoint(null);
    taskUUID = doEditProvider(p, false);
    taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    // Verify masked fields
    p = Provider.getOrBadRequest(p.getUuid());
    k8sRegInfo =
        p.getRegions().get(0).getZones().get(0).getDetails().getCloudInfo().getKubernetes();
    assertEquals("https://1.2.3.4", k8sRegInfo.getApiServerEndpoint());

    // Should happen only when we add kubeconfig to the zone, not the
    // second time when we modify the storage class.
    verify(mockAccessManager, times(1)).createKubernetesConfig(anyString(), anyMap(), eq(true));
    verify(mockAccessManager, times(2))
        .createKubernetesAuthDataFile(anyString(), anyString(), anyString(), eq(true));
  }

  @Test
  public void testProviderNameChangeWithExistingName() {
    Provider p = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws);
    Provider p2 = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws, "aws-2");
    Result providerRes = getProvider(p.getUuid());
    ObjectNode bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    bodyJson.put("name", "aws-2");
    Result result = assertPlatformException(() -> editProvider(bodyJson, false));
    assertBadRequest(result, "Provider with name aws-2 already exists.");
  }

  @Test
  public void testImageBundleEditProvider() throws InterruptedException {
    Provider p = ModelFactory.newProvider(defaultCustomer, Common.CloudType.gcp);
    Region.create(p, "us-west-1", "us-west-1", "yb-image1");
    ImageBundleDetails details = new ImageBundleDetails();
    Map<String, ImageBundleDetails.BundleInfo> regionImageInfo = new HashMap<>();
    regionImageInfo.put("us-west-1", new ImageBundleDetails.BundleInfo());
    details.setRegions(regionImageInfo);
    details.setArch(Architecture.x86_64);
    details.setGlobalYbImage("yb_image");
    ImageBundle.create(p, "ib-1", details, true);

    Result providerRes = getProvider(p.getUuid());
    JsonNode bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    p = Json.fromJson(bodyJson, Provider.class);
    ImageBundle ib = new ImageBundle();
    ib.setName("ib-2");
    ib.setProvider(p);
    ib.setDetails(details);

    List<ImageBundle> ibs = p.getImageBundles();
    ibs.add(ib);
    p.setImageBundles(ibs);
    UUID taskUUID = doEditProvider(p, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    p = Provider.getOrBadRequest(p.getUuid());
    assertEquals(2, p.getImageBundles().size());
    p.getImageBundles()
        .forEach(
            bundle -> {
              if (bundle.getName().equals("ib-2")) {
                assertEquals(false, bundle.getUseAsDefault());
              } else {
                assertEquals(true, bundle.getUseAsDefault());
              }
            });
  }

  @Test
  public void testWaitForFinishingTasksTimeout() throws InterruptedException {
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.waitForProviderTasksStepMs.getKey(), "50");
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.waitForProviderTasksTimeoutMs.getKey(), "100");
    UUID backupTaskUUID = UUID.randomUUID();
    CreateBackup createBackup = app.injector().instanceOf(CreateBackup.class);
    BackupRequestParams params = new BackupRequestParams();
    Universe universe = ModelFactory.createUniverse("univ", defaultCustomer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(),
        univ -> {
          univ.getUniverseDetails().getPrimaryCluster().userIntent.provider =
              provider.getUuid().toString();
        });
    params.setUniverseUUID(universe.getUniverseUUID());
    providerEditRestrictionManager.onTaskCreated(backupTaskUUID, createBackup, params);
    TaskInfo backupTaskInfo = new TaskInfo(TaskType.BackupUniverse, null);
    backupTaskInfo.setTaskState(TaskInfo.State.Running);
    backupTaskInfo.setTaskUUID(backupTaskUUID);
    backupTaskInfo.setTaskParams(Json.newObject());
    backupTaskInfo.setOwner("Myself");
    backupTaskInfo.save();
    ScheduleTask.create(backupTaskUUID, UUID.randomUUID());
    Collection<UUID> tasksInUse = providerEditRestrictionManager.getTasksInUse(provider.getUuid());
    assertFalse("Not empty task in use", tasksInUse.isEmpty());
    provider.setName("new name");
    UUID taskUUID = doEditProvider(provider, false, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Failure, taskInfo.getTaskState());
    assertNotEquals("new name", Provider.getOrBadRequest(provider.getUuid()).getName());
    assertTrue(taskInfo.getErrorMessage().contains("Reached timeout of 100 ms"));
  }

  @Test
  public void testWaitForFinishingTasksSuccess() throws InterruptedException {
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.waitForProviderTasksStepMs.getKey(), "100");
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.waitForProviderTasksTimeoutMs.getKey(), "10000");
    UUID backupTaskUUID = UUID.randomUUID();
    CreateBackup createBackup = app.injector().instanceOf(CreateBackup.class);
    BackupRequestParams params = new BackupRequestParams();
    Universe universe = ModelFactory.createUniverse("univ", defaultCustomer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(),
        univ -> {
          univ.getUniverseDetails().getPrimaryCluster().userIntent.provider =
              provider.getUuid().toString();
        });
    params.setUniverseUUID(universe.getUniverseUUID());
    providerEditRestrictionManager.onTaskCreated(backupTaskUUID, createBackup, params);
    TaskInfo backupTaskInfo = new TaskInfo(TaskType.BackupUniverse, null);
    backupTaskInfo.setTaskState(TaskInfo.State.Running);
    backupTaskInfo.setTaskUUID(backupTaskUUID);
    backupTaskInfo.setTaskParams(Json.newObject());
    backupTaskInfo.setOwner("Myself");
    backupTaskInfo.save();
    ScheduleTask.create(backupTaskUUID, UUID.randomUUID());
    Collection<UUID> tasksInUse = providerEditRestrictionManager.getTasksInUse(provider.getUuid());
    assertFalse("Not empty task in use", tasksInUse.isEmpty());
    provider.setName("new name");
    UUID taskUUID = doEditProvider(provider, false, false);
    Thread.sleep(200);
    backupTaskInfo.setTaskState(Success);
    backupTaskInfo.save();
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals("new name", Provider.getOrBadRequest(provider.getUuid()).getName());
  }

  @Test
  public void testImageBundleEditViaProviderRegionAdd() throws InterruptedException {
    factory.globalRuntimeConf().setValue(GlobalConfKeys.enableVMOSPatching.getKey(), "false");
    factory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.disableImageBundleValidation.getKey(), "true");
    Provider awsProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws);
    Region.create(awsProvider, "us-west-1", "us-west-1", "yb-image1");
    ImageBundle.Metadata metadata = new ImageBundle.Metadata();
    metadata.setType(ImageBundleType.YBA_ACTIVE);
    createImageBundle(awsProvider, metadata);

    Result providerRes = getProvider(awsProvider.getUuid());
    JsonNode bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    awsProvider = Json.fromJson(bodyJson, Provider.class);

    // Add a region to the provider
    awsProvider.getRegions().add(Json.fromJson(getAWSRegionJson(), Region.class));
    UUID taskUUID = doEditProvider(awsProvider, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    awsProvider = Provider.getOrBadRequest(awsProvider.getUuid());
    ImageBundle ib1 = awsProvider.getImageBundles().get(0);
    Map<String, ImageBundleDetails.BundleInfo> bundleInfoMap = ib1.getDetails().getRegions();
    assertEquals(2, bundleInfoMap.size());
    assertTrue(bundleInfoMap.keySet().contains("us-west-2"));

    // Edit an existing region.
    Region region = Region.getByCode(awsProvider, "us-west-2");
    region.getDetails().getCloudInfo().getAws().setYbImage("Updated YB Image");
    List<Region> regions = ImmutableList.of(region);
    awsProvider.setRegions(regions);
    taskUUID = doEditProvider(awsProvider, false);
    taskInfo = waitForTask(taskUUID);
    awsProvider = Provider.getOrBadRequest(awsProvider.getUuid());
    ib1 = awsProvider.getImageBundles().get(0);
    bundleInfoMap = ib1.getDetails().getRegions();
    assertEquals(2, bundleInfoMap.size());
    assertTrue(bundleInfoMap.keySet().contains("us-west-2"));
    ImageBundleDetails.BundleInfo bInfo = ib1.getDetails().getRegions().get("us-west-2");
    assertEquals("Updated YB Image", bInfo.getYbImage());
  }

  @Test
  public void testFailEditProviderRegionAddCustomBundle() throws InterruptedException {
    Provider awsProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws);
    Region.create(awsProvider, "us-west-1", "us-west-1", "yb-image1");
    ImageBundleDetails details = new ImageBundleDetails();
    Map<String, ImageBundleDetails.BundleInfo> regionImageInfo = new HashMap<>();
    regionImageInfo.put("us-west-1", new ImageBundleDetails.BundleInfo());
    details.setRegions(regionImageInfo);
    details.setArch(Architecture.x86_64);
    ImageBundle.Metadata metadata = new ImageBundle.Metadata();
    metadata.setType(ImageBundleType.CUSTOM);
    ImageBundle iB = ImageBundle.create(awsProvider, "ib-1", details, metadata, true);

    Result providerRes = getProvider(awsProvider.getUuid());
    JsonNode bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    awsProvider = Json.fromJson(bodyJson, Provider.class);

    // Add a region to the provider
    awsProvider.getRegions().add(Json.fromJson(getAWSRegionJson(), Region.class));
    Provider updatedProvider = awsProvider;
    Result result = assertPlatformException(() -> editProvider(updatedProvider, false));
    assertBadRequest(result, "Specify the AMI for the region us-west-2 in the image bundle ib-1");

    regionImageInfo.put("us-west-2", new ImageBundleDetails.BundleInfo());
    iB.getDetails().setRegions(regionImageInfo);

    awsProvider.setImageBundles(ImmutableList.of(iB));
    UUID taskUUID = doEditProvider(awsProvider, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    awsProvider = Provider.getOrBadRequest(awsProvider.getUuid());

    Map<String, ImageBundleDetails.BundleInfo> bundleInfoMap = iB.getDetails().getRegions();
    assertEquals(2, bundleInfoMap.size());
    assertTrue(bundleInfoMap.keySet().contains("us-west-2"));
  }

  @Test
  public void testEditProviderRegionAddYBADeprecatedBundle() throws InterruptedException {
    factory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.disableImageBundleValidation.getKey(), "true");
    Provider awsProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws);
    Region.create(awsProvider, "us-west-1", "us-west-1", "yb-image1");
    ImageBundle.Metadata metadata = new ImageBundle.Metadata();
    metadata.setType(ImageBundleType.YBA_DEPRECATED);
    ImageBundle iB = createImageBundle(awsProvider, metadata);

    Result providerRes = getProvider(awsProvider.getUuid());
    JsonNode bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    awsProvider = Json.fromJson(bodyJson, Provider.class);

    // Add a region to the provider
    awsProvider.getRegions().add(Json.fromJson(getAWSRegionJson(), Region.class));
    UUID taskUUID = doEditProvider(awsProvider, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    awsProvider = Provider.getOrBadRequest(awsProvider.getUuid());

    Map<String, ImageBundleDetails.BundleInfo> bundleInfoMap = iB.getDetails().getRegions();
    assertEquals(1, bundleInfoMap.size());
    assertFalse(awsProvider.getImageBundles().get(0).getActive());
  }

  @Test
  public void testRegionDeleteImageBundleUpdate() throws InterruptedException {
    Provider awsProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws);
    Region.create(awsProvider, "us-west-1", "us-west-1", "yb-image1");
    ImageBundle.Metadata metadata = new ImageBundle.Metadata();
    metadata.setType(ImageBundleType.YBA_DEPRECATED);
    createImageBundle(awsProvider, metadata);

    Result providerRes = getProvider(awsProvider.getUuid());
    JsonNode bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    awsProvider = Json.fromJson(bodyJson, Provider.class);

    assertEquals(awsProvider.getImageBundles().get(0).getDetails().getRegions().size(), 1);
    awsProvider.getRegions().get(0).setActive(false);
    UUID taskUUID = doEditProvider(awsProvider, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    awsProvider = Provider.getOrBadRequest(awsProvider.getUuid());

    assertEquals(awsProvider.getImageBundles().get(0).getDetails().getRegions().size(), 0);
  }

  @Test
  public void testImageBundleEditRegionAdd() {
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.disableImageBundleValidation.getKey(), "true");
    Provider p = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws);
    Region.create(p, "us-west-1", "us-west-1", "yb-image1");
    createImageBundle(p, null);

    // Ensure adding a region updates the imageBundle
    JsonNode regionBody = getAWSRegionJson();
    Result region = addRegionV2(p.getUuid(), regionBody);
    JsonNode bodyJson = (ObjectNode) Json.parse(contentAsString(region));
    Region cRegion = Json.fromJson(bodyJson, Region.class);

    Result providerRes = getProvider(p.getUuid());
    bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    p = Json.fromJson(bodyJson, Provider.class);

    ImageBundle ib1 = p.getImageBundles().get(0);
    Map<String, ImageBundleDetails.BundleInfo> bundleInfoMap = ib1.getDetails().getRegions();
    assertEquals(2, bundleInfoMap.size());
    assertTrue(bundleInfoMap.keySet().contains("us-west-2"));

    // Ensure editing a region updates the imageBundle.
    cRegion.getDetails().getCloudInfo().getAws().setYbImage("Updated YB Image");
    region = editRegionV2(p.getUuid(), cRegion.getUuid(), Json.toJson(cRegion));
    providerRes = getProvider(p.getUuid());
    bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    p = Json.fromJson(bodyJson, Provider.class);

    ib1 = p.getImageBundles().get(0);
    bundleInfoMap = ib1.getDetails().getRegions();
    ImageBundleDetails.BundleInfo bInfo = bundleInfoMap.get(cRegion.getCode());
    assertEquals("Updated YB Image", bInfo.getYbImage());
  }

  @Test
  public void testImageBundleEditRegionAddLegacyPayload() {
    JsonNode vpcInfo = Json.parse("{\"us-west-2\": {\"zones\": {\"us-west-2a\": \"subnet-1\"}}}");
    when(mockNetworkManager.bootstrap(any(), any(), any())).thenReturn(vpcInfo);
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.disableImageBundleValidation.getKey(), "true");
    Provider p = ModelFactory.newProvider(defaultCustomer, Common.CloudType.aws);
    Region.create(p, "us-west-1", "us-west-1", "yb-image1");
    createImageBundle(p, null);

    // Ensure adding a region using legacy region updates the imageBundle (YBM)
    JsonNode regionBody = getAWSRegionJsonLegacy();
    Result region = addRegion(p.getUuid(), regionBody);
    JsonNode bodyJson = (ObjectNode) Json.parse(contentAsString(region));
    Region cRegion = Json.fromJson(bodyJson, Region.class);

    Result providerRes = getProvider(p.getUuid());
    bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    p = Json.fromJson(bodyJson, Provider.class);

    ImageBundle ib1 = p.getImageBundles().get(0);
    Map<String, ImageBundleDetails.BundleInfo> bundleInfoMap = ib1.getDetails().getRegions();
    assertEquals(2, bundleInfoMap.size());
    assertTrue(bundleInfoMap.keySet().contains("us-west-2"));

    // Ensure editing a region using legacy region updates the imageBundle (YBM)
    ObjectNode regionEditFormData = Json.newObject();
    regionEditFormData.put("ybImage", "Updated YB Image");
    regionEditFormData.put("securityGroupId", "securityGroupId");
    regionEditFormData.put("vnetName", "vnetName");
    region = editRegion(p.getUuid(), cRegion.getUuid(), regionEditFormData);
    providerRes = getProvider(p.getUuid());
    bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    p = Json.fromJson(bodyJson, Provider.class);

    ib1 = p.getImageBundles().get(0);
    bundleInfoMap = ib1.getDetails().getRegions();
    ImageBundleDetails.BundleInfo bInfo = bundleInfoMap.get(cRegion.getCode());
    assertEquals("Updated YB Image", bInfo.getYbImage());
  }

  private Provider createK8sProvider() {
    return createK8sProvider(true);
  }

  private Provider createK8sProvider(boolean withConfig) {
    ObjectMapper mapper = new ObjectMapper();

    String providerName = "Kubernetes-Provider";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "kubernetes");
    bodyJson.put("name", providerName);
    ObjectNode configJson = Json.newObject();
    if (withConfig) {
      configJson.put("KUBECONFIG_NAME", "test");
      configJson.put("KUBECONFIG_CONTENT", TestUtils.readResource("test-kubeconfig.conf"));
    }
    configJson.put("KUBECONFIG_PROVIDER", "GKE");
    bodyJson.set("config", configJson);

    ArrayNode regions = mapper.createArrayNode();
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "us-west1");
    regionJson.put("name", "US West");
    ArrayNode azs = mapper.createArrayNode();
    ObjectNode azJson = Json.newObject();
    azJson.put("code", "us-west1-a");
    azJson.put("name", "us-west1-a");
    azs.add(azJson);
    regionJson.putArray("zoneList").addAll(azs);
    regions.add(regionJson);
    bodyJson.putArray("regionList").addAll(regions);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + defaultCustomer.getUuid() + "/providers/kubernetes",
            user.createAuthToken(),
            bodyJson);
    JsonNode resultJson = Json.parse(contentAsString(result));

    return Provider.getOrBadRequest(
        defaultCustomer.getUuid(), UUID.fromString(resultJson.get("uuid").asText()));
  }

  private void mockDnsManagerListSuccess(String mockDnsName) {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"name\": \"" + mockDnsName + "\"}";
    shellResponse.code = 0;
    when(mockDnsManager.listDnsRecord(any(), any())).thenReturn(shellResponse);
  }

  private UUID doEditProvider(Provider editProviderReq, boolean validate) {
    return doEditProvider(editProviderReq, validate, false);
  }

  private UUID doEditProvider(
      Provider editProviderReq, boolean validate, boolean ignoreValidationErrors) {
    Result result = editProvider(editProviderReq, validate, ignoreValidationErrors);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    return UUID.fromString(json.get("taskUUID").asText());
  }

  private void verifyEditError(Provider provider, boolean validate, String error) {
    Result result = assertPlatformException(() -> editProvider(provider, validate));
    assertBadRequest(result, error);
  }

  private JsonNode getAWSRegionJson() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "us-west-2");
    bodyJson.put("name", "us-west-2");
    ObjectNode cloudInfo = Json.newObject();
    ObjectNode aws = Json.newObject();
    aws.put("vnet", "vnet");
    aws.put("securityGroupId", "securityGroupId");
    aws.put("ybImage", "ybImage");
    cloudInfo.set("aws", aws);

    ObjectNode details = Json.newObject();
    details.set("cloudInfo", cloudInfo);
    bodyJson.set("details", details);
    ArrayNode zones = Json.newArray();
    ObjectNode zone = Json.newObject();
    zone.put("name", "us-west-2a");
    zone.put("code", "us-west-2a");
    zone.put("subnet", "subnet");
    zones.add(zone);
    bodyJson.set("zones", zones);

    return bodyJson;
  }

  private JsonNode getAWSRegionJsonLegacy() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "us-west-2");
    bodyJson.put("name", "us-west-2");
    bodyJson.put("ybImage", "ybImage");
    bodyJson.put("securityGroupId", "securityGroupId");
    bodyJson.put("vnetName", "vnetName");

    return bodyJson;
  }

  private List<AccessKey> createTempAccesskeys() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.sshPrivateKeyContent = CertificateHelperTest.getServerKeyContent();
    AccessKey accessKeyTemp = AccessKey.create(provider.getUuid(), "access-key-temp", keyInfo);
    List<AccessKey> accessKeys = new ArrayList<>();
    accessKeys.add(accessKeyTemp);
    return accessKeys;
  }

  private ImageBundle createImageBundle(Provider provider, ImageBundle.Metadata metadata) {
    ImageBundleDetails details = new ImageBundleDetails();
    Map<String, ImageBundleDetails.BundleInfo> regionImageInfo = new HashMap<>();
    ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
    bundleInfo.setYbImage("yb_image");
    regionImageInfo.put("us-west-1", bundleInfo);
    details.setRegions(regionImageInfo);
    details.setArch(Architecture.x86_64);
    return ImageBundle.create(provider, "test-image", details, metadata, true);
  }
}
