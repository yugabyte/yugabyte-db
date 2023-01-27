// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static com.yugabyte.yw.forms.PlatformResults.YBPSuccess.withMessage;

import com.google.inject.Inject;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.forms.AccessKeyFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http.MultipartFormData;
import play.mvc.Http.MultipartFormData.FilePart;
import play.mvc.Result;

@Api(
    value = "Access Keys",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AccessKeyController extends AuthenticatedController {

  @Inject AccessManager accessManager;

  @Inject TemplateManager templateManager;

  @Inject ProviderEditRestrictionManager providerEditRestrictionManager;

  public static final Logger LOG = LoggerFactory.getLogger(AccessKeyController.class);

  @ApiOperation(value = "Get an access key", response = AccessKey.class)
  public Result index(UUID customerUUID, UUID providerUUID, String keyCode) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);

    AccessKey accessKey = AccessKey.getOrBadRequest(providerUUID, keyCode);
    accessKey.mergeProviderDetails();
    return PlatformResults.withData(accessKey);
  }

  @ApiOperation(
      value = "List access keys for a specific provider",
      response = AccessKey.class,
      responseContainer = "List")
  public Result list(UUID customerUUID, UUID providerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);

    List<AccessKey> accessKeys;
    accessKeys = AccessKey.getAll(providerUUID);
    accessKeys.forEach(AccessKey::mergeProviderDetails);
    return PlatformResults.withData(accessKeys);
  }

  @ApiOperation(
      value = "List access keys for all providers of a customer",
      response = AccessKey.class,
      responseContainer = "List")
  public Result listAllForCustomer(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<UUID> providerUUIDs =
        Provider.getAll(customerUUID)
            .stream()
            .map(provider -> provider.uuid)
            .collect(Collectors.toList());
    List<AccessKey> accessKeys = AccessKey.getByProviderUuids(providerUUIDs);
    accessKeys.forEach(AccessKey::mergeProviderDetails);
    return PlatformResults.withData(accessKeys);
  }

  // TODO: Move this endpoint under region since this api is per region
  @ApiOperation(
      nickname = "createAccesskey",
      value = "Create/Upload an access key for onprem Provider region",
      notes = "UNSTABLE - This API will undergo changes in future.",
      response = AccessKey.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "AccessKeyFormData",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AccessKeyFormData",
          required = true))
  public Result create(UUID customerUUID, UUID providerUUID) {
    final Provider provider = Provider.getOrBadRequest(providerUUID);
    AccessKeyFormData formData =
        formFactory
            .getFormDataOrBadRequest(AccessKeyFormData.class)
            .get()
            .setOrValidateRequestDataWithExistingKey(provider);
    AccessKey accessKey =
        providerEditRestrictionManager.tryEditProvider(
            providerUUID,
            () -> {
              try {
                return create(customerUUID, provider, formData);
              } catch (IOException e) {
                LOG.error("Failed to create access key", e);
                throw new PlatformServiceException(
                    INTERNAL_SERVER_ERROR,
                    "Failed to create access key: " + e.getLocalizedMessage());
              }
            });
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AccessKey,
            Objects.toString(accessKey.idKey, null),
            Audit.ActionType.Create,
            request().body().asJson());
    return PlatformResults.withData(accessKey);
  }

  private AccessKey create(UUID customerUUID, Provider provider, AccessKeyFormData formData)
      throws IOException {
    LOG.info(
        "Creating access key {} for customer {}, provider {}.",
        formData.keyCode,
        customerUUID,
        provider.uuid);
    List<Region> regionList = provider.regions;
    if (regionList.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Provider is in invalid state. No regions.");
    }
    Region region = regionList.get(0);

    if (formData.setUpChrony
        && region.provider.code.equals(onprem.name())
        && (formData.ntpServers == null || formData.ntpServers.isEmpty())) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "NTP servers not provided for on-premises provider for which chrony setup is desired");
    }

    if (region.provider.code.equals(onprem.name()) && formData.sshUser == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "sshUser cannot be null for onprem providers.");
    }

    // Check if a public/private key was uploaded as part of the request
    MultipartFormData<File> multiPartBody = request().body().asMultipartFormData();
    AccessKey accessKey;
    if (multiPartBody != null) {
      FilePart<File> filePart = multiPartBody.getFile("keyFile");
      File uploadedFile = filePart.getFile();
      if (formData.keyType == null || uploadedFile == null) {
        throw new PlatformServiceException(BAD_REQUEST, "keyType and keyFile params required.");
      }
      accessKey =
          accessManager.uploadKeyFile(
              region.uuid,
              uploadedFile,
              formData.keyCode,
              formData.keyType,
              formData.sshUser,
              formData.sshPort,
              formData.airGapInstall,
              formData.skipProvisioning,
              formData.setUpChrony,
              formData.ntpServers,
              formData.showSetUpChrony);
    } else if (formData.keyContent != null && !formData.keyContent.isEmpty()) {
      if (formData.keyType == null) {
        throw new PlatformServiceException(BAD_REQUEST, "keyType params required.");
      }
      // Create temp file and fill with content
      Path tempFile = Files.createTempFile(formData.keyCode, formData.keyType.getExtension());
      Files.write(tempFile, formData.keyContent.getBytes());

      // Upload temp file to create the access key and return success/failure
      accessKey =
          accessManager.uploadKeyFile(
              region.uuid,
              tempFile.toFile(),
              formData.keyCode,
              formData.keyType,
              formData.sshUser,
              formData.sshPort,
              formData.airGapInstall,
              formData.skipProvisioning,
              formData.setUpChrony,
              formData.ntpServers,
              formData.showSetUpChrony);
    } else {
      accessKey =
          accessManager.addKey(
              region.uuid,
              formData.keyCode,
              null,
              formData.sshUser,
              formData.sshPort,
              formData.airGapInstall,
              formData.skipProvisioning,
              formData.setUpChrony,
              formData.ntpServers,
              formData.showSetUpChrony);
    }

    // In case of onprem provider, we add a couple of additional attributes like passwordlessSudo
    // and create a pre-provision script
    if (region.provider.code.equals(onprem.name())) {
      templateManager.createProvisionTemplate(
          accessKey,
          formData.airGapInstall,
          formData.passwordlessSudoAccess,
          formData.installNodeExporter,
          formData.nodeExporterPort,
          formData.nodeExporterUser,
          formData.setUpChrony,
          formData.ntpServers);
    }

    if (formData.expirationThresholdDays != null) {
      accessKey.updateExpirationDate(formData.expirationThresholdDays);
    }

    KeyInfo keyInfo = accessKey.getKeyInfo();
    keyInfo.mergeFrom(provider.details);
    return accessKey;
  }

  @ApiOperation(
      nickname = "delete_accesskey",
      value = "Delete an access key",
      response = YBPSuccess.class)
  public Result delete(UUID customerUUID, UUID providerUUID, String keyCode) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);
    AccessKey accessKey = AccessKey.getOrBadRequest(providerUUID, keyCode);
    LOG.info(
        "Deleting access key {} for customer {}, provider {}", keyCode, customerUUID, providerUUID);

    accessKey.deleteOrThrow();
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AccessKey,
            Objects.toString(accessKey.idKey, null),
            Audit.ActionType.Delete);
    return withMessage("Deleted KeyCode: " + keyCode);
  }
}
