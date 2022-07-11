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
import com.google.cloud.kms.v1.KeyManagementServiceClient;
import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.services.SmartKeyEARService;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;

import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Encryption at rest",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class EncryptionAtRestController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestController.class);

  private static Set<String> API_URL =
      ImmutableSet.of("api.amer.smartkey.io", "api.eu.smartkey.io", "api.uk.smartkey.io");

  public static final String AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";
  public static final String AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";
  public static final String AWS_REGION_FIELDNAME = "AWS_REGION";
  public static final String AWS_KMS_ENDPOINT_FIELDNAME = "AWS_KMS_ENDPOINT";

  public static final String SMARTKEY_API_KEY_FIELDNAME = "api_key";
  public static final String SMARTKEY_BASE_URL_FIELDNAME = "base_url";

  public static final String HC_ADDR_FNAME = HashicorpVaultConfigParams.HC_VAULT_ADDRESS;
  public static final String HC_TOKEN_FNAME = HashicorpVaultConfigParams.HC_VAULT_TOKEN;
  public static final String HC_ENGINE_FNAME = HashicorpVaultConfigParams.HC_VAULT_ENGINE;
  public static final String HC_MPATH_FNAME = HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH;

  @Inject EncryptionAtRestManager keyManager;

  @Inject Commissioner commissioner;

  @Inject CloudAPI.Factory cloudAPIFactory;

  @Inject GcpEARServiceUtil gcpEARServiceUtil;

  private void checkIfKMSConfigExists(UUID customerUUID, ObjectNode formData) {
    String kmsConfigName = formData.get("name").asText();
    if (KmsConfig.listKMSConfigs(customerUUID)
        .stream()
        .anyMatch(config -> config.name.equals(kmsConfigName))) {
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
          throw new PlatformServiceException(BAD_REQUEST, "Invalid AWS Credentials.");
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
            Function<ObjectNode, String> token =
                new SmartKeyEARService()::retrieveSessionAuthorization;
            token.apply(formData);
          } catch (Exception e) {
            throw new PlatformServiceException(BAD_REQUEST, "Invalid API Key.");
          }
        }
        break;
      case HASHICORP:
        if (formData.get(HC_ADDR_FNAME) == null || formData.get(HC_TOKEN_FNAME) == null) {
          throw new PlatformServiceException(BAD_REQUEST, "Invalid VAULT URL OR TOKEN");
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
      default:
        throw new PlatformServiceException(
            BAD_REQUEST, "Unrecognized key provider: " + keyProvider);
    }
  }

  private void checkEditableFields(ObjectNode formData, KeyProvider keyProvider, UUID configUUID) {

    KmsConfig config = KmsConfig.get(configUUID);
    ObjectNode authconfig = EncryptionAtRestUtil.getAuthConfig(configUUID, keyProvider);
    if (formData.get("name") != null && !config.name.equals(formData.get("name").asText())) {
      throw new PlatformServiceException(BAD_REQUEST, "KmsConfig name cannot be changed.");
    }

    if (keyProvider.equals(KeyProvider.AWS)) {
      if (formData.get(AWS_REGION_FIELDNAME) != null
          && !authconfig.get(AWS_REGION_FIELDNAME).equals(formData.get(AWS_REGION_FIELDNAME))) {
        throw new PlatformServiceException(BAD_REQUEST, "KmsConfig region cannot be changed.");
      }
    } else if (keyProvider.equals(KeyProvider.SMARTKEY)) {
      // NO checks required
    } else if (keyProvider.equals(KeyProvider.HASHICORP)) {

      if (formData.get(HC_ENGINE_FNAME) != null
          && !authconfig.get(HC_ENGINE_FNAME).equals(formData.get(HC_ENGINE_FNAME))) {
        throw new PlatformServiceException(
            BAD_REQUEST, "KmsConfig vault engine cannot be changed.");
      }
      if (formData.get(HC_MPATH_FNAME) != null
          && !authconfig.get(HC_MPATH_FNAME).equals(formData.get(HC_MPATH_FNAME))) {
        throw new PlatformServiceException(
            BAD_REQUEST, "KmsConfig vault engine path cannot be changed.");
      }
    }
  }

  private ObjectNode addNonEditableFieldsData(
      ObjectNode formData, UUID configUUID, KeyProvider keyProvider) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID, keyProvider);
    if (keyProvider.equals(KeyProvider.AWS)) {
      formData.set(AWS_REGION_FIELDNAME, authConfig.get(AWS_REGION_FIELDNAME));
      if (formData.get(AWS_ACCESS_KEY_ID_FIELDNAME) == null
          && authConfig.get(AWS_ACCESS_KEY_ID_FIELDNAME) != null) {
        formData.set(AWS_ACCESS_KEY_ID_FIELDNAME, authConfig.get(AWS_ACCESS_KEY_ID_FIELDNAME));
      }
      if (formData.get(AWS_SECRET_ACCESS_KEY_FIELDNAME) == null
          && authConfig.get(AWS_SECRET_ACCESS_KEY_FIELDNAME) != null) {
        formData.set(
            AWS_SECRET_ACCESS_KEY_FIELDNAME, authConfig.get(AWS_SECRET_ACCESS_KEY_FIELDNAME));
      }
    } else if (keyProvider.equals(KeyProvider.SMARTKEY)) {
      if (formData.get(SMARTKEY_API_KEY_FIELDNAME) == null
          && authConfig.get(SMARTKEY_API_KEY_FIELDNAME) != null) {
        formData.set(SMARTKEY_API_KEY_FIELDNAME, authConfig.get(SMARTKEY_API_KEY_FIELDNAME));
      }
    } else if (keyProvider.equals(KeyProvider.HASHICORP)) {

      formData.set(HC_ENGINE_FNAME, authConfig.get(HC_ENGINE_FNAME));
      formData.set(HC_MPATH_FNAME, authConfig.get(HC_MPATH_FNAME));

      if (formData.get(HC_TOKEN_FNAME) == null && authConfig.get(HC_TOKEN_FNAME) != null) {
        formData.set(HC_TOKEN_FNAME, authConfig.get(HC_TOKEN_FNAME));
      }
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
  public Result createKMSConfig(UUID customerUUID, String keyProvider) {
    LOG.info(
        String.format(
            "Creating KMS configuration for customer %s with %s",
            customerUUID.toString(), keyProvider));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    try {
      TaskType taskType = TaskType.CreateKMSConfig;
      ObjectNode formData = (ObjectNode) request().body().asJson();
      // Validating the KMS Provider config details.
      validateKMSProviderConfigFormData(formData, keyProvider, customerUUID);
      // checks if a already KMS Config exists with the requested name
      checkIfKMSConfigExists(customerUUID, formData);
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
              ctx(), Audit.TargetType.KMSConfig, null, Audit.ActionType.Create, formData, taskUUID);
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
  public Result editKMSConfig(UUID customerUUID, UUID configUUID) {
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
      ObjectNode formData = (ObjectNode) request().body().asJson();
      // Check for non-editable fields.
      checkEditableFields(formData, config.keyProvider, configUUID);
      // add non-editable fields in formData from existing config.
      formData = addNonEditableFieldsData(formData, configUUID, config.keyProvider);
      // Validating the KMS Provider config details.
      validateKMSProviderConfigFormData(formData, config.keyProvider.toString(), customerUUID);
      KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
      taskParams.configUUID = configUUID;
      taskParams.kmsProvider = config.keyProvider;
      taskParams.providerConfig = formData;
      taskParams.kmsConfigName = config.name;
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
              ctx(),
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
    KmsConfig config = KmsConfig.get(configUUID);
    ObjectNode kmsConfig =
        keyManager.getServiceInstance(config.keyProvider.name()).getAuthConfig(configUUID);
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
        KmsConfig.listKMSConfigs(customerUUID)
            .stream()
            .map(
                configModel -> {
                  ObjectNode result = null;
                  ObjectNode credentials =
                      keyManager
                          .getServiceInstance(configModel.keyProvider.name())
                          .getAuthConfig(configModel.configUUID);
                  if (credentials != null) {
                    result = Json.newObject();
                    ObjectNode metadata = Json.newObject();
                    metadata.put("configUUID", configModel.configUUID.toString());
                    metadata.put("provider", configModel.keyProvider.name());
                    metadata.put(
                        "in_use", EncryptionAtRestUtil.configInUse(configModel.configUUID));
                    metadata.put(
                        "universeDetails",
                        Json.toJson(EncryptionAtRestUtil.getUniverses(configModel.configUUID)));
                    metadata.put("name", configModel.name);
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
  public Result deleteKMSConfig(UUID customerUUID, UUID configUUID) {
    LOG.info(
        String.format(
            "Deleting KMS configuration %s for customer %s",
            configUUID.toString(), customerUUID.toString()));
    Customer customer = Customer.getOrBadRequest(customerUUID);
    try {
      KmsConfig config = KmsConfig.get(configUUID);
      TaskType taskType = TaskType.DeleteKMSConfig;
      KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
      taskParams.kmsProvider = config.keyProvider;
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
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.KMSConfig,
              configUUID.toString(),
              Audit.ActionType.Delete,
              taskUUID);
      return new YBPTask(taskUUID).asResult();
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  @ApiOperation(
      value = "Retrive a universe's KMS key",
      response = Object.class,
      responseContainer = "Map")
  public Result retrieveKey(UUID customerUUID, UUID universeUUID) {
    LOG.info(
        String.format(
            "Retrieving universe key for customer %s and universe %s",
            customerUUID.toString(), universeUUID.toString()));
    ObjectNode formData = (ObjectNode) request().body().asJson();
    byte[] keyRef = Base64.getDecoder().decode(formData.get("reference").asText());
    UUID configUUID = UUID.fromString(formData.get("configUUID").asText());
    byte[] recoveredKey = getRecoveredKeyOrBadRequest(universeUUID, configUUID, keyRef);
    ObjectNode result =
        Json.newObject()
            .put("reference", keyRef)
            .put("value", Base64.getEncoder().encodeToString(recoveredKey));
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RetrieveKmsKey,
            formData);
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
    return PlatformResults.withData(
        KmsHistory.getAllTargetKeyRefs(universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY)
            .stream()
            .map(
                history -> {
                  return Json.newObject()
                      .put("reference", history.uuid.keyRef)
                      .put("configUUID", history.configUuid.toString())
                      .put("timestamp", history.timestamp.toString());
                })
            .collect(Collectors.toList()));
  }

  @ApiOperation(value = "Remove a universe's key reference history", response = YBPSuccess.class)
  public Result removeKeyRefHistory(UUID customerUUID, UUID universeUUID) {
    LOG.info(
        String.format(
            "Removing key ref for customer %s with universe %s",
            customerUUID.toString(), universeUUID.toString()));
    keyManager.cleanupEncryptionAtRest(customerUUID, universeUUID);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
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
    KmsHistory activeKey = EncryptionAtRestUtil.getActiveKeyOrBadRequest(universeUUID);
    String keyRef = activeKey.uuid.keyRef;
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
