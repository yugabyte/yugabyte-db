// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs;

import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobStorageException;
import com.google.cloud.storage.Storage;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.S3Exception;

public interface CloudClientsFactory {

  Storage createGcpStorage(CustomerConfigStorageGCSData configData)
      throws IOException, UnsupportedEncodingException;

  BlobContainerClient createBlobContainerClient(
      CustomerConfigStorageAzureData configData, String azureUrl, String container)
      throws BlobStorageException;

  S3Client createS3Client(CustomerConfigStorageS3Data configData) throws S3Exception;
}
