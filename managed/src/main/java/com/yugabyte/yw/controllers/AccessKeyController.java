// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static com.yugabyte.yw.forms.PlatformResults.YBPSuccess.withMessage;

import com.google.inject.Inject;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.forms.AccessKeyFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
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

  public static final Logger LOG = LoggerFactory.getLogger(AccessKeyController.class);

  @ApiOperation(value = "Get an access key", response = AccessKey.class)
  public Result index(UUID customerUUID, UUID providerUUID, String keyCode) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);

    AccessKey accessKey = AccessKey.getOrBadRequest(providerUUID, keyCode);
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
    return PlatformResults.withData(accessKeys);
  }

  @ApiOperation(
      value = "List access keys for all providers of a customer",
      response = AccessKey.class,
      responseContainer = "List")
  public Result listAllForProviders(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<UUID> providerUUIDs =
        Provider.getAll(customerUUID)
            .stream()
            .map(provider -> provider.uuid)
            .collect(Collectors.toList());
    List<AccessKey> accessKeys = AccessKey.getByProviderUuids(providerUUIDs);
    return PlatformResults.withData(accessKeys);
  }

  @ApiOperation(
      nickname = "create_accesskey",
      value = "Create an access key",
      response = AccessKey.class)
  public Result create(UUID customerUUID, UUID providerUUID) throws IOException {
    AccessKeyFormData formData = formFactory.getFormDataOrBadRequest(AccessKeyFormData.class).get();
    formData = accessManager.setOrValidateRequestDataWithExistingKey(formData, providerUUID);
    UUID regionUUID = formData.regionUUID;
    Region region = Region.getOrBadRequest(customerUUID, providerUUID, regionUUID);

    String keyCode = formData.keyCode;
    String keyContent = formData.keyContent;
    AccessManager.KeyType keyType = formData.keyType;
    String sshUser = formData.sshUser;
    Integer sshPort = formData.sshPort;
    boolean airGapInstall = formData.airGapInstall;
    boolean skipProvisioning = formData.skipProvisioning;
    boolean setUpChrony = formData.setUpChrony;
    List<String> ntpServers = formData.ntpServers;
    boolean showSetUpChrony = formData.showSetUpChrony;
    boolean passwordlessSudoAccess = formData.passwordlessSudoAccess;
    boolean installNodeExporter = formData.installNodeExporter;
    Integer nodeExporterPort = formData.nodeExporterPort;
    String nodeExporterUser = formData.nodeExporterUser;
    Integer expirationThresholdDays = formData.expirationThresholdDays;
    AccessKey accessKey;

    LOG.info(
        "Creating access key {} for customer {}, provider {}.",
        keyCode,
        customerUUID,
        providerUUID);

    if (setUpChrony
        && region.provider.code.equals(onprem.name())
        && (ntpServers == null || ntpServers.isEmpty())) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "NTP servers not provided for on-premises provider for which chrony setup is desired");
    }

    if (region.provider.code.equals(onprem.name()) && sshUser == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "sshUser cannot be null for onprem providers.");
    }

    // Check if a public/private key was uploaded as part of the request
    MultipartFormData<File> multiPartBody = request().body().asMultipartFormData();
    if (multiPartBody != null) {
      FilePart<File> filePart = multiPartBody.getFile("keyFile");
      File uploadedFile = filePart.getFile();
      if (keyType == null || uploadedFile == null) {
        throw new PlatformServiceException(BAD_REQUEST, "keyType and keyFile params required.");
      }
      accessKey =
          accessManager.uploadKeyFile(
              region.uuid,
              uploadedFile,
              keyCode,
              keyType,
              sshUser,
              sshPort,
              airGapInstall,
              skipProvisioning,
              setUpChrony,
              ntpServers,
              showSetUpChrony);
    } else if (keyContent != null && !keyContent.isEmpty()) {
      if (keyType == null) {
        throw new PlatformServiceException(BAD_REQUEST, "keyType params required.");
      }
      // Create temp file and fill with content
      Path tempFile = Files.createTempFile(keyCode, keyType.getExtension());
      Files.write(tempFile, keyContent.getBytes());

      // Upload temp file to create the access key and return success/failure
      accessKey =
          accessManager.uploadKeyFile(
              regionUUID,
              tempFile.toFile(),
              keyCode,
              keyType,
              sshUser,
              sshPort,
              airGapInstall,
              skipProvisioning,
              setUpChrony,
              ntpServers,
              showSetUpChrony);
    } else {
      accessKey =
          accessManager.addKey(
              regionUUID,
              keyCode,
              null,
              sshUser,
              sshPort,
              airGapInstall,
              skipProvisioning,
              setUpChrony,
              ntpServers,
              showSetUpChrony);
    }

    // In case of onprem provider, we add a couple of additional attributes like passwordlessSudo
    // and create a preprovision script
    if (region.provider.code.equals(onprem.name())) {
      templateManager.createProvisionTemplate(
          accessKey,
          airGapInstall,
          passwordlessSudoAccess,
          installNodeExporter,
          nodeExporterPort,
          nodeExporterUser,
          setUpChrony,
          ntpServers);
    }

    if (expirationThresholdDays != null) {
      accessKey.updateExpirationDate(expirationThresholdDays);
    }

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AccessKey,
            Objects.toString(accessKey.idKey, null),
            Audit.ActionType.Create,
            request().body().asJson());
    return PlatformResults.withData(accessKey);
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
