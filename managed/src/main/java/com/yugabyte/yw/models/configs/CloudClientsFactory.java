// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs;

import com.amazonaws.services.s3.AmazonS3;
import com.azure.storage.blob.BlobContainerClient;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.cloud.storage.Storage;
import java.io.IOException;
import java.io.UnsupportedEncodingException;

public interface CloudClientsFactory {

  Storage createGcpStorage(String gcpCredentials) throws IOException, UnsupportedEncodingException;

  BlobContainerClient createBlobContainerClient(String azUrl, String azSasToken, String container);

  AmazonS3 createS3Client(JsonNode data);
}
