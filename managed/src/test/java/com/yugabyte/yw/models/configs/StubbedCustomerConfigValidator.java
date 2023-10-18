// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobStorageException;
import com.google.api.gax.paging.Page;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.Storage.BlobListOption;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.Collections;
import java.util.List;

// @formatter:off
/*
 * We are extending CustomerConfigValidator with some mocked behaviour.
 *
 *  - It allows to avoid direct cloud connections;
 *  - It allows to mark some buckets/URLs as existing and all others as wrong;
 *  - It allows to accept or to refuse keys/credentials.
 *
 */
// @formatter:on
public class StubbedCustomerConfigValidator extends CustomerConfigValidator
    implements CloudClientsFactory {

  public final AmazonS3Client s3Client = mock(AmazonS3Client.class);

  private final Storage gcpStorage = mock(Storage.class);

  public final BlobContainerClient blobContainerClient = mock(BlobContainerClient.class);

  private final BlobStorageException blobStorageException = mock(BlobStorageException.class);

  private final AWSUtil awsUtil = mock(AWSUtil.class);

  private boolean refuseKeys = false;

  public StubbedCustomerConfigValidator(
      BeanValidator beanValidator,
      List<String> allowedBuckets,
      StorageUtilFactory storageUtilFactory,
      RuntimeConfGetter runtimeConfGetter,
      AWSUtil awsUtil) {
    super(beanValidator, storageUtilFactory, runtimeConfGetter, awsUtil);

    lenient()
        .when(s3Client.doesBucketExistV2(any(String.class)))
        .thenAnswer(invocation -> allowedBuckets.contains(invocation.getArguments()[0]));
    lenient()
        .when(s3Client.listObjectsV2(any(String.class), any(String.class)))
        .thenAnswer(
            invocation -> {
              boolean allowedBucket = allowedBuckets.contains(invocation.getArguments()[0]);
              ListObjectsV2Result result = new ListObjectsV2Result();
              result.setKeyCount(allowedBucket ? 1 : 0);
              return result;
            });

    lenient()
        .when(
            gcpStorage.list(
                any(String.class), any(BlobListOption.class), any(BlobListOption.class)))
        .thenAnswer(
            invocation -> {
              if (allowedBuckets.contains(invocation.getArguments()[0])) {
                return new Page<Blob>() {
                  @Override
                  public boolean hasNextPage() {
                    return false;
                  }

                  @Override
                  public String getNextPageToken() {
                    return null;
                  }

                  @Override
                  public Page<Blob> getNextPage() {
                    return null;
                  }

                  @Override
                  public Iterable<Blob> iterateAll() {
                    return null;
                  }

                  @Override
                  public Iterable<Blob> getValues() {
                    return Collections.singleton(mock(Blob.class));
                  }
                };
              }
              return mock(Page.class);
            });
  }

  @Override
  public AmazonS3 createS3Client(CustomerConfigStorageS3Data configData) {
    if (refuseKeys) {
      throw new AmazonS3Exception(
          "The AWS Access Key Id you provided does not exist in our records.");
    }
    return s3Client;
  }

  @Override
  public Storage createGcpStorage(CustomerConfigStorageGCSData configData)
      throws UnsupportedEncodingException, IOException {
    if (refuseKeys) {
      throw new IOException("Invalid GCP Credential Json.");
    }
    return gcpStorage;
  }

  @Override
  public BlobContainerClient createBlobContainerClient(
      String azUrl, String azSasToken, String container) {
    if (refuseKeys) {
      when(blobStorageException.getMessage()).thenReturn("Invalid SAS token!");
      throw blobStorageException;
    }
    return blobContainerClient;
  }

  @Override
  protected CloudClientsFactory createCloudFactory() {
    return this;
  }

  public void setRefuseKeys(boolean value) {
    refuseKeys = value;
  }
}
