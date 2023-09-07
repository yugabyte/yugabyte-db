package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudImageBundleSetup;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class ImageBundleControllerTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private Users user;
  private final ObjectMapper mapper = Json.mapper();
  SettableRuntimeConfigFactory runtimeConfigFactory;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    user = ModelFactory.testUser(customer);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
  }

  public class EditProviderParallelThreadExecutor extends Thread {

    private Provider provider;

    public EditProviderParallelThreadExecutor(Provider provider) {
      this.provider = provider;
    }

    @Override
    public void run() {
      AccessKey.create(
          provider.getUuid(), AccessKey.getDefaultKeyCode(provider), new AccessKey.KeyInfo());
      String jsonString =
          String.format(
              "{\"code\":\"aws\",\"name\":\"test\",\"regions\":[{\"name\":\"us-west-1\""
                  + ",\"code\":\"us-west-1\", \"details\": {\"cloudInfo\": { \"aws\": {"
                  + "\"vnetName\":\"vpc-foo\","
                  + "\"securityGroupId\":\"sg-foo\" }}}, "
                  + "\"zones\":[{\"code\":\"us-west-1a\",\"name\":\"us-west-1a\","
                  + "\"secondarySubnet\":\"subnet-foo\",\"subnet\":\"subnet-foo\"}]}],"
                  + "\"version\": %d}",
              provider.getVersion());
      Result result = editProvider(Json.parse(jsonString), provider.getUuid());
    }
  }

  /* ==== Helper Request Functions ==== */

  private Result listImageBundles(UUID customerUUID, UUID providerUUID, Architecture arch) {
    String uri = "/api/customers/%s/providers/%s/image_bundle";
    if (arch != null) {
      uri += String.format("?arch=%s", arch.toString());
    }
    return doRequestWithAuthToken(
        "GET",
        String.format(uri, customerUUID.toString(), providerUUID.toString()),
        user.createAuthToken());
  }

  private Result getImageBundle(UUID customerUUID, UUID providerUUID, UUID imageBundleUUID) {
    String uri = "/api/customers/%s/providers/%s/image_bundle/%s";
    return doRequestWithAuthToken(
        "GET",
        String.format(
            uri, customerUUID.toString(), providerUUID.toString(), imageBundleUUID.toString()),
        user.createAuthToken());
  }

  private Result createImageBundle(UUID customerUUID, UUID providerUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/%s/providers/%s/image_bundle";
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format(uri, customerUUID.toString(), providerUUID.toString()),
        user.createAuthToken(),
        bodyJson);
  }

  private Result editImageBundle(
      UUID customerUUID, UUID providerUUID, UUID imageBundleUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/%s/providers/%s/image_bundle/%s";
    return doRequestWithAuthTokenAndBody(
        "PUT",
        String.format(
            uri, customerUUID.toString(), providerUUID.toString(), imageBundleUUID.toString()),
        user.createAuthToken(),
        bodyJson);
  }

  private Result deleteImageBundle(UUID customerUUID, UUID providerUUID, UUID imageBundleUUID) {
    String uri = "/api/customers/%s/providers/%s/image_bundle/%s";
    return doRequestWithAuthToken(
        "DELETE",
        String.format(
            uri, customerUUID.toString(), providerUUID.toString(), imageBundleUUID.toString()),
        user.createAuthToken());
  }

  private Result editProvider(JsonNode bodyJson, UUID providerUUID) {
    return doRequestWithAuthTokenAndBody(
        "PUT",
        "/api/customers/"
            + customer.getUuid()
            + "/providers/"
            + providerUUID
            + "/edit?validate=false",
        user.createAuthToken(),
        bodyJson);
  }

  @Test
  public void testListImageBundles() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, true);
    ImageBundle ib2 = ImageBundle.create(provider, "ImageBundle-2", details, false);

    Result result = listImageBundles(customer.getUuid(), provider.getUuid(), null);
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    List<ImageBundle> imageBundles = Json.fromJson(json, List.class);
    assertEquals(imageBundles.size(), 2);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListImageBundleArch() {
    ImageBundleDetails details1 = new ImageBundleDetails();
    details1.setGlobalYbImage("Global-AMI-Image");
    details1.setArch(Architecture.x86_64);
    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details1, true);

    ImageBundleDetails details2 = new ImageBundleDetails();
    details2.setGlobalYbImage("Global-AMI-Image");
    details2.setArch(Architecture.aarch64);
    ImageBundle ib2 = ImageBundle.create(provider, "ImageBundle-2", details2, true);
    ImageBundle ib3 = ImageBundle.create(provider, "ImageBundle-3", details2, false);

    Result result = listImageBundles(customer.getUuid(), provider.getUuid(), Architecture.aarch64);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    Set<String> bundleNames = ImmutableSet.of("ImageBundle-2", "ImageBundle-3");
    assertEquals(bundleNames.size(), json.size());

    List<ImageBundle> imageBundles = new ArrayList<>();
    ArrayNode arrayNode = (ArrayNode) json;

    for (JsonNode node : arrayNode) {
      // Convert each JSON object in the array to an ImageBundle object
      ImageBundle imageBundle = Json.fromJson(node, ImageBundle.class);
      imageBundles.add(imageBundle);
    }
    for (ImageBundle bundle : imageBundles) {
      assertTrue(bundleNames.contains(bundle.getName()));
    }

    result = listImageBundles(customer.getUuid(), provider.getUuid(), Architecture.x86_64);
    assertEquals(OK, result.status());
    json = Json.parse(contentAsString(result));

    imageBundles = new ArrayList<>();
    arrayNode = (ArrayNode) json;

    for (JsonNode node : arrayNode) {
      // Convert each JSON object in the array to an ImageBundle object
      ImageBundle imageBundle = Json.fromJson(node, ImageBundle.class);
      imageBundles.add(imageBundle);
    }
    bundleNames = ImmutableSet.of("ImageBundle-1");
    assertEquals(bundleNames.size(), json.size());
    for (ImageBundle bundle : imageBundles) {
      assertTrue(bundleNames.contains(bundle.getName()));
    }
  }

  @Test
  public void testGetImageBundle() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, true);
    Result result = getImageBundle(customer.getUuid(), provider.getUuid(), ib1.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    ImageBundle imageBundle = Json.fromJson(json, ImageBundle.class);
    assertEquals(imageBundle.getName(), ib1.getName());
    assertEquals(imageBundle.getDetails(), ib1.getDetails());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateImageBundle() {
    ObjectNode imageBundleBodyJson = mapper.createObjectNode();
    imageBundleBodyJson.put("name", "Creating Image Bundle");
    imageBundleBodyJson.put("providerUUID", provider.getUuid().toString());

    ObjectNode details = mapper.createObjectNode();
    details.put("globalYbImage", "Testing Global YB Image");
    imageBundleBodyJson.put("details", details);

    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(CloudImageBundleSetup.Params.class)))
        .thenReturn(fakeTaskUUID);

    Result result = createImageBundle(customer.getUuid(), provider.getUuid(), imageBundleBodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    assertEquals(json.get("taskUUID").asText(), fakeTaskUUID.toString());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testEditImageBundle() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, true);
    ib1.getDetails().setGlobalYbImage("Updated Global YB Image");

    Result result =
        editImageBundle(
            customer.getUuid(), provider.getUuid(), ib1.getUuid(), (ObjectNode) Json.toJson(ib1));
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ImageBundle updatedBundle = Json.fromJson(json, ImageBundle.class);

    assertEquals("Updated Global YB Image", updatedBundle.getDetails().getGlobalYbImage());
  }

  @Test(expected = Exception.class)
  public void testEditImageBundleWithInvalidRegionReference() {
    Region region = Region.create(provider, "us-west-1", "us-west-1", "foo");
    region.setVnetName("vpc-foo");
    region.setSecurityGroupId("sg-foo");
    region.save();

    ImageBundleDetails details = new ImageBundleDetails();
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, true);
    Map<String, ImageBundleDetails.BundleInfo> ibRegionDetailsMap = new HashMap<>();
    details.setRegions(ibRegionDetailsMap);

    Result result =
        editImageBundle(
            customer.getUuid(), provider.getUuid(), ib1.getUuid(), (ObjectNode) Json.toJson(ib1));
  }

  @Test
  public void testDeleteImageBundle() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, false);
    ImageBundle ib2 = ImageBundle.create(provider, "ImageBundle-2", details, true);

    Result result = deleteImageBundle(customer.getUuid(), provider.getUuid(), ib1.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDeleteImageBundleDefault() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, false);
    ImageBundle ib2 = ImageBundle.create(provider, "ImageBundle-2", details, true);

    Result result =
        assertPlatformException(
            () -> deleteImageBundle(customer.getUuid(), provider.getUuid(), ib2.getUuid()));
    assertBadRequest(
        result,
        String.format(
            "Image Bundle %s is currently marked as default. Cannot delete", ib2.getUuid()));
  }

  @Test
  public void testDeleteOnlyImageBundle() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, true);

    Result result =
        assertPlatformException(
            () -> deleteImageBundle(customer.getUuid(), provider.getUuid(), ib1.getUuid()));
    assertBadRequest(
        result, "Minimum one image bundle must be associated with provider. Cannot delete");
  }

  @Test
  public void testGetInvalidImageBundle() {
    // Trying to query for a bundle that doesn't exist
    Result result =
        assertPlatformException(
            () -> getImageBundle(customer.getUuid(), provider.getUuid(), UUID.randomUUID()));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testEditImageBundleDefault() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, true);
    ImageBundle ib2 = ImageBundle.create(provider, "ImageBundle-2", details, false);
    ib2.setUseAsDefault(true);

    Result result =
        editImageBundle(
            customer.getUuid(), provider.getUuid(), ib2.getUuid(), (ObjectNode) Json.toJson(ib2));
    assertEquals(OK, result.status());
    ib2.refresh();
    assertEquals(true, ib2.getUseAsDefault());
  }

  @Test
  public void testEditImageBundleDefaultFail() {
    ImageBundleDetails details = new ImageBundleDetails();
    details.setGlobalYbImage("Global-AMI-Image");
    details.setArch(Architecture.x86_64);

    ImageBundle ib1 = ImageBundle.create(provider, "ImageBundle-1", details, true);
    ib1.setUseAsDefault(false);

    Result result =
        assertPlatformException(
            () ->
                editImageBundle(
                    customer.getUuid(),
                    provider.getUuid(),
                    ib1.getUuid(),
                    (ObjectNode) Json.toJson(ib1)));
    assertBadRequest(
        result,
        String.format(
            "One of the image bundle should be default for the provider %s", provider.getUuid()));
  }
}
