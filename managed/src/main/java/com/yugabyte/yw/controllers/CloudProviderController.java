// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudCleanup;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.forms.CloudProviderFormData;
import com.yugabyte.yw.forms.CloudBootstrapFormData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TaskInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.forms.OnPremFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData;

import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;

import com.fasterxml.jackson.databind.node.ObjectNode;

import com.google.inject.Inject;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import javax.persistence.PersistenceException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerInstanceTypeMetadata;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerRegionMetadata;

public class CloudProviderController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  AWSInitializer awsInitializer;

  @Inject
  Commissioner commissioner;

  @Inject
  ConfigHelper configHelper;

  /**
   * GET endpoint for listing providers
   * @return JSON response with provider's
   */
  public Result list(UUID customerUUID) {
    List<Provider> providerList = Provider.getAll(customerUUID);
    return ok(Json.toJson(providerList));
  }

  // This endpoint we are using only for deleting provider for integration test purpose. our
  // UI should call cleanup endpoint.
  public Result delete(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.get(customerUUID, providerUUID);

    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
    }

    Customer customer = Customer.get(customerUUID);
    if (customer.getUniversesForProvider(provider.code).size() > 0) {
      return ApiResponse.error(BAD_REQUEST, "Cannot delete Provider with Universes");
    }

    // TODO: move this to task framework
    try {
      List<AccessKey> accessKeys = AccessKey.getAll(providerUUID);
      accessKeys.forEach((accessKey) -> {
        accessKey.delete();
      });

      provider.delete();
      return ApiResponse.success("Deleted provider: " + providerUUID);
    } catch (RuntimeException e) {
      LOG.error(e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete provider: " + providerUUID);
    }
  }

  /**
   * POST endpoint for creating new providers
   * @return JSON response of newly created provider
   */
  public Result create(UUID customerUUID) {
    Form<CloudProviderFormData> formData = formFactory.form(CloudProviderFormData.class).bindFromRequest();

    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    Common.CloudType providerCode = formData.get().code;
    Provider provider = Provider.get(customerUUID, providerCode);
    if (provider != null) {
      return ApiResponse.error(BAD_REQUEST, "Duplicate provider code: " + providerCode);
    }

    // Since the Map<String, String> doesn't get parsed, so for now we would just
    // parse it from the requestBody
    JsonNode requestBody = request().body().asJson();
    Map<String, String> config = new HashMap<>();
    if (requestBody.has("config")) {
      config = Json.fromJson(requestBody.get("config"), Map.class);
    }
    try {
      provider = Provider.create(customerUUID, providerCode, formData.get().name, config);
      return ApiResponse.success(provider);
    } catch (PersistenceException e) {
      LOG.error(e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to create provider: " + providerCode);
    }
  }

  // TODO: This is temporary endpoint, so we can setup docker, will move this
  // to standard provider bootstrap route soon.
  public Result setupDocker(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      ApiResponse.error(BAD_REQUEST, "Invalid Customer Context.");
    }
    Provider provider = Provider.get(customerUUID, Common.CloudType.docker);
    if (provider != null) {
      return ApiResponse.success(provider);
    }

    try {
      Provider newProvider = Provider.create(customerUUID, Common.CloudType.docker, "Docker");
      Map<String, Object> regionMetadata = configHelper.getConfig(DockerRegionMetadata);
      regionMetadata.forEach((regionCode, metadata) -> {
        Region region = Region.createWithMetadata(newProvider, regionCode, Json.toJson(metadata));
        Arrays.asList("a", "b", "c").forEach((zoneSuffix) -> {
          String zoneName = regionCode + zoneSuffix;
          AvailabilityZone.create(region, zoneName, zoneName, "yugabyte-bridge");
        });
      });
      Map<String, Object> instanceTypeMetadata = configHelper.getConfig(DockerInstanceTypeMetadata);
      instanceTypeMetadata.forEach((itCode, metadata) ->
          InstanceType.createWithMetadata(newProvider, itCode, Json.toJson(metadata)));
      return ApiResponse.success(newProvider);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to create docker provider");
    }
  }

  public Result initialize(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
    }
    return awsInitializer.initialize(customerUUID, providerUUID);
  }

  public Result bootstrap(UUID customerUUID, UUID providerUUID) {
    Form<CloudBootstrapFormData> formData = formFactory.form(CloudBootstrapFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID:" + providerUUID);
    }

    CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
    taskParams.providerUUID = providerUUID;
    taskParams.regionList = formData.get().regionList;
    taskParams.hostVPCId = formData.get().hostVPCId;
    UUID taskUUID = commissioner.submit(TaskInfo.Type.CloudBootstrap, taskParams);

    // TODO: add customer task
    ObjectNode resultNode = Json.newObject();
    resultNode.put("taskUUID", taskUUID.toString());
    return ApiResponse.success(resultNode);
  }

  public Result cleanup(UUID customerUUID, UUID providerUUID) {
    Form<CloudBootstrapFormData> formData = formFactory.form(CloudBootstrapFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID:" + providerUUID);
    }

    CloudCleanup.Params taskParams = new CloudCleanup.Params();
    taskParams.providerUUID = providerUUID;
    taskParams.regionList = formData.get().regionList;
    UUID taskUUID = commissioner.submit(TaskInfo.Type.CloudCleanup, taskParams);

    // TODO: add customer task
    ObjectNode resultNode = Json.newObject();
    resultNode.put("taskUUID", taskUUID.toString());
    return ApiResponse.success(resultNode);
  }
}
