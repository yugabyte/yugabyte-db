/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.services.SmartKeyEARService;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.AwsKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil.AzuKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil.GcpKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponse;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Result;

@Api(
    value = "Encryption at rest",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class EncryptionAtRestController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestController.class);

  // All these fields must be kept the same from the old authConfig (if it has)
  public static final List<String> awsKmsNonEditableFields =
      AwsKmsAuthConfigField.getNonEditableFields();
  // Below AWS fields can be editable. If no new field is specified, use the same old one.
  public static final List<String> awsKmsEditableFields = AwsKmsAuthConfigField.getEditableFields();

  private static Set<String> API_URL =
      ImmutableSet.of("api.amer.smartkey.io", "api.eu.smartkey.io", "api.uk.smartkey.io");

  public static final String SMARTKEY_API_KEY_FIELDNAME = "api_key";
  public static final String SMARTKEY_BASE_URL_FIELDNAME = "base_url";

  @Inject EncryptionAtRestManager keyManager;

  @Inject Commissioner commissioner;

  @Inject CloudAPI.Factory cloudAPIFactory;

  @Inject GcpEARServiceUtil gcpEARServiceUtil;

  @Inject AzuEARServiceUtil azuEARServiceUtil;

  private void checkIfKMSConfigExists(UUID customerUUID, ObjectNode formData) {
    String kmsConfigName = formData.get("name").asText();
    if (KmsConfig.listKMSConfigs(customerUUID).stream()
        .anyMatch(config -> config.getName().equals(kmsConfigName))) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Kms config with %s name already exists", kmsConfigName));
    }
  }

  private void validateKMSProviderConfigFormData(
      ObjectNode formData, String keyProvider, UUID customerUUID) {
    switch (KeyProvider.valueOf(keyProvider.toUpperCase())) {
      case AWS:
        CloudAPI cloudAPI = cloudAPIFactory.get(KeyProvider.AWS.toString().toLowerCase());
        if (cloudAPI == null) {
          throw new PlatformServiceException(
              SERVICE_UNAVAILABLE, "Cloud not create CloudAPI to validate the credentials");
        }
        if (!cloudAPI.isValidCredsKms(formData, customerUUID)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Invalid AWS Credentials or it has insuffient permissions.");
        }
        break;
      case SMARTKEY:
        if (formData.get(SMARTKEY_BASE_URL_FIELDNAME) == null
            || !EncryptionAtRestController.API_URL.contains(
                formData.get(SMARTKEY_BASE_URL_FIELDNAME).textValue())) {
          throw new PlatformServiceException(BAD_REQUEST, "Invalid API URL.");
        }
        if (formData.get(SMARTKEY_API_KEY_FIELDNAME) != null) {
          try {
            ((SmartKeyEARService) keyManager.getServiceInstance(KeyProvider.SMARTKEY.name()))
                .retrieveSessionAuthorization(formData);
          } catch (Exception e) {
            throw new PlatformServiceException(BAD_REQUEST, "Invalid API Key.");
          }
        }
        break;
      case HASHICORP:
        if (formData.get(HashicorpVaultConfigParams.HC_VAULT_ADDRESS) == null) {
          throw new PlatformServiceException(BAD_REQUEST, "Invalid VAULT URL");
        }
        if (formData.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN) == null
            && (formData.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID) == null
                || formData.get(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID) == null)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "VAULT TOKEN or AppRole credentials must be provided");
        }
        if (formData.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN) != null
            && (formData.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID) != null
                || formData.get(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID) != null)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "One of Vault Token or AppRole credentials must be provided");
        }
        try {
          if (HashicorpEARServiceUtil.getVaultSecretEngine(formData) == null)
            throw new PlatformServiceException(BAD_REQUEST, "Invalid Vault parameters");
        } catch (Exception e) {
          throw new PlatformServiceException(BAD_REQUEST, e.toString());
        }
        break;
      case GCP:
        try {
          gcpEARServiceUtil.validateKMSProviderConfigFormData(formData);
          LOG.info(
              "Finished validating GCP provider config form data for cryptokey = "
                  + gcpEARServiceUtil.getCryptoKeyRN(formData));
        } catch (Exception e) {
          LOG.warn("Could not finish validating GCP provider config form data.");
          throw new PlatformServiceException(BAD_REQUEST, e.toString());
        }
        break;
      case AZU:
        try {
          azuEARServiceUtil.validateKMSProviderConfigFormData(formData);
          LOG.info(
              "Finished validating AZU provider config form data for key vault = "
                  + azuEARServiceUtil.getConfigFieldValue(
                      formData, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName)
                  + ", key name = "
                  + azuEARServiceUtil.getConfigFieldValue(
                      formData, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName));
        } catch (Exception e) {
          LOG.warn("Could not finish validating AZU provider config form data.");
          throw new PlatformServiceException(BAD_REQUEST, e.toString());
        }
        break;
      default:
        throw new PlatformServiceException(
            BAD_REQUEST, "Unrecognized key provider: " + keyProvider);
    }
  }

  private void checkEditableFields(ObjectNode formData, KeyProvider keyProvider, UUID configUUID) {

    KmsConfig config = KmsConfig.get(configUUID);
    ObjectNode authconfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    if (formData.get("name") != null && !config.getName().equals(formData.get("name").asText())) {
      throw new PlatformServiceException(BAD_REQUEST, "KmsConfig name cannot be changed.");
    }

    switch (keyProvider) {
      case AWS:
        for (String field : awsKmsNonEditableFields) {
          if (formData.has(field)) {
            if (!authconfig.has(field)
                || (authconfig.has(field) && !authconfig.get(field).equals(formData.get(field)))) {
              throw new PlatformServiceException(
                  BAD_REQUEST, String.format("AWS KmsConfig field '%s' cannot be changed.", field));
            }
          }
        }
        LOG.debug("Verified that all the fields in the AWS KMS request are editable");
        break;
      case SMARTKEY:
        // NO checks required
        break;
      case HASHICORP:
        List<String> hashicorpKmsNonEditableFields =
            Arrays.asList(
                HashicorpVaultConfigParams.HC_VAULT_ENGINE,
                HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH,
                HashicorpVaultConfigParams.HC_VAULT_ADDRESS);
        for (String field : hashicorpKmsNonEditableFields) {
          if (formData.get(field) != null && !authconfig.get(field).equals(formData.get(field))) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format("Hashicorp KmsConfig field '%s' cannot be changed.", field));
          }
        }
        break;
      case GCP:
        // All the below fields are non editable
        List<String> nonEditableFields =
            Arrays.asList(
                GcpKmsAuthConfigField.LOCATION_ID.fieldName,
                GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName,
                GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName,
                GcpKmsAuthConfigField.KEY_RING_ID.fieldName,
                GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName);
        for (String field : nonEditableFields) {
          if (formData.has(field)) {
            if (!authconfig.has(field)
                || (authconfig.has(field) && !authconfig.get(field).equals(formData.get(field)))) {
              throw new PlatformServiceException(
                  BAD_REQUEST, String.format("GCP KmsConfig field '%s' cannot be changed.", field));
            }
          }
        }
        LOG.info("Verified that all the fields in the request are editable");
        break;
      case AZU:
        // All the below fields are non editable in AZU
        List<String> nonEditableFieldsAzu =
            Arrays.asList(
                AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName,
                AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName,
                AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName,
                AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName);
        for (String field : nonEditableFieldsAzu) {
          if (formData.has(field)) {
            if (!authconfig.has(field)
                || (authconfig.has(field) && !authconfig.get(field).equals(formData.get(field)))) {
              throw new PlatformServiceException(
                  BAD_REQUEST,
                  String.format("AZU Kms config field '%s' cannot be changed.", field));
            }
          }
        }
        LOG.info("Verified that all the fields in the AZU edit request are editable");
        break;
      default:
        throw new PlatformServiceException(
            BAD_REQUEST, "Unrecognized key provider while editing kms config: " + keyProvider);
    }
  }

  private ObjectNode addNonEditableFieldsData(
      ObjectNode formData, UUID configUUID, KeyProvider keyProvider) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    switch (keyProvider) {
      case AWS:
        // Make a copy of the original authConfig object and edit the editable fields.
        ObjectNode updatedFormData = authConfig.deepCopy();
        for (String fieldName : awsKmsEditableFields) {
          if (formData.has(fieldName)) {
            updatedFormData.set(fieldName, formData.get(fieldName));
          }
        }
        LOG.debug("Added all required AWS KMS fields to the formData to be edited");
        return updatedFormData;
      case SMARTKEY:
        if (formData.get(SMARTKEY_API_KEY_FIELDNAME) == null
            && authConfig.get(SMARTKEY_API_KEY_FIELDNAME) != null) {
          formData.set(SMARTKEY_API_KEY_FIELDNAME, authConfig.get(SMARTKEY_API_KEY_FIELDNAME));
        }
        break;
      case HASHICORP:
        formData.set(
            HashicorpVaultConfigParams.HC_VAULT_ENGINE,
            authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ENGINE));
        formData.set(
            HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH,
            authConfig.get(HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH));

        if (formData.get(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE) == null
            && authConfig.get(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE) != null) {
          formData.set(
              HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE,
              authConfig.get(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE));
        }

        // all of the credentials (token or approle) are empty, populate the value from auth config
        if (formData.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN) == null
            && (formData.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID) == null
                && formData.get(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID) == null)) {
          if (authConfig.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN) != null) {
            formData.set(
                HashicorpVaultConfigParams.HC_VAULT_TOKEN,
                authConfig.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN));
          }
          if (authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID) != null) {
            formData.set(
                HashicorpVaultConfigParams.HC_VAULT_ROLE_ID,
                authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID));
          }
          if (authConfig.get(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID) != null) {
            formData.set(
                HashicorpVaultConfigParams.HC_VAULT_SECRET_ID,
                authConfig.get(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID));
          }
        }

        break;
      case GCP:
        // All these fields must be kept the same from the old authConfig (if it has)
        List<String> nonEditableFields =
            Arrays.asList(
                GcpKmsAuthConfigField.LOCATION_ID.fieldName,
                GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName,
                GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName,
                GcpKmsAuthConfigField.KEY_RING_ID.fieldName,
                GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName);
        for (String field : nonEditableFields) {
          if (authConfig.has(field)) {
            formData.set(field, authConfig.get(field));
          }
        }
        // GCP_CONFIG field can change. If no config is specified, use the same old one.
        if (!formData.has(GcpKmsAuthConfigField.GCP_CONFIG.fieldName)
            && authConfig.has(GcpKmsAuthConfigField.GCP_CONFIG.fieldName)) {
          formData.set(
              GcpKmsAuthConfigField.GCP_CONFIG.fieldName,
              authConfig.get(GcpKmsAuthConfigField.GCP_CONFIG.fieldName));
        }
        LOG.info("Added all required fields to the formData to be edited");
        break;
      case AZU:
        // All these fields must be kept the same from the old authConfig (if it has)
        List<String> nonEditableFieldsAzu =
            Arrays.asList(
                AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName,
                AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName,
                AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName,
                AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName);
        for (String field : nonEditableFieldsAzu) {
          if (authConfig.has(field)) {
            formData.set(field, authConfig.get(field));
          }
        }
        // Below fields can change. If no new field is specified, use the same old one.
        List<String> editableFieldsAzu =
            Arrays.asList(
                AzuKmsAuthConfigField.CLIENT_ID.fieldName,
                AzuKmsAuthConfigField.CLIENT_SECRET.fieldName,
                AzuKmsAuthConfigField.TENANT_ID.fieldName);
        for (String field : editableFieldsAzu) {
          if (!formData.has(field) && authConfig.has(field)) {
            formData.set(field, authConfig.get(field));
          }
        }
        break;
      default:
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Unrecognized key provider while adding non editable fields: " + keyProvider);
    }
    return formData;
  }

  @ApiOperation(value = "Create a KMS configuration", response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "KMS config",
        value = "KMS config to be created",
        required = true,
        dataType = "Object",
        paramType = "body")
  })
  public Result createKMSConfig(UUID customerUUID, String keyProvider, Http.Request request) {
    LOG.info(
        String.format(
            "Creating KMS configuration for customer %s with %s",
            customerUUID.toString(), keyProvider));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    try {
      TaskType taskType = TaskType.CreateKMSConfig;
      ObjectNode formData = (ObjectNode) request.body().asJson();
      // checks if a already KMS Config exists with the requested name
      checkIfKMSConfigExists(customerUUID, formData);
      // Validating the KMS Provider config details.
      validateKMSProviderConfigFormData(formData, keyProvider, customerUUID);
      KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
      taskParams.kmsProvider = Enum.valueOf(KeyProvider.class, keyProvider);
      taskParams.providerConfig = formData;
      taskParams.customerUUID = customerUUID;
      taskParams.kmsConfigName = formData.get("name").asText();
      formData.remove("name");
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info("Submitted create KMS config for {}, task uuid = {}.", customerUUID, taskUUID);
      // Add this task uuid to the user universe.
      CustomerTask.create(
          customer,
          customerUUID,
          taskUUID,
          CustomerTask.TargetType.KMSConfiguration,
          CustomerTask.TaskType.Create,
          taskParams.getName());
      LOG.info(
          "Saved task uuid " + taskUUID + " in customer tasks table for customer: " + customerUUID);

      auditService()
          .createAuditEntryWithReqBody(
              request, Audit.TargetType.KMSConfig, null, Audit.ActionType.Create, taskUUID);
      return new YBPTask(taskUUID).asResult();
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  @ApiOperation(value = "Edit a KMS configuration", response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "KMS config",
        value = "KMS config to be edited",
        required = true,
        dataType = "Object",
        paramType = "body")
  })
  public Result editKMSConfig(UUID customerUUID, UUID configUUID, Http.Request request) {
    LOG.info(
        String.format(
            "Editing KMS configuration %s for customer %s",
            configUUID.toString(), customerUUID.toString()));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    KmsConfig config = KmsConfig.get(configUUID);
    if (config == null) {
      String errMsg =
          "KMS config with config UUID "
              + configUUID
              + " does not exist for customer "
              + customerUUID;
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    try {
      TaskType taskType = TaskType.EditKMSConfig;
      ObjectNode formData = (ObjectNode) request.body().asJson();
      // Check for non-editable fields.
      checkEditableFields(formData, config.getKeyProvider(), configUUID);
      // add non-editable fields in formData from existing config.
      formData = addNonEditableFieldsData(formData, configUUID, config.getKeyProvider());
      // Validating the KMS Provider config details.
      validateKMSProviderConfigFormData(formData, config.getKeyProvider().toString(), customerUUID);
      KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
      taskParams.configUUID = configUUID;
      taskParams.kmsProvider = config.getKeyProvider();
      taskParams.providerConfig = formData;
      taskParams.kmsConfigName = config.getName();
      taskParams.customerUUID = customerUUID;
      formData.remove("name");
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info("Submitted Edit KMS config for {}, task uuid = {}.", customerUUID, taskUUID);
      // Add this task uuid to the user universe.
      CustomerTask.create(
          customer,
          customerUUID,
          taskUUID,
          CustomerTask.TargetType.KMSConfiguration,
          CustomerTask.TaskType.Update,
          taskParams.getName());
      LOG.info(
          "Saved task uuid " + taskUUID + " in customer tasks table for customer: " + customerUUID);
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.KMSConfig,
              configUUID.toString(),
              Audit.ActionType.Edit,
              formData,
              taskUUID);
      return new YBPTask(taskUUID).asResult();
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  @ApiOperation(
      value = "Get details of a KMS configuration",
      response = Object.class,
      responseContainer = "Map")
  public Result getKMSConfig(UUID customerUUID, UUID configUUID) {
    LOG.info(String.format("Retrieving KMS configuration %s", configUUID.toString()));
    Customer.getOrBadRequest(customerUUID);
    KmsConfig config = KmsConfig.getOrBadRequest(customerUUID, configUUID);
    ObjectNode kmsConfig =
        keyManager.getServiceInstance(config.getKeyProvider().name()).getAuthConfig(configUUID);
    if (kmsConfig == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("No KMS configuration found for config %s", configUUID.toString()));
    }
    return PlatformResults.withRawData(kmsConfig);
  }

  // TODO: Cleanup raw json
  @ApiOperation(
      value = "List KMS configurations",
      response = Object.class,
      responseContainer = "List")
  public Result listKMSConfigs(UUID customerUUID) {
    LOG.info(String.format("Listing KMS configurations for customer %s", customerUUID.toString()));
    List<JsonNode> kmsConfigs =
        KmsConfig.listKMSConfigs(customerUUID).stream()
            .map(
                configModel -> {
                  ObjectNode result = null;
                  ObjectNode credentials =
                      keyManager
                          .getServiceInstance(configModel.getKeyProvider().name())
                          .getAuthConfig(configModel.getConfigUUID());
                  if (credentials != null) {
                    result = Json.newObject();
                    ObjectNode metadata = Json.newObject();
                    metadata.put("configUUID", configModel.getConfigUUID().toString());
                    metadata.put("provider", configModel.getKeyProvider().name());
                    metadata.put(
                        "in_use", EncryptionAtRestUtil.configInUse(configModel.getConfigUUID()));
                    metadata.put(
                        "universeDetails",
                        Json.toJson(
                            EncryptionAtRestUtil.getUniverses(configModel.getConfigUUID())));
                    metadata.put("name", configModel.getName());
                    result.put("credentials", CommonUtils.maskConfig(credentials));
                    result.put("metadata", metadata);
                  }
                  return result;
                })
            .filter(Objects::nonNull)
            .collect(Collectors.toList());

    return PlatformResults.withData(kmsConfigs);
  }

  @ApiOperation(value = "Delete a KMS configuration", response = YBPTask.class)
  public Result deleteKMSConfig(UUID customerUUID, UUID configUUID, Http.Request request) {
    LOG.info(
        String.format(
            "Deleting KMS configuration %s for customer %s",
            configUUID.toString(), customerUUID.toString()));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    try {
      KmsConfig config = KmsConfig.getOrBadRequest(customerUUID, configUUID);
      TaskType taskType = TaskType.DeleteKMSConfig;
      KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
      taskParams.kmsProvider = config.getKeyProvider();
      taskParams.customerUUID = customerUUID;
      taskParams.configUUID = configUUID;
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      LOG.info("Submitted delete KMS config for {}, task uuid = {}.", customerUUID, taskUUID);

      // Add this task uuid to the user universe.
      CustomerTask.create(
          customer,
          customerUUID,
          taskUUID,
          CustomerTask.TargetType.KMSConfiguration,
          CustomerTask.TaskType.Delete,
          taskParams.getName());
      LOG.info(
          "Saved task uuid " + taskUUID + " in customer tasks table for customer: " + customerUUID);
      auditService()
          .createAuditEntry(
              request,
              Audit.TargetType.KMSConfig,
              configUUID.toString(),
              Audit.ActionType.Delete,
              taskUUID);
      return new YBPTask(taskUUID).asResult();
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  @ApiOperation(value = "Refresh KMS Config", response = YBPSuccess.class)
  @ApiResponses(
      @ApiResponse(
          code = 500,
          message = "If there is an error refreshing the KMS config.",
          response = YBPError.class))
  public Result refreshKMSConfig(UUID customerUUID, UUID configUUID, Request request) {
    LOG.info(
        "Refreshing KMS configuration '{}' for customer '{}'.",
        configUUID.toString(),
        customerUUID.toString());
    Customer.getOrBadRequest(customerUUID);
    KmsConfig kmsConfig = KmsConfig.getOrBadRequest(customerUUID, configUUID);
    keyManager.getServiceInstance(kmsConfig.getKeyProvider().name()).refreshKms(configUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.KMSConfig, configUUID.toString(), Audit.ActionType.Refresh);
    return YBPSuccess.withMessage(
        String.format(
            "Successfully refreshed %s KMS config '%s'.",
            kmsConfig.getKeyProvider().name(), configUUID));
  }

  @ApiOperation(
      value = "Retrive a universe's KMS key",
      response = Object.class,
      responseContainer = "Map")
  public Result retrieveKey(UUID customerUUID, UUID universeUUID, Http.Request request) {
    LOG.info(
        String.format(
            "Retrieving universe key for customer %s and universe %s",
            customerUUID.toString(), universeUUID.toString()));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    ObjectNode formData = (ObjectNode) request.body().asJson();
    byte[] keyRef = Base64.getDecoder().decode(formData.get("reference").asText());
    UUID configUUID = UUID.fromString(formData.get("configUUID").asText());
    byte[] recoveredKey = getRecoveredKeyOrBadRequest(universeUUID, configUUID, keyRef);
    ObjectNode result =
        Json.newObject()
            .put("reference", keyRef)
            .put("value", Base64.getEncoder().encodeToString(recoveredKey));
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RetrieveKmsKey);
    return PlatformResults.withRawData(result);
  }

  public byte[] getRecoveredKeyOrBadRequest(UUID universeUUID, UUID configUUID, byte[] keyRef) {
    byte[] recoveredKey = keyManager.getUniverseKey(universeUUID, configUUID, keyRef);
    if (recoveredKey == null || recoveredKey.length == 0) {
      final String errMsg =
          String.format("No universe key found for universe %s", universeUUID.toString());
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    return recoveredKey;
  }

  @ApiOperation(
      value = "Get a universe's key reference history",
      response = Object.class,
      responseContainer = "List")
  public Result getKeyRefHistory(UUID customerUUID, UUID universeUUID) {
    LOG.info(
        String.format(
            "Retrieving key ref history for customer %s and universe %s",
            customerUUID.toString(), universeUUID.toString()));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    return PlatformResults.withData(
        KmsHistory.getAllTargetKeyRefs(universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY).stream()
            .map(
                history -> {
                  return Json.newObject()
                      .put("reference", history.getUuid().keyRef)
                      .put("configUUID", history.getConfigUuid().toString())
                      .put("re_encryption_count", history.getUuid().reEncryptionCount)
                      .put("db_key_id", history.dbKeyId)
                      .put("timestamp", history.getTimestamp().toString());
                })
            .collect(Collectors.toList()));
  }

  @ApiOperation(value = "Remove a universe's key reference history", response = YBPSuccess.class)
  public Result removeKeyRefHistory(UUID customerUUID, UUID universeUUID, Http.Request request) {
    LOG.info(
        String.format(
            "Removing key ref for customer %s with universe %s",
            customerUUID.toString(), universeUUID.toString()));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    keyManager.cleanupEncryptionAtRest(customerUUID, universeUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RemoveKmsKeyReferenceHistory);
    return YBPSuccess.withMessage("Key ref was successfully removed");
  }

  @ApiOperation(
      value = "Get a universe's key reference",
      response = Object.class,
      responseContainer = "Map")
  public Result getCurrentKeyRef(UUID customerUUID, UUID universeUUID) {
    LOG.info(
        String.format(
            "Retrieving key ref for customer %s and universe %s",
            customerUUID.toString(), universeUUID.toString()));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    KmsHistory activeKey = EncryptionAtRestUtil.getActiveKeyOrBadRequest(universeUUID);
    String keyRef = activeKey.getUuid().keyRef;
    if (keyRef == null || keyRef.length() == 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Could not retrieve key service for customer %s and universe %s",
              customerUUID.toString(), universeUUID.toString()));
    }
    return PlatformResults.withRawData(Json.newObject().put("reference", keyRef));
  }
}
