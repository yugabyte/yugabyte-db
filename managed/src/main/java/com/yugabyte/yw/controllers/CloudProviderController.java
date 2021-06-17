// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.cloud.AZUInitializer;
import com.yugabyte.yw.cloud.GCPInitializer;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import io.swagger.annotations.*;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Api(value = "Provider", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CloudProviderController extends AuthenticatedController {
  @Inject private CloudProviderService cloudProviderService;

  @Inject private ValidatingFormFactory formFactory;

  @Inject private AWSInitializer awsInitializer;

  @Inject private GCPInitializer gcpInitializer;

  @Inject private AZUInitializer azuInitializer;

  /**
   * GET endpoint for listing providers
   *
   * @return JSON response with provider's
   */
  @ApiOperation(value = "listProvider", response = Provider.class, responseContainer = "List")
  public Result list(UUID customerUUID) {
    return ApiResponse.success(Provider.getAll(customerUUID));
  }

  // This endpoint we are using only for deleting provider for integration test purpose. our
  // UI should call cleanup endpoint.
  @ApiOperation(value = "TEST_ONLY", hidden = true, response = YWResults.YWSuccess.class)
  public Result delete(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    cloudProviderService.delete(customer, provider);
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.withMessage("Deleted provider: " + providerUUID);
  }

  /**
   * POST endpoint for creating new providers
   *
   * @return JSON response of newly created provider
   */
  @ApiOperation(value = "createProvider", response = Provider.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "providerFormData",
          value = "provider form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.CloudProviderFormData",
          required = true))
  public Result create(UUID customerUUID) throws IOException {
    JsonNode reqBody = maybeMassageRequestConfig();
    CloudProviderFormData cloudProviderFormData =
        formFactory.getFormDataOrBadRequest(reqBody, CloudProviderFormData.class);
    Provider provider =
        cloudProviderService.createProvider(
            Customer.getOrBadRequest(customerUUID), cloudProviderFormData);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(cloudProviderFormData));
    return ApiResponse.success(provider);
  }

  // For creating the a multi-cluster kubernetes provider.
  public Result createKubernetes(UUID customerUUID) throws IOException {
    JsonNode requestBody = request().body().asJson();
    KubernetesProviderFormData formData =
        formFactory.getFormDataOrBadRequest(requestBody, KubernetesProviderFormData.class);

    Provider provider =
        cloudProviderService.createKubernetes(Customer.getOrBadRequest(customerUUID), formData);
    auditService().createAuditEntry(ctx(), request(), requestBody);
    return ApiResponse.success(provider);
  }

  @ApiOperation(
      value = "getSuggestedKubernetesConfigs",
      notes =
          " Performs discovery of region, zones, pull secret, storageClass when running"
              + " inside a Kubernetes cluster. Returns the discovered information as a JSON, which"
              + " is similar to the one which is passed to the createKubernetes method.",
      response = KubernetesProviderFormData.class)
  public Result getSuggestedKubernetesConfigs(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return ApiResponse.success(Json.toJson(cloudProviderService.suggestedKubernetesConfigs()));
  }

  // TODO: This is temporary endpoint, so we can setup docker, will move this
  // to standard provider bootstrap route soon.
  @ApiOperation(value = "setupDocker", notes = "Unused", hidden = true)
  public Result setupDocker(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    List<Provider> providerList = Provider.get(customerUUID, Common.CloudType.docker);
    if (!providerList.isEmpty()) {
      return ApiResponse.success(providerList.get(0));
    }

    Provider newProvider = cloudProviderService.setupNewDockerProvider(customer);
    auditService().createAuditEntry(ctx(), request());
    return ApiResponse.success(newProvider);
  }

  @ApiOperation(value = "refreshPricing", notes = "Refresh Provider pricing info")
  public Result initialize(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    if (provider.code.equals("gcp")) {
      return gcpInitializer.initialize(customerUUID, providerUUID);
    } else if (provider.code.equals("azu")) {
      return azuInitializer.initialize(customerUUID, providerUUID);
    }
    return awsInitializer.initialize(customerUUID, providerUUID);
  }

  @ApiOperation(value = "bootstrap", response = YWResults.YWTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "bootstrap params",
          dataType = "com.yugabyte.yw.commissioner.tasks.CloudBootstrap$Params",
          paramType = "body",
          required = true))
  public Result bootstrap(UUID customerUUID, UUID providerUUID) {
    // TODO(bogdan): Need to manually parse maps, maybe add try/catch on parse?
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    JsonNode requestBody = request().body().asJson();
    CloudBootstrap.Params taskParams =
        formFactory.getFormDataOrBadRequest(requestBody, CloudBootstrap.Params.class);
    UUID taskUUID = cloudProviderService.bootstrap(customer, provider, taskParams);
    auditService().createAuditEntry(ctx(), request(), requestBody, taskUUID);
    return new YWResults.YWTask(taskUUID).asResult();
  }

  @ApiOperation(value = "cleanup", notes = "Unimplemented", hidden = true)
  public Result cleanup(UUID customerUUID, UUID providerUUID) {
    // TODO(bogdan): this is not currently used, be careful about the API...
    Form<CloudBootstrapFormData> formData =
        formFactory.getFormDataOrBadRequest(CloudBootstrapFormData.class);

    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    return YWResults.YWSuccess.empty();

    /*
    CloudCleanup.Params taskParams = new CloudCleanup.Params();
    taskParams.providerUUID = providerUUID;
    taskParams.regionList = formData.get().regionList;
    UUID taskUUID = commissioner.submit(TaskType.CloudCleanup, taskParams);

    // TODO: add customer task
    return new YWResults.YWTask(taskUUID).asResult();
    */
  }

  @ApiOperation(value = "editProvider", response = Provider.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "edit provider form data",
          name = "EditProviderFormData",
          dataType = "com.yugabyte.yw.forms.EditProviderRequest",
          required = true,
          paramType = "body"))
  public Result edit(UUID customerUUID, UUID providerUUID) throws IOException {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    EditProviderRequest editProviderReq =
        formFactory.getFormDataOrBadRequest(request().body().asJson(), EditProviderRequest.class);
    cloudProviderService.editProvider(provider, editProviderReq);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(editProviderReq));
    return ApiResponse.success(provider);
  }

  private JsonNode maybeMassageRequestConfig() {
    JsonNode requestBody = request().body().asJson();
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

        contents = configNode.get("YB_FIREWALL_TAGS");
        if (contents != null && !contents.textValue().isEmpty()) {
          config.put("YB_FIREWALL_TAGS", contents.textValue());
        }
        ((ObjectNode) requestBody).set("config", Json.toJson(config));
      }
    }
    return requestBody;
  }
}
