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
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.ShellProcessHandler;
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
  DnsManager dnsManager;

  /**
   * GET endpoint for listing providers
   * @return JSON response with provider's
   */
  public Result list(UUID customerUUID) {
    List<Provider> providerList = Provider.getAll(customerUUID);
    ArrayNode providers = Json.newArray();
    providerList.forEach((provider) -> {
      ObjectNode providerJson = (ObjectNode) Json.toJson(provider);
      providerJson.set("config", provider.getMaskedConfig());
      providers.add(providerJson);
    });
    return ApiResponse.success(providers);
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
    if (!providerCode.equals(Common.CloudType.kubernetes)) {
      Provider provider = Provider.get(customerUUID, providerCode);
      if (provider != null) {
        return ApiResponse.error(BAD_REQUEST, "Duplicate provider code: " + providerCode);
      }
    }

    // Since the Map<String, String> doesn't get parsed, so for now we would just
    // parse it from the requestBody
    JsonNode requestBody = request().body().asJson();
    Map<String, String> config = new HashMap<>();
    JsonNode configNode = requestBody.get("config");
    // Confirm we had a "config" key and it was not null.
    if (configNode != null && !configNode.isNull()) {
      if (providerCode.equals(Common.CloudType.gcp)) {
        config = Json.fromJson(configNode.get("config_file_contents"), Map.class);
      } else {
        config = Json.fromJson(configNode, Map.class);
      }
    }
    try {
      Provider provider = Provider.create(customerUUID, providerCode, formData.get().name, config);
      if (!config.isEmpty()) {
        switch (provider.code) {
          case "aws":
            String hostedZoneId = provider.getAwsHostedZoneId();
            if (hostedZoneId != null) {
              return validateAwsHostedZoneUpdate(provider, hostedZoneId);
            }
            break;
          case "gcp":
            updateGCPConfig(provider, config);
            break;
          case "kubernetes":
            updateKubeConfig(provider, config);
            break;
        }
      }
      return ApiResponse.success(provider);
    } catch (RuntimeException e) {
      e.printStackTrace();
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to create provider: " + providerCode);
    }
  }

  private void updateGCPConfig(Provider provider, Map<String,String> config) {
    String gcpCredentialsFile = null;
    try {
      gcpCredentialsFile = accessManager.createCredentialsFile(
          provider.uuid, Json.toJson(config));
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to create GCP Credentials file.");
    }

    Map<String, String> newConfig = new HashMap<String, String>();
    newConfig.put("GCE_EMAIL", config.get("client_email"));
    newConfig.put("GCE_PROJECT", config.get("project_id"));
    newConfig.put("GOOGLE_APPLICATION_CREDENTIALS", gcpCredentialsFile);

    provider.setConfig(newConfig);
    provider.save();
  }

  private void updateKubeConfig(Provider provider, Map<String, String> config) {
    String kubeConfigFile = null;
    try {
      kubeConfigFile = accessManager.createKubernetesConfig(
          provider.uuid, config
      );
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to create Kube Config file.");
    }
    // Remove the kubeconfig file related configs from provider config.
    config.remove("KUBECONFIG_NAME");
    config.remove("KUBECONFIG_CONTENT");
    // Update the KUBECONFIG variable with file path.
    config.put("KUBECONFIG", kubeConfigFile);
    provider.setConfig(config);
    provider.save();
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
    CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
    taskParams.providerCode = Common.CloudType.valueOf(provider.code);
    taskParams.providerUUID = providerUUID;
    taskParams.regionList = formData.get().regionList;
    // For now, if we send an empty list from the UI it seems to show up in the formData as null.
    if (taskParams.regionList == null) {
      taskParams.regionList = new ArrayList<String>();
    }
    String hostVpcRegion = formData.get().hostVpcRegion;
    if (provider.code.equals("gcp")) {
      // Ignore hostVpcRegion for GCP.
      hostVpcRegion = null;
    }
    if (formData.get().destVpcId != null && !formData.get().destVpcId.isEmpty()) {
      if (provider.code.equals("gcp")) {
        // We need to save the destVpcId into the provider config, because we'll need it during
        // instance creation. Technically, we could make it a ybcloud parameter, but we'd still need to
        // store it somewhere and the config is the easiest place to put it. As such, since all the
        // config is loaded up as env vars anyway, might as well use in in devops like that...
        Map<String, String> config = provider.getConfig();
        config.put("CUSTOM_GCE_NETWORK", formData.get().destVpcId);
        provider.setConfig(config);
        provider.save();
      } else if (provider.code.equals("aws")) {
        if (hostVpcRegion == null || hostVpcRegion.isEmpty()) {
          return ApiResponse.error(
              BAD_REQUEST, "For AWS provider, destVpcId requires hostVpcRegion");
        }
        // Add the hostVpcRegion to the regionList for an AWS provider, as we would only bootstrap
        // that particular region.
        taskParams.regionList.add(hostVpcRegion);
      }
    }
    // If the regionList is still empty by here, then we need to list the regions available.
    if (taskParams.regionList.isEmpty()) {
      CloudQueryHelper queryHelper = Play.current().injector().instanceOf(CloudQueryHelper.class);
      JsonNode regionInfo = queryHelper.getRegions(provider.uuid);
      if (regionInfo instanceof ArrayNode) {
        ArrayNode regionListArray = (ArrayNode) regionInfo;
        for (JsonNode region : regionListArray) {
          taskParams.regionList.add(region.asText());
        }
      }
    }
    taskParams.hostVpcRegion = hostVpcRegion;
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

  public Result edit(UUID customerUUID, UUID providerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      ApiResponse.error(BAD_REQUEST, "Invalid Customer Context.");
    }
    JsonNode formData =  request().body().asJson();
    String hostedZoneId = formData.get("hostedZoneId").asText();
    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
    }
    if (!provider.code.equals("aws")) {
      return ApiResponse.error(BAD_REQUEST, "Expected aws found providers with code: " + provider.code);
    }
    if (hostedZoneId == null || hostedZoneId.length() == 0) {
      return ApiResponse.error(BAD_REQUEST, "Required field hosted zone id");
    }
    return validateAwsHostedZoneUpdate(provider, hostedZoneId);
  }

  private Result validateAwsHostedZoneUpdate(Provider provider, String hostedZoneId) {
    // TODO: do we have a good abstraction to inspect this AND know that it's an error outside?
    ShellProcessHandler.ShellResponse response = dnsManager.listDnsRecord(
        provider.uuid, hostedZoneId);
    if (response.code != 0) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Invalid devops API response: " + response.message);
    }
    // The result returned from devops should be of the form
    // {
    //    "name": "dev.yugabyte.com."
    // }
    JsonNode hostedZoneData = Json.parse(response.message);
    hostedZoneData = hostedZoneData.get("name");
    if (hostedZoneData == null || hostedZoneData.asText().isEmpty()) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Invalid devops API response: " + response.message);
    }
    try {
      provider.updateHostedZone(hostedZoneId, hostedZoneData.asText());
    } catch (RuntimeException e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return ApiResponse.success(provider);
  }
}
