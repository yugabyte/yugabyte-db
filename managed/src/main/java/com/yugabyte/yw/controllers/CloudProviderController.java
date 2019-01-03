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
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
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
import play.Environment;
import play.libs.Json;
import play.mvc.Result;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.persistence.PersistenceException;

import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerInstanceTypeMetadata;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.DockerRegionMetadata;

public class CloudProviderController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderController.class);

  private static JsonNode KUBERNETES_DEV_INSTANCE_TYPE = Json.parse(
      "{\"instanceTypeCode\": \"dev\", \"numCores\": 0, \"memSizeGB\": 1}");
  private static JsonNode KUBERNETES_INSTANCE_TYPES = Json.parse("[" +
      "{\"instanceTypeCode\": \"xsmall\", \"numCores\": 2, \"memSizeGB\": 4}," +
      "{\"instanceTypeCode\": \"small\", \"numCores\": 4, \"memSizeGB\": 7.5}," +
      "{\"instanceTypeCode\": \"medium\", \"numCores\": 8, \"memSizeGB\": 15}," +
      "{\"instanceTypeCode\": \"large\", \"numCores\": 16, \"memSizeGB\": 15}," +
      "{\"instanceTypeCode\": \"xlarge\", \"numCores\": 32, \"memSizeGB\": 30}]");

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

  @Inject
  private play.Environment environment;

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
        // We may receive a config file, or we may be asked to use the local service account.
        JsonNode contents = configNode.get("config_file_contents");
        if (contents != null) {
          config = Json.fromJson(contents, Map.class);
        }
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
            try {
              createKubernetesInstanceTypes(provider);
            } catch (javax.persistence.PersistenceException ex) {
              // TODO: make instance types more multi-tenant friendly...
            }
            kubernetesProvision(provider, customerUUID);
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
    // If we were not given a config file, then no need to do anything here.
    if (config.isEmpty()) {
      return;
    }
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

  private void createKubernetesInstanceTypes(Provider provider) {
    KUBERNETES_INSTANCE_TYPES.forEach((instanceType -> {
      InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
      idt.setVolumeDetailsList(1, 100, InstanceType.VolumeType.SSD);
      InstanceType.upsert(provider.code,
          instanceType.get("instanceTypeCode").asText(),
          instanceType.get("numCores").asInt(),
          instanceType.get("memSizeGB").asDouble(),
          idt
      );
    }));
    if (environment.isDev()) {
      InstanceType.InstanceTypeDetails idt = new InstanceType.InstanceTypeDetails();
      idt.setVolumeDetailsList(1, 100, InstanceType.VolumeType.SSD);
      InstanceType.upsert(provider.code,
          KUBERNETES_DEV_INSTANCE_TYPE.get("instanceTypeCode").asText(),
          KUBERNETES_DEV_INSTANCE_TYPE.get("numCores").asInt(),
          KUBERNETES_DEV_INSTANCE_TYPE.get("memSizeGB").asDouble(),
          idt
      );
    }
  }

  /* Function to create Commissioner task for provisioning K8s providers
  // Will also be helpful to provision regions/AZs in the future
  */
  private void kubernetesProvision(Provider provider, UUID customerUUID) {
    CloudTaskParams taskParams = new CloudTaskParams();
    taskParams.providerUUID = provider.uuid;
    Customer customer = Customer.get(customerUUID);
    UUID taskUUID = commissioner.submit(TaskType.KubernetesProvision, taskParams);
    CustomerTask.create(customer,
      provider.uuid,
      taskUUID,
      CustomerTask.TargetType.Provider,
      CustomerTask.TaskType.Create,
      provider.name);

  }

  private void updateKubeConfig(Provider provider, Map<String, String> config) {
    String kubeConfigFile = null;
    String pullSecretFile = null;
    try {
      kubeConfigFile = accessManager.createKubernetesConfig(
          provider.uuid, config
      );
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to create Kube Config file.");
    }
    if (config.containsKey("KUBECONFIG_PULL_SECRET_NAME")) {
      if (config.get("KUBECONFIG_PULL_SECRET_NAME") != null) {
        try {
          pullSecretFile = accessManager.createPullSecret(
              provider.uuid, config
          );
        } catch (IOException e) {
          LOG.error(e.getMessage());
          throw new RuntimeException("Unable to create Pull Secret file.");
        }
      }
    }
    // Remove the kubeconfig file related configs from provider config.
    config.remove("KUBECONFIG_NAME");
    config.remove("KUBECONFIG_CONTENT");
    // Update the KUBECONFIG variable with file path.

    config.remove("KUBECONFIG_PULL_SECRET_NAME");
    config.remove("KUBECONFIG_PULL_SECRET_CONTENT");
    config.put("KUBECONFIG", kubeConfigFile);
    if (pullSecretFile != null) {
      config.put("KUBECONFIG_PULL_SECRET", pullSecretFile);
    }
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
    // TODO(bogdan): Need to manually parse maps, maybe add try/catch on parse?
    JsonNode requestBody = request().body().asJson();
    CloudBootstrap.Params taskParams = Json.fromJson(requestBody, CloudBootstrap.Params.class);

    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID:" + providerUUID);
    }
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      ApiResponse.error(BAD_REQUEST, "Invalid Customer Context.");
    }
    // Set the top-level provider info.
    taskParams.providerUUID = providerUUID;
    if (taskParams.destVpcId != null && !taskParams.destVpcId.isEmpty()) {
      if (provider.code.equals("gcp")) {
        // We need to save the destVpcId into the provider config, because we'll need it during
        // instance creation. Technically, we could make it a ybcloud parameter, but we'd still need to
        // store it somewhere and the config is the easiest place to put it. As such, since all the
        // config is loaded up as env vars anyway, might as well use in in devops like that...
        Map<String, String> config = provider.getConfig();
        config.put("CUSTOM_GCE_NETWORK", taskParams.destVpcId);
        provider.setConfig(config);
        provider.save();
      } else if (provider.code.equals("aws")) {
        taskParams.destVpcId = null;
      }
    }

    // If the regionList is still empty by here, then we need to list the regions available.
    if (taskParams.perRegionMetadata == null) {
      taskParams.perRegionMetadata = new HashMap<>();
    }
    if (taskParams.perRegionMetadata.isEmpty()) {
      CloudQueryHelper queryHelper = Play.current().injector().instanceOf(CloudQueryHelper.class);
      JsonNode regionInfo = queryHelper.getRegions(provider.uuid);
      if (regionInfo instanceof ArrayNode) {
        ArrayNode regionListArray = (ArrayNode) regionInfo;
        for (JsonNode region : regionListArray) {
          taskParams.perRegionMetadata.put(
              region.asText(), new CloudBootstrap.Params.PerRegionMetadata());
        }
      }
    }

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
    // TODO(bogdan): this is not currently used, be careful about the API...
    Form<CloudBootstrapFormData> formData = formFactory.form(CloudBootstrapFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    Provider provider = Provider.get(customerUUID, providerUUID);
    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID:" + providerUUID);
    } else {
      return ApiResponse.error(BAD_REQUEST, "Internal API issues");
    }

    /*
    CloudCleanup.Params taskParams = new CloudCleanup.Params();
    taskParams.providerUUID = providerUUID;
    taskParams.regionList = formData.get().regionList;
    UUID taskUUID = commissioner.submit(TaskType.CloudCleanup, taskParams);

    // TODO: add customer task
    ObjectNode resultNode = Json.newObject();
    resultNode.put("taskUUID", taskUUID.toString());
    return ApiResponse.success(resultNode);
    */
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
