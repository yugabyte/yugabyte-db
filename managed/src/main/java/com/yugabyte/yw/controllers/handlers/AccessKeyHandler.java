package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.base.Strings;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.forms.AccessKeyFormData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Files.TemporaryFile;
import play.mvc.Http.MultipartFormData;
import play.mvc.Http.MultipartFormData.FilePart;
import play.mvc.Http.RequestBody;

@Slf4j
@Singleton
public class AccessKeyHandler {

  @Inject AccessManager accessManager;

  @Inject TemplateManager templateManager;

  @Inject ProviderEditRestrictionManager providerEditRestrictionManager;

  @Inject FileHelperService fileHelperService;

  public AccessKey create(
      UUID customerUUID, Provider provider, AccessKeyFormData formData, RequestBody requestBody) {
    log.info(
        "Creating access key {} for customer {}, provider {}.",
        formData.keyCode,
        customerUUID,
        provider.getUuid());
    return providerEditRestrictionManager.tryEditProvider(
        provider.getUuid(), () -> doCreate(provider, formData, requestBody));
  }

  private AccessKey doCreate(
      Provider provider, AccessKeyFormData formData, RequestBody requestBody) {
    // ToDo: Shubham why we are still using formData here?
    try {
      List<Region> regionList = provider.getRegions();
      if (regionList.isEmpty()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Provider is in invalid state. No regions.");
      }
      Region region = regionList.get(0);

      if (formData.setUpChrony
          && region.getProviderCloudCode().equals(CloudType.onprem)
          && (formData.ntpServers == null || formData.ntpServers.isEmpty())) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "NTP servers not provided for on-premises"
                + " provider for which chrony setup is desired");
      }

