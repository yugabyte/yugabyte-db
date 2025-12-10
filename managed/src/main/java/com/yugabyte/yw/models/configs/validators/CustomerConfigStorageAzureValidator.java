// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobStorageException;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.CloudUtil.ExtraPermissionToValidate;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.configs.CloudClientsFactory;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData.RegionLocations;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.net.UnknownHostException;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

public class CustomerConfigStorageAzureValidator extends CustomerConfigStorageValidator {

  private static final Collection<String> AZ_URL_SCHEMES = Arrays.asList(new String[] {"https"});

  private final CloudClientsFactory factory;
  private final StorageUtilFactory storageUtilFactory;

  private final List<ExtraPermissionToValidate> permissions =
      ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST);

  @Inject
  public CustomerConfigStorageAzureValidator(
      BeanValidator beanValidator,
      CloudClientsFactory factory,
      StorageUtilFactory storageUtilFactory) {
    super(beanValidator, AZ_URL_SCHEMES);
    this.factory = factory;
    this.storageUtilFactory = storageUtilFactory;
  }

  @Override
  public void validate(CustomerConfigData data) {
    super.validate(data);

    CustomerConfigStorageAzureData azureData = (CustomerConfigStorageAzureData) data;

    // Block config creation if both Azure SAS token and USE_AZURE_IAM passed, or
    // neither of the two.
    if (StringUtils.isBlank(azureData.azureSasToken) ^ (azureData.useAzureIam)) {
      SetMultimap<String, String> validationErrorsMap = HashMultimap.create();
      validationErrorsMap.put(
          CustomerConfigConsts.USE_AZURE_IAM_FIELDNAME,
          "Must pass only one of 'AZURE_STORAGE_SAS_TOKEN' or 'USE_AZURE_IAM'.");
      validationErrorsMap.put(
          AZUtil.AZURE_STORAGE_SAS_TOKEN_FIELDNAME,
          "Must pass only one of 'AZURE_STORAGE_SAS_TOKEN' or 'USE_AZURE_IAM'.");
      throwMultipleBeanConfigDataValidatorError(validationErrorsMap, "storageConfigValidation");
    }

    validateAzureUrl(
        azureData, CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, azureData.backupLocation);
    if (azureData.regionLocations != null) {
      for (RegionLocations location : azureData.regionLocations) {
        if (StringUtils.isEmpty(location.region)) {
          throwBeanConfigDataValidatorError(
              CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
        }
        validateUrl(CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location, true, false);
        // For region locations, a temporary config with region-specific SAS token
        CustomerConfigStorageAzureData regionConfig = new CustomerConfigStorageAzureData();
        regionConfig.azureSasToken = location.azureSasToken;
        regionConfig.useAzureIam = azureData.useAzureIam;
        validateAzureUrl(
            regionConfig, CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location);
      }
    }
  }

  private void validateAzureUrl(
      CustomerConfigStorageAzureData azureData, String fieldName, String azUriPath) {
    String protocol =
        azUriPath.indexOf(':') >= 0 ? azUriPath.substring(0, azUriPath.indexOf(':')) : "";

    // Assuming azure backup location will always start with https://
    if (azUriPath.length() < 8 || !AZ_URL_SCHEMES.contains(protocol)) {
      String exceptionMsg = "Invalid azUriPath format: " + azUriPath;
      throwBeanConfigDataValidatorError(fieldName, exceptionMsg);
    } else {
      String[] splitLocation = AZUtil.getSplitLocationValue(azUriPath);
      int splitLength = splitLocation.length;
      if (splitLength < 2) {
        // azUrl and container should be there in backup location.
        String exceptionMsg = "Invalid azUriPath format: " + azUriPath;
        throwBeanConfigDataValidatorError(fieldName, exceptionMsg);
      }

      String azUrl = "https://" + splitLocation[0];
      String container = splitLocation[1];
      String cloudPath = "";
      if (splitLength > 2) {
        cloudPath = splitLocation[2];
      }

      try {
        BlobContainerClient blobContainerClient =
            factory.createBlobContainerClient(azureData, azUrl, container);
        ((AZUtil) (storageUtilFactory.getCloudUtil(Util.AZ)))
            .validateOnBlobContainerClient(blobContainerClient, cloudPath, permissions);
      } catch (BlobStorageException e) {
        String exceptionMsg = e.getMessage();
        throwBeanConfigDataValidatorError(fieldName, exceptionMsg);
      } catch (Exception e) {
        if (e.getCause() != null && e.getCause() instanceof UnknownHostException) {
          String exceptionMsg = "Cannot access " + azUrl;
          throwBeanConfigDataValidatorError(fieldName, exceptionMsg);
        }
        throw e;
      }
    }
  }
}
