// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobStorageException;
import com.google.api.gax.paging.Page;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.Storage.BlobListOption;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.Collections;
import java.util.List;
import java.util.function.Consumer;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.HeadBucketRequest;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Request;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Response;
import software.amazon.awssdk.services.s3.model.S3Exception;

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

  public final S3Client s3Client = mock(S3Client.class);

  public final Storage gcpStorage = mock(Storage.class);

  public final BlobContainerClient blobContainerClient = mock(BlobContainerClient.class);

  private final BlobStorageException blobStorageException = mock(BlobStorageException.class);

  private final AWSUtil awsUtil = mock(AWSUtil.class);

  private boolean refuseKeys = false;

  public StubbedCustomerConfigValidator(
      BeanValidator beanValidator,
      List<String> allowedBuckets,
      StorageUtilFactory storageUtilFactory,
      RuntimeConfGetter runtimeConfGetter,
      AWSUtil awsUtil,
      AZUtil azUtil,
      GCPUtil gcpUtil) {
    super(beanValidator, storageUtilFactory, runtimeConfGetter, awsUtil, azUtil, gcpUtil);

    lenient()
        .when(s3Client.headBucket(any(Consumer.class)))
        .thenAnswer(
            invocation -> {
              Consumer<HeadBucketRequest.Builder> builderConsumer = invocation.getArgument(0);
              HeadBucketRequest.Builder builder = HeadBucketRequest.builder();
              builderConsumer.accept(builder);
              HeadBucketRequest request = builder.build();
              String bucketName = request.bucket();
              if (!allowedBuckets.contains(bucketName)) {
                throw S3Exception.builder().statusCode(404).build();
              }
              return null;
            });
    lenient()
        .when(s3Client.listObjectsV2(any(ListObjectsV2Request.class)))
        .thenAnswer(
            invocation -> {
              ListObjectsV2Request request = invocation.getArgument(0);
              String bucketName = request.bucket();
              boolean allowedBucket = allowedBuckets.contains(bucketName);
              ListObjectsV2Response result =
                  ListObjectsV2Response.builder().keyCount(allowedBucket ? 1 : 0).build();
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
  public S3Client createS3Client(CustomerConfigStorageS3Data configData) {
    if (refuseKeys) {
      throw S3Exception.builder()
          .message("The AWS Access Key Id you provided does not exist in our records.")
          .build();
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
      CustomerConfigStorageAzureData configData, String azureUrl, String container) {
    if (refuseKeys) {
      when(blobStorageException.getMessage()).thenReturn("Invalid credentials!");
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
