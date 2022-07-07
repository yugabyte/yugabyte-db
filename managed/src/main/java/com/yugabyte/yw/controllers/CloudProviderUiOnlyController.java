// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.forms.CloudProviderFormData;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.JsonFieldsValidator;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "UI_ONLY",
    hidden = true,
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CloudProviderUiOnlyController extends AuthenticatedController {

  @Inject private CloudProviderHandler cloudProviderHandler;

  @Inject private JsonFieldsValidator fieldsValidator;

  /**
   * POST UI Only endpoint for creating new providers
   *
   * @return JSON response of newly created provider
   */
  @ApiOperation(value = "UI_ONLY", nickname = "createCloudProvider", hidden = true)
  public Result create(UUID customerUUID) throws IOException {
    JsonNode reqBody = maybeMassageRequestConfig(request().body().asJson());
    CloudProviderFormData cloudProviderFormData =
        formFactory.getFormDataOrBadRequest(reqBody, CloudProviderFormData.class);
    fieldsValidator.validateFields(
        JsonFieldsValidator.createProviderKey(cloudProviderFormData.code),
        cloudProviderFormData.config);
    Provider provider =
        cloudProviderHandler.createProvider(
            Customer.getOrBadRequest(customerUUID),
            cloudProviderFormData.code,
            cloudProviderFormData.name,
            cloudProviderFormData.config,
            cloudProviderFormData.region);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CloudProvider,
            Objects.toString(provider.uuid, null),
            Audit.ActionType.Create,
            Json.toJson(cloudProviderFormData));
    return PlatformResults.withData(provider);
  }

  // TODO: This is temporary endpoint, so we can setup docker, will move this
  // to standard provider bootstrap route soon.
  @ApiOperation(value = "setupDocker", notes = "Unused", hidden = true)
  public Result setupDocker(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    List<Provider> providerList = Provider.get(customerUUID, Common.CloudType.docker);
    if (!providerList.isEmpty()) {
      return PlatformResults.withData(providerList.get(0));
    }

    Provider newProvider = cloudProviderHandler.setupNewDockerProvider(customer);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CloudProvider,
            Objects.toString(newProvider.uuid, null),
            Audit.ActionType.SetupDocker);
    return PlatformResults.withData(newProvider);
  }

  // For creating the a multi-cluster kubernetes provider.
  @ApiOperation(value = "UI_ONLY", nickname = "createKubernetes", hidden = true)
  public Result createKubernetes(UUID customerUUID) throws IOException {
    JsonNode requestBody = request().body().asJson();
    KubernetesProviderFormData formData =
        formFactory.getFormDataOrBadRequest(requestBody, KubernetesProviderFormData.class);
    fieldsValidator.validateFields(
        JsonFieldsValidator.createProviderKey(formData.code), formData.config);

    Provider provider =
        cloudProviderHandler.createKubernetes(Customer.getOrBadRequest(customerUUID), formData);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CloudProvider,
            Objects.toString(provider.uuid, null),
            Audit.ActionType.CreateKubernetes,
            requestBody);
    return PlatformResults.withData(provider);
  }

  @ApiOperation(
      value = "UI_ONLY",
      nickname = "getSuggestedKubernetesConfigs",
      hidden = true,
      notes =
          " Performs discovery of region, zones, pull secret, storageClass when running"
              + " inside a Kubernetes cluster. Returns the discovered information as a JSON, which"
              + " is similar to the one which is passed to the createKubernetes method.",
      response = KubernetesProviderFormData.class)
  public Result getSuggestedKubernetesConfigs(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(cloudProviderHandler.suggestedKubernetesConfigs());
  }

  /** Deprecated because uses GET for state mutating method and now getting audited. */
  @Deprecated
  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result initialize(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    cloudProviderHandler.refreshPricing(customerUUID, provider);
    return YBPSuccess.withMessage(provider.code.toUpperCase() + " Initialized");
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result bootstrap(UUID customerUUID, UUID providerUUID) {
    // TODO(bogdan): Need to manually parse maps, maybe add try/catch on parse?
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    JsonNode requestBody = request().body().asJson();
    CloudBootstrap.Params taskParams =
        formFactory.getFormDataOrBadRequest(requestBody, CloudBootstrap.Params.class);
    UUID taskUUID = cloudProviderHandler.bootstrap(customer, provider, taskParams);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CloudProvider,
            Objects.toString(provider.uuid, null),
            Audit.ActionType.Bootstrap,
            requestBody,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(value = "cleanup", notes = "Unimplemented", hidden = true)
  public Result cleanup(UUID customerUUID, UUID providerUUID) {
    // TODO(bogdan): this is not currently used, be careful about the API...
    return YBPSuccess.empty();

    /*
    CloudCleanup.Params taskParams = new CloudCleanup.Params();
    taskParams.providerUUID = providerUUID;
    taskParams.regionList = formData.get().regionList;
    UUID taskUUID = commissioner.submit(TaskType.CloudCleanup, taskParams);

    // TODO: add customer task
    return new YWResults.YWTask(taskUUID).asResult();
    */
  }

  @VisibleForTesting
  static JsonNode maybeMassageRequestConfig(JsonNode requestBody) {
    JsonNode configNode = requestBody.get("config");
    // Confirm we had a "config" key and it was not null.
    if (configNode != null && !configNode.isNull()) {
      if (requestBody.get("code").asText().equals(Common.CloudType.gcp.name())) {
        Map<String, String> config = new HashMap<>();
        // We may receive a config file, or we may be asked to use the local service account.
        // Default to using config file.
        boolean shouldUseHostCredentials =
            configNode.has("use_host_credentials")
                && configNode.get("use_host_credentials").asBoolean();
        JsonNode contents = configNode.get("config_file_contents");
        if (!shouldUseHostCredentials && contents != null) {
          config = Json.fromJson(contents, Map.class);
        }

        contents = configNode.get("host_project_id");
        if (contents != null && !contents.textValue().isEmpty()) {
          config.put("GCE_HOST_PROJECT", contents.textValue());
        }

        contents = configNode.get(CloudProviderHandler.YB_FIREWALL_TAGS);
        if (contents != null && !contents.textValue().isEmpty()) {
          config.put(CloudProviderHandler.YB_FIREWALL_TAGS, contents.textValue());
        }
        ((ObjectNode) requestBody).set("config", Json.toJson(config));
      }
    }
    return requestBody;
  }
}
