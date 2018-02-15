// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.cloud.GCPInitializer;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudCleanup;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.forms.CloudProviderFormData;
import com.yugabyte.yw.forms.CloudBootstrapFormData;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.TaskType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;

import com.google.inject.Inject;

import play.api.Play;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import javax.persistence.PersistenceException;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
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
  GCPInitializer gcpInitializer;

  @Inject
  Commissioner commissioner;

  @Inject
  ConfigHelper configHelper;
  
  @Inject
  AccessManager accessManager;

  @Inject
  TemplateManager templateManager;

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
      for (AccessKey accessKey : AccessKey.getAll(providerUUID)) {
        if (!accessKey.getKeyInfo().provisionInstanceScript.isEmpty()) {
          new File(accessKey.getKeyInfo().provisionInstanceScript).delete();
        }
        accessKey.delete();
      }
      InstanceType.deleteInstanceTypesForProvider(provider);
      NodeInstance.deleteByProvider(providerUUID);
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
  public Result create(UUID customerUUID) throws IOException {
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
    if (requestBody.has("config") && providerCode.equals(Common.CloudType.gcp)) {
      JsonNode configNode = requestBody.get("config");
      config = Json.fromJson(configNode.get("config_file_contents"), Map.class);
    }
    try {
      provider = Provider.create(customerUUID, providerCode, formData.get().name, config);
      if (provider.code.equals("gcp") && !config.isEmpty()) {
        String gcpCredentialsFile = accessManager.createCredentialsFile(
            provider.uuid, requestBody.get("config").get("config_file_contents"));

        Map<String, String> newConfig = new HashMap<String, String>();
        newConfig.put("GCE_EMAIL", config.get("client_email"));
        newConfig.put("GCE_PROJECT", config.get("project_id"));
        newConfig.put("GOOGLE_APPLICATION_CREDENTIALS", gcpCredentialsFile);

        provider.setConfig(newConfig);
        provider.save();
      }
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
    if (provider.code.equals("gcp")) {
      return gcpInitializer.initialize(customerUUID, providerUUID);
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
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      ApiResponse.error(BAD_REQUEST, "Invalid Customer Context.");
    }
    // We need to save the destVpcId into the provider config, because we'll need it during
    // instance creation. Technically, we could make it a ybcloud parameter, but we'd still need to
    // store it somewhere and the config is the easiest place to put it. As such, since all the
    // config is loaded up as env vars anyway, might as well use in in devops like that...
    if (formData.get().destVpcId != null && !formData.get().destVpcId.isEmpty()) {
      Map<String, String> config = provider.getConfig();
      config.put("CUSTOM_GCE_NETWORK", formData.get().destVpcId);
      provider.setConfig(config);
      provider.save();
    }
    CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
    taskParams.providerCode = Common.CloudType.valueOf(provider.code);
    taskParams.providerUUID = providerUUID;
    taskParams.regionList = formData.get().regionList;
    // For now, if we send an empty list from the UI it seems to show up in the formData as null.
    if (taskParams.regionList == null) {
      taskParams.regionList = new ArrayList<String>();
    }
    if (taskParams.regionList.isEmpty()) {
      CloudQueryHelper queryHelper = Play.current().injector().instanceOf(CloudQueryHelper.class);
      JsonNode regionInfo = queryHelper.getRegions(provider.uuid);
      // TODO: validate this is an array!
      ArrayNode regionListArray = (ArrayNode) regionInfo;
      for (JsonNode region : regionListArray) {
        taskParams.regionList.add(region.asText());
      }
    }
    taskParams.hostVpcId = formData.get().hostVpcId;
    taskParams.destVpcId = formData.get().destVpcId;

    UUID taskUUID = commissioner.submit(TaskType.CloudBootstrap, taskParams);
    CustomerTask.create(customer,
      providerUUID,
      taskUUID,
      CustomerTask.TargetType.Provider,
      CustomerTask.TaskType.Create,
      provider.name);

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
    UUID taskUUID = commissioner.submit(TaskType.CloudCleanup, taskParams);

    // TODO: add customer task
    ObjectNode resultNode = Json.newObject();
    resultNode.put("taskUUID", taskUUID.toString());
    return ApiResponse.success(resultNode);
  }
}
