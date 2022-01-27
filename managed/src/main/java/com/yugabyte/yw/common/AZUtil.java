// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.BlobContainerClientBuilder;

@Singleton
public class AZUtil {
  public static final String AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";
  public static final String AZURE_STORAGE_BACKUP_LOCATION = "BACKUP_LOCATION";

  public static String[] getSplitLocationValue(String backupLocation, Boolean isConfigLocation) {
    backupLocation = backupLocation.substring(8);
    Integer splitValue = isConfigLocation ? 2 : 3;
    String[] split = backupLocation.split("/", splitValue);
    return split;
  }

  public static BlobContainerClient createBlobContainerClient(
      String azureUrl, String sasToken, String container) {
    BlobContainerClient blobContainerClient =
        new BlobContainerClientBuilder()
            .endpoint(azureUrl)
            .sasToken(sasToken)
            .containerName(container)
            .buildClient();
    return blobContainerClient;
  }
}
