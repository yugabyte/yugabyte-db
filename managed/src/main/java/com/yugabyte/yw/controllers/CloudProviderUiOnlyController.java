// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.PrometheusConfigManager;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.forms.CloudProviderFormData;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.JsonFieldsValidator;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "UI_ONLY",
    hidden = true,
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CloudProviderUiOnlyController extends AuthenticatedController {

  @Inject private CloudProviderHandler cloudProviderHandler;

  @Inject private JsonFieldsValidator fieldsValidator;

  @Inject private PrometheusConfigManager prometheusConfigManager;

  /**
   * POST UI Only endpoint for creating new providers
   *
   * @return JSON response of newly created provider
   */
  @ApiOperation(value = "UI_ONLY", nickname = "createCloudProvider", hidden = true)
  public Result create(UUID customerUUID, Http.Request request) throws IOException {
    JsonNode reqBody = CloudInfoInterface.mayBeMassageRequest(request.body().asJson(), false);
    CloudProviderFormData cloudProviderFormData =
        formFactory.getFormDataOrBadRequest(reqBody, CloudProviderFormData.class);
    fieldsValidator.validateFields(
        JsonFieldsValidator.createProviderKey(cloudProviderFormData.code),
        cloudProviderFormData.config);
    // Hack to ensure old API remains functional.
    Provider reqProvider = new Provider();
    reqProvider.setCode(cloudProviderFormData.code.toString());
    if (!reqBody.isNull() && reqBody.has("details")) {
      ObjectMapper objectMapper = new ObjectMapper();
      reqProvider.setDetails(
          objectMapper.readValue(reqBody.get("details").toString(), ProviderDetails.class));
    } else {
      reqProvider.setConfigMap(cloudProviderFormData.config);
    }
    Provider existingProvider =
        Provider.get(customerUUID, cloudProviderFormData.name, cloudProviderFormData.code);
    if (existingProvider != null) {
      throw new PlatformServiceException(
          CONFLICT,
          String.format("Provider with the name %s already exists", cloudProviderFormData.name));
    }

    Provider provider =
        cloudProviderHandler.createProvider(
            Customer.getOrBadRequest(customerUUID),
            cloudProviderFormData.code,
            cloudProviderFormData.name,
            reqProvider,
            false,
            false);
    if (provider.getCloudCode() == Common.CloudType.onprem) {
      // UI was not calling bootstrap for onprem in old workflow, so need to mark provider as READY.
      provider.setUsabilityState(Provider.UsabilityState.READY);
      provider.save();
    }
    CloudInfoInterface.mayBeMassageResponse(provider);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            Objects.toString(provider.getUuid(), null),
            Audit.ActionType.Create,
            Json.toJson(cloudProviderFormData));
    return PlatformResults.withData(provider);
  }

  // TODO: This is temporary endpoint, so we can setup docker, will move this
  // to standard provider bootstrap route soon.
  @ApiOperation(value = "setupDocker", notes = "Unused", hidden = true)
  public Result setupDocker(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    List<Provider> providerList = Provider.get(customerUUID, Common.CloudType.docker);
    if (!providerList.isEmpty()) {
      return PlatformResults.withData(providerList.get(0));
    }

    Provider newProvider = cloudProviderHandler.setupNewDockerProvider(customer);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.CloudProvider,
            Objects.toString(newProvider.getUuid(), null),
            Audit.ActionType.SetupDocker);
    return PlatformResults.withData(newProvider);
  }

  // For creating the a multi-cluster kubernetes provider.
  @ApiOperation(value = "UI_ONLY", nickname = "createKubernetes", hidden = true)
  public Result createKubernetes(UUID customerUUID, Http.Request request) throws IOException {
    JsonNode requestBody = request.body().asJson();
    KubernetesProviderFormData formData =
        formFactory.getFormDataOrBadRequest(requestBody, KubernetesProviderFormData.class);
    fieldsValidator.validateFields(
        JsonFieldsValidator.createProviderKey(formData.code), formData.config);

    Provider provider =
        cloudProviderHandler.createKubernetes(Customer.getOrBadRequest(customerUUID), formData);
    prometheusConfigManager.updateK8sScrapeConfigs();
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            Objects.toString(provider.getUuid(), null),
            Audit.ActionType.CreateKubernetes);
    CloudInfoInterface.mayBeMassageResponse(provider);
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
    return PlatformResults.withRawData(
        Json.toJson(cloudProviderHandler.suggestedKubernetesConfigs()));
  }

  /** Deprecated because uses GET for state mutating method and now getting audited. */
  @Deprecated
  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result initialize(UUID customerUUID, UUID providerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    cloudProviderHandler.refreshPricing(customerUUID, provider);
    return YBPSuccess.withMessage(provider.getCode().toUpperCase() + " Initialized");
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  public Result bootstrap(UUID customerUUID, UUID providerUUID, Http.Request request) {
    // TODO(bogdan): Need to manually parse maps, maybe add try/catch on parse?
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    JsonNode requestBody = request.body().asJson();
    CloudBootstrap.Params taskParams =
        formFactory.getFormDataOrBadRequest(requestBody, CloudBootstrap.Params.class);
    UUID taskUUID = cloudProviderHandler.bootstrap(customer, provider, taskParams);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            Objects.toString(provider.getUuid(), null),
            Audit.ActionType.Bootstrap,
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
}
