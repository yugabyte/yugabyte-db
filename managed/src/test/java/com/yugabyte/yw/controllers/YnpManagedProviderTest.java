// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertForbidden;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudProviderEdit;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.YnpProviderUtil;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.InstanceType.VolumeDetails;
import com.yugabyte.yw.models.InstanceType.VolumeType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import java.util.LinkedList;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

/**
 * Covers the restrictions YBA puts on an on-prem provider created by YNP: the user cannot change
 * its configuration, add instance types to it or register node instances with it, while YNP itself
 * still can. Deleting a dangling instance type stays allowed and an instance type is dropped once
 * its last node instance is gone.
 */
public class YnpManagedProviderTest extends FakeDBApplication {

  private static final String INSTANCE_TYPE_CODE = "ynp-instance-type";

  private Customer customer;
  private Users user;
  private Provider ynpProvider;
  private Region ynpRegion;
  private AvailabilityZone ynpZone;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    UUID taskUUID = buildTaskInfo(null, TaskType.CloudProviderEdit);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    ynpProvider = createOnPremProvider("ynp-provider", true /* ynpManaged */);
    ynpRegion = Region.create(ynpProvider, "region-1", "Region 1", "default-image");
    ynpZone = AvailabilityZone.createOrThrow(ynpRegion, "az-1", "AZ 1", "subnet-1");
    createInstanceType(ynpProvider, INSTANCE_TYPE_CODE);
  }

  private Provider createOnPremProvider(String name, boolean ynpManaged) {
    ProviderDetails details = new ProviderDetails();
    details.skipProvisioning = true;
    ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    OnPremCloudInfo onPremCloudInfo = new OnPremCloudInfo();
    onPremCloudInfo.ybHomeDir = "/home/yugabyte";
    onPremCloudInfo.ynpManaged = ynpManaged;
    cloudInfo.onprem = onPremCloudInfo;
    details.setCloudInfo(cloudInfo);
    Provider provider = Provider.create(customer.getUuid(), Common.CloudType.onprem, name, details);
    provider.setUsabilityState(Provider.UsabilityState.READY);
    provider.save();
    return provider;
  }

  private InstanceType createInstanceType(Provider provider, String instanceTypeCode) {
    InstanceTypeDetails details = new InstanceTypeDetails();
    VolumeDetails volumeDetails = new VolumeDetails();
    volumeDetails.volumeSizeGB = 100;
    volumeDetails.volumeType = VolumeType.SSD;
    volumeDetails.mountPath = "/mnt/d0";
    details.volumeDetailsList.add(volumeDetails);
    return InstanceType.upsert(provider.getUuid(), instanceTypeCode, 4, 8.0, details);
  }

  private NodeInstance createNodeInstance(AvailabilityZone zone, String ip, String instanceType) {
    NodeInstanceData nodeData = new NodeInstanceData();
    nodeData.ip = ip;
    nodeData.region = zone.getRegion().getCode();
    nodeData.zone = zone.getCode();
    nodeData.instanceType = instanceType;
    nodeData.instanceName = "node-" + ip;
    return NodeInstance.create(zone.getUuid(), nodeData);
  }

  private String editProviderUri(Provider provider) {
    return "/api/customers/" + customer.getUuid() + "/providers/" + provider.getUuid() + "/edit";
  }

  private JsonNode editProviderBody(Provider provider, boolean ynpManaged) {
    ObjectNode onPrem = Json.newObject();
    onPrem.put("ybHomeDir", "/home/yugabyte");
    onPrem.put("ynpManaged", ynpManaged);
    ObjectNode cloudInfo = Json.newObject();
    cloudInfo.set("onprem", onPrem);
    ObjectNode details = Json.newObject();
    details.put("skipProvisioning", true);
    details.set("cloudInfo", cloudInfo);
    ObjectNode body = Json.newObject();
    body.put("code", Common.CloudType.onprem.toString());
    body.put("name", provider.getName());
    body.put("version", provider.getVersion());
    body.set("details", details);
    body.set("regions", Json.toJson(provider.getRegions()));
    return body;
  }

  private JsonNode instanceTypeBody(String instanceTypeCode) {
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", instanceTypeCode);
    InstanceTypeDetails details = new InstanceTypeDetails();
    VolumeDetails volumeDetails = new VolumeDetails();
    volumeDetails.volumeSizeGB = 100;
    volumeDetails.volumeType = VolumeType.SSD;
    volumeDetails.mountPath = "/mnt/d1";
    details.volumeDetailsList.add(volumeDetails);
    ObjectNode body = Json.newObject();
    body.set("idKey", idKey);
    body.put("memSizeGB", 8.0);
    body.put("numCores", 4);
    body.set("instanceTypeDetails", Json.toJson(details));
    return body;
  }

  private JsonNode nodeInstanceBody(AvailabilityZone zone, String ip, String instanceType) {
    NodeInstanceFormData formData = new NodeInstanceFormData();
    NodeInstanceData nodeData = new NodeInstanceData();
    nodeData.ip = ip;
    nodeData.region = zone.getRegion().getCode();
    nodeData.zone = zone.getCode();
    nodeData.instanceType = instanceType;
    nodeData.instanceName = "node-" + ip;
    formData.nodes = new LinkedList<>();
    formData.nodes.add(nodeData);
    return Json.toJson(formData);
  }

  /** Same as the regular test request helpers, but marks the call as coming from YNP. */
  private Result doYnpRequestWithBody(String method, String uri, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, uri)
            .header(TokenAuthenticator.AUTH_TOKEN_HEADER, user.createAuthToken())
            .header(YnpProviderUtil.YNP_REQUEST_HEADER, "true");
    if (body != null) {
      request.bodyJson(body);
    }
    return route(request);
  }

  @Test
  public void testEditProviderIsBlockedForUser() {
    Result result =
        doRequestWithBody("PUT", editProviderUri(ynpProvider), editProviderBody(ynpProvider, true));
    assertForbidden(
        result,
        "Editing the provider configuration is not allowed for provider ynp-provider because it is"
            + " created and managed by YNP.");
  }

  @Test
  public void testEditProviderIsAllowedForYnp() {
    Result result =
        doYnpRequestWithBody(
            "PUT", editProviderUri(ynpProvider), editProviderBody(ynpProvider, true));
    assertOk(result);
  }

  @Test
  public void testEditProviderCannotClearYnpManagedFlag() {
    // Even coming from YNP, the flag is carried over from what is persisted rather than taken from
    // the request body.
    Result result =
        doYnpRequestWithBody(
            "PUT", editProviderUri(ynpProvider), editProviderBody(ynpProvider, false));
    assertOk(result);

    ArgumentCaptor<CloudProviderEdit.Params> paramsCaptor =
        ArgumentCaptor.forClass(CloudProviderEdit.Params.class);
    verify(mockCommissioner).submit(any(TaskType.class), paramsCaptor.capture());
    Provider newProviderState = paramsCaptor.getValue().newProviderState;
    OnPremCloudInfo onPremCloudInfo = CloudInfoInterface.get(newProviderState);
    assertNotNull(onPremCloudInfo);
    assertTrue(onPremCloudInfo.ynpManaged);
  }

  @Test
  public void testEditProviderIsAllowedForNonYnpProvider() {
    Provider provider = createOnPremProvider("plain-provider", false /* ynpManaged */);
    Region.create(provider, "region-1", "Region 1", "default-image");
    provider.refresh();
    Result result =
        doRequestWithBody("PUT", editProviderUri(provider), editProviderBody(provider, false));
    assertOk(result);
  }

  @Test
  public void testAddRegionIsBlockedForUser() {
    ObjectNode body = Json.newObject();
    body.put("code", "region-2");
    body.put("name", "Region 2");
    Result result =
        doRequestWithBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + ynpProvider.getUuid()
                + "/provider_regions",
            body);
    assertForbidden(
        result,
        "Adding a region is not allowed for provider ynp-provider because it is created and managed"
            + " by YNP.");
  }

  @Test
  public void testAddAccessKeyIsBlockedForUser() {
    ObjectNode body = Json.newObject();
    body.put("keyCode", "key-1");
    Result result =
        doRequestWithBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + ynpProvider.getUuid()
                + "/access_keys",
            body);
    assertForbidden(
        result,
        "Adding an access key is not allowed for provider ynp-provider because it is created and"
            + " managed by YNP.");
  }

  @Test
  public void testAddInstanceTypeIsBlockedForUser() {
    Result result =
        doRequestWithBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + ynpProvider.getUuid()
                + "/instance_types",
            instanceTypeBody("new-instance-type"));
    assertForbidden(
        result,
        "Adding an instance type is not allowed for provider ynp-provider because it is created and"
            + " managed by YNP.");
    assertNull(InstanceType.get(ynpProvider.getUuid(), "new-instance-type"));
  }

  @Test
  public void testAddInstanceTypeIsAllowedForYnp() {
    Result result =
        doYnpRequestWithBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + ynpProvider.getUuid()
                + "/instance_types",
            instanceTypeBody("new-instance-type"));
    assertOk(result);
    assertNotNull(InstanceType.get(ynpProvider.getUuid(), "new-instance-type"));
  }

  @Test
  public void testAddNodeInstanceIsBlockedForUser() {
    // The node instance endpoint is reached through the zone, so the check runs in the controller
    // rather than in the BlockYnpManagedProvider action.
    Result result =
        assertPlatformException(
            () ->
                doRequestWithBody(
                    "POST",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/zones/"
                        + ynpZone.getUuid()
                        + "/nodes",
                    nodeInstanceBody(ynpZone, "10.0.0.1", INSTANCE_TYPE_CODE)));
    assertForbidden(
        result,
        "Adding a node instance is not allowed for provider ynp-provider because it is created and"
            + " managed by YNP.");
    assertTrue(NodeInstance.listByZone(ynpZone.getUuid(), INSTANCE_TYPE_CODE).isEmpty());
  }

  @Test
  public void testAddNodeInstanceIsAllowedForYnp() {
    Result result =
        doYnpRequestWithBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/zones/" + ynpZone.getUuid() + "/nodes",
            nodeInstanceBody(ynpZone, "10.0.0.1", INSTANCE_TYPE_CODE));
    assertOk(result);
    assertEquals(1, NodeInstance.listByZone(ynpZone.getUuid(), INSTANCE_TYPE_CODE).size());
  }

  @Test
  public void testDeleteDanglingInstanceTypeIsAllowedForUser() {
    Result result =
        doRequest(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + ynpProvider.getUuid()
                + "/instance_types/"
                + INSTANCE_TYPE_CODE);
    assertOk(result);
    assertFalse(InstanceType.get(ynpProvider.getUuid(), INSTANCE_TYPE_CODE).isActive());
  }

  @Test
  public void testDeleteInstanceTypeInUseIsRejected() {
    createNodeInstance(ynpZone, "10.0.0.1", INSTANCE_TYPE_CODE);
    Result result =
        assertPlatformException(
            () ->
                doRequest(
                    "DELETE",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/providers/"
                        + ynpProvider.getUuid()
                        + "/instance_types/"
                        + INSTANCE_TYPE_CODE));
    assertBadRequest(result, "Cannot delete the Instance Type with existing Instances");
    assertTrue(InstanceType.get(ynpProvider.getUuid(), INSTANCE_TYPE_CODE).isActive());
  }

  @Test
  public void testDeletingLastNodeInstanceRemovesInstanceType() {
    createNodeInstance(ynpZone, "10.0.0.1", INSTANCE_TYPE_CODE);
    createNodeInstance(ynpZone, "10.0.0.2", INSTANCE_TYPE_CODE);

    // The instance type is still used by the second node instance.
    assertOk(deleteNodeInstance(ynpProvider, "10.0.0.1"));
    assertTrue(InstanceType.get(ynpProvider.getUuid(), INSTANCE_TYPE_CODE).isActive());

    assertOk(deleteNodeInstance(ynpProvider, "10.0.0.2"));
    assertFalse(InstanceType.get(ynpProvider.getUuid(), INSTANCE_TYPE_CODE).isActive());
  }

  @Test
  public void testDeletingLastNodeInstanceKeepsInstanceTypeOfNonYnpProvider() {
    Provider provider = createOnPremProvider("plain-provider", false /* ynpManaged */);
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    AvailabilityZone zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    createInstanceType(provider, INSTANCE_TYPE_CODE);
    createNodeInstance(zone, "10.1.0.1", INSTANCE_TYPE_CODE);

    assertOk(deleteNodeInstance(provider, "10.1.0.1"));
    assertTrue(InstanceType.get(provider.getUuid(), INSTANCE_TYPE_CODE).isActive());
  }

  private Result deleteNodeInstance(Provider provider, String ip) {
    return doRequest(
        "DELETE",
        "/api/customers/"
            + customer.getUuid()
            + "/providers/"
            + provider.getUuid()
            + "/instances/"
            + ip);
  }

  @Test
  public void testIsYnpManagedOnlyAppliesToOnPrem() {
    Provider awsProvider = ModelFactory.awsProvider(customer);
    assertFalse(awsProvider.isYnpManaged());
    assertTrue(ynpProvider.isYnpManaged());
    assertFalse(createOnPremProvider("plain-provider", false).isYnpManaged());
  }
}