      if (region.getProviderCloudCode().equals(CloudType.onprem) && formData.sshUser == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "sshUser cannot be null for onprem providers.");
      }

      // Check if a public/private key was uploaded as part of the request
      MultipartFormData<TemporaryFile> multiPartBody = requestBody.asMultipartFormData();
      AccessKey accessKey;
      if (multiPartBody != null) {
        FilePart<TemporaryFile> filePart = multiPartBody.getFile("keyFile");
        TemporaryFile uploadedFile = filePart.getRef();
        if (formData.keyType == null || uploadedFile == null) {
          throw new PlatformServiceException(BAD_REQUEST, "keyType and keyFile params required.");
        }
        accessKey =
            accessManager.uploadKeyFile(
                region.getUuid(),
                uploadedFile.path(),
                formData.keyCode,
                formData.keyType,
                formData.sshUser,
                formData.sshPort,
                formData.airGapInstall,
                formData.skipProvisioning,
                formData.setUpChrony,
                formData.ntpServers,
                formData.showSetUpChrony,
                true,
                formData.skipKeyValidateAndUpload);
      } else if (formData.keyContent != null && !formData.keyContent.isEmpty()) {
        if (formData.keyType == null) {
          throw new PlatformServiceException(BAD_REQUEST, "keyType params required.");
        }
        // Create temp file and fill with content
        Path tempFile =
            fileHelperService.createTempFile(formData.keyCode, formData.keyType.getExtension());
        Files.write(tempFile, formData.keyContent.getBytes());

        // Upload temp file to create the access key and return success/failure
        accessKey =
            accessManager.uploadKeyFile(
                region.getUuid(),
                tempFile.toAbsolutePath(),
                formData.keyCode,
                formData.keyType,
                formData.sshUser,
                formData.sshPort,
                formData.airGapInstall,
                formData.skipProvisioning,
                formData.setUpChrony,
                formData.ntpServers,
                formData.showSetUpChrony,
                true,
                formData.skipKeyValidateAndUpload);
      } else {
        accessKey =
            accessManager.addKey(
                region.getUuid(),
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

      // In case of onprem provider, we add a couple of additional attributes like
      // passwordlessSudo
      // and create a pre-provision script
      if (region.getProviderCloudCode().equals(CloudType.onprem)) {
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
      keyInfo.mergeFrom(provider.getDetails());
      return accessKey;
    } catch (IOException e) {
      log.error("Failed to create access key", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to create access key: " + e.getLocalizedMessage());
    }
  }

  public AccessKey edit(UUID customerUUID, Provider provider, AccessKey accessKey, String keyCode) {
    log.info(
        "Editing access key {} for customer {}, provider {}.",
        keyCode,
        customerUUID,
        provider.getUuid());
    return providerEditRestrictionManager.tryEditProvider(
        provider.getUuid(), () -> doEdit(provider, accessKey, keyCode));
  }

  /**
   * Creates a new accessKey for the given provider as part of accessKey edit.
   *
   * @param provider Provider corresponding to which access key is being edited.
   * @param accessKey Optional accessKey that will be present for the cases of SelfManaged Keys. In
   *     case not specified, we will create a new YBA managed key.
   * @param keyCode Optional keyCode to be used for the accessKey.
   * @return the access key.
   */
  public AccessKey doEdit(
      Provider provider, @Nullable AccessKey accessKey, @Nullable String keyCode) {
    ProviderDetails details = provider.getDetails();
    String keyPairName = null;
    String sshPrivateKeyContent = null;
    boolean skipKeyValidateAndUpload = false;
    if (accessKey != null) {
      AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
      keyPairName = keyInfo.keyPairName;
      sshPrivateKeyContent = keyInfo.sshPrivateKeyContent;
      skipKeyValidateAndUpload = keyInfo.skipKeyValidateAndUpload;
      try {
        AccessKey.getOrBadRequest(provider.getUuid(), keyPairName);
        keyPairName = AccessKey.getNewKeyCode(keyPairName);
      } catch (PlatformServiceException e) {
        // Do nothing in case accessKey with above keyCode does not exist.
      }
    }
    if (keyPairName == null) {
      // If the keyPairName is not specified, generate a new keyCode
      // based on the provider.
      keyPairName = AccessKey.getNewKeyCode(provider);
    } else if (keyPairName.equals(keyCode)) {
      // If the passed keyPairName is same as the currentKeyCode, generate
      // the new Code appending timestamp to the current one.
      keyPairName = AccessKey.getNewKeyCode(keyPairName);
    }
    List<Region> regions = Region.getByProvider(provider.getUuid());
    AccessKey newAccessKey = null;
    for (Region region : regions) {
      if (!Strings.isNullOrEmpty(sshPrivateKeyContent)) {
        newAccessKey =
            accessManager.saveAndAddKey(
                region.getUuid(),
                sshPrivateKeyContent,
                keyPairName,
                AccessManager.KeyType.PRIVATE,
                details.sshUser,
                details.sshPort,
                details.airGapInstall,
                false,
                details.setUpChrony,
                details.ntpServers,
                details.showSetUpChrony,
                skipKeyValidateAndUpload);
      } else {
        newAccessKey =
            accessManager.addKey(
                region.getUuid(),
                keyPairName,
                null,
                details.sshUser,
                details.sshPort,
                details.airGapInstall,
                false,
                details.setUpChrony,
                details.ntpServers,
                details.showSetUpChrony);
      }
    }
    return newAccessKey;
  }

  public void deleteFile(String filePath) {
    if (!new File(filePath).delete()) {
      log.error("Failed to delete {}", filePath);
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Failed to delete %s", filePath));
    }
    // Delete the key from file_data
    FileData.deleteFileFromDB(filePath);
  }

  public void delete(AccessKey accessKey) {
    KeyInfo keyInfo = accessKey.getKeyInfo();

    // Delete the key from FS : publicKey, privateKey, vault_password, vaultFile
    try {
      deleteFile(keyInfo.publicKey);
      deleteFile(keyInfo.privateKey);
      deleteFile(keyInfo.vaultFile);
      deleteFile(keyInfo.vaultPasswordFile);
    } catch (Exception e) {
      log.error("Failed to delete access key with error: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Failed to delete access key with error: %s", e));
    }

    // Delete the key from DB
    accessKey.deleteOrThrow();
  }
}
