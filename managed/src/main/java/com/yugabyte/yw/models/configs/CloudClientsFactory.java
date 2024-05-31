// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs;

import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobStorageException;
import com.google.cloud.storage.Storage;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.io.IOException;
import java.io.UnsupportedEncodingException;

public interface CloudClientsFactory {

  Storage createGcpStorage(CustomerConfigStorageGCSData configData)
      throws IOException, UnsupportedEncodingException;

  BlobContainerClient createBlobContainerClient(String azUrl, String azSasToken, String container)
      throws BlobStorageException;

  AmazonS3 createS3Client(CustomerConfigStorageS3Data configData) throws AmazonS3Exception;
}
