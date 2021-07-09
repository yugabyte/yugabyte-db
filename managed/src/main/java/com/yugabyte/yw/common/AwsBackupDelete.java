// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.yugabyte.yw.models.Backup;
import com.amazonaws.auth.credentials.BasicAWSCredentials;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.model.DeleteObjectsRequest;
import com.amazonaws.services.s3.model.DeleteObjectsResponse;
import com.amazonaws.services.s3.model.ListObjectsV2Request;
import com.amazonaws.services.s3.model.ListObjectsV2Response;
import com.amazonaws.services.s3.model.ObjectIdentifier;
import com.fasterxml.jackson.databind.JsonNode;
import org.apache.commons.lang3.StringUtils;

@Singleton
public class AwsBackupDelete{

    private static final String AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";

    private static final String AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";

    private static final String AWS_HOST_BASE_FIELDNAME = "AWS_HOST_BASE";

    private static String[] getSplitLocationValue(String backupLocation){
        backupLocation = backupLocation.substring(5);
        String[] split = backupLocation.split("/", 2);
        return split;
    }

    public static AmazonS3Client create(String key, String secret) {
        AWSCredentials credentials = new BasicAWSCredentials(key, secret);
        return new AmazonS3Client(credentials);
    }

    public static void deleteStorage(
        JsonNode credentials, Backup backup) {
        String backupLocation = backup.getBackupInfo().storageLocation;
        AmazonS3Client s3Client = create(credentials.get(AWS_ACCESS_KEY_ID_FIELDNAME).asText(), credentials.get(AWS_SECRET_ACCESS_KEY_FIELDNAME).asText());
        if (credentials.get(AWS_HOST_BASE_FIELDNAME) != null
            && !StringUtils.isBlank(credentials.get(AWS_HOST_BASE_FIELDNAME).textValue())) {
            s3Client.setEndpoint(credentials.get(AWS_HOST_BASE_FIELDNAME).textValue());
        }
        String[] splitLocation = getSplitLocationValue(backupLocation);
        String bucketName = splitLocation[0];
        String objectPrefix = splitLocation[1];
        String nextContinuationToken = null;
        do {
        ListObjectsV2Response listObjectsResponse =
            listObjects(bucketName, objectPrefix, s3Client, nextContinuationToken);
        nextContinuationToken = null;
        if (listObjectsResponse.isTruncated()) {
            nextContinuationToken = listObjectsResponse.nextContinuationToken();
        }
        List<ObjectIdentifier> objectKeys =
            listObjectsResponse
                .contents()
                .parallelStream()
                .map(o -> ObjectIdentifier.builder().key(o.key()).build())
                .collect(Collectors.toList());
        DeleteObjectsRequest deleteRequest =
            DeleteObjectsRequest.builder()
                .bucket(bucketName)
                .delete(Delete.builder().objects(objectKeys).build())
                .build();
        DeleteObjectsResponse response = s3Client.deleteObjects(deleteRequest);
        if (response.hasErrors()) {
            log.error(response.errors().get(0).message());
            backup.transitionState(Backup.BackupState.FailedToDelete);
            break;
      }
    } while (nextContinuationToken != null);
    if(backup.state != Backup.BackupState.FailedToDelete){
        backup.transitionState(Backup.BackupState.Deleted);
    }
  }

  private static ListObjectsV2Response listObjects(
      String bucketName, String objectPrefix, S3Client s3Client, String continuationToken) {
    ListObjectsV2Request.Builder builder = ListObjectsV2Request.builder();
    builder.bucket(bucketName);
    if (continuationToken != null) {
      builder.continuationToken(continuationToken);
    }
    builder.prefix(objectPrefix);
    return s3Client.listObjectsV2(builder.build());
  }


}