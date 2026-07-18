// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.IAMConfiguration;
import java.io.File;
import java.net.URI;
import java.util.ArrayList;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import software.amazon.awssdk.auth.credentials.*;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.regions.providers.DefaultAwsRegionProviderChain;
import software.amazon.awssdk.services.iam.IamClient;
import software.amazon.awssdk.services.iam.model.GetRoleRequest;
import software.amazon.awssdk.services.iam.model.Role;
import software.amazon.awssdk.services.sts.StsClient;
import software.amazon.awssdk.services.sts.StsClientBuilder;
import software.amazon.awssdk.services.sts.model.AssumeRoleRequest;
import software.amazon.awssdk.services.sts.model.AssumeRoleResponse;
import software.amazon.awssdk.services.sts.model.AssumeRoleWithWebIdentityRequest;
import software.amazon.awssdk.services.sts.model.AssumeRoleWithWebIdentityResponse;
import software.amazon.awssdk.services.sts.model.Credentials;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityRequest;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityResponse;

@Slf4j
public class IAMTemporaryCredentialsProvider {

  public static final String STS_DEFAULT_ENDPOINT = "sts.amazonaws.com";
  private static final String NUM_RETRIES_CONFIG_PATH = "yb.aws.iam_credentials_num_retries";
  private static final int WAIT_EACH_RETRY_SECS = 5;

  private final RuntimeConfGetter confGetter;

  public IAMTemporaryCredentialsProvider(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public enum IAMCredentialSource {
    // Try all in order WEB_TOKEN > IAM_USER > ASSUME_INSTANCE_ROLE > EC2_INSTANCE
    DEFAULT("default"),
    // ProfileCredentialsProvider
    IAM_USER("iam_user"),
    // WebIdentityTokenCredentialsProvider
    WEB_TOKEN("web_token"),
    // AssumeInstanceRole
    ASSUME_INSTANCE_ROLE("assume_instance_role"),
    // EC2ContainerCredentialsProvider
    EC2_INSTANCE("ec2_instance");

    private final String enumVal;

    IAMCredentialSource(String enumVal) {
      this.enumVal = enumVal;
    }

    public String getValue() {
      return enumVal;
    }
  }

  public AwsCredentials getTemporaryCredentials(CustomerConfigStorageS3Data s3Data) {
    List<CustomAWSCredentials> credentialsSource = new ArrayList<>();
    switch (s3Data.iamConfig.credentialSource) {
      case ASSUME_INSTANCE_ROLE:
        credentialsSource.add(new AssumeInstanceRole(s3Data.iamConfig));
        break;
      case EC2_INSTANCE:
        credentialsSource.add(new InstanceProfileCredentials());
        break;
      case IAM_USER:
        credentialsSource.add(new IAMUserCredentials(s3Data.iamConfig.iamUserProfile));
        break;
      case WEB_TOKEN:
        credentialsSource.add(new AssumeRoleWithWebIdentity(s3Data.iamConfig));
        break;
      case DEFAULT:
        log.debug(
            "Trying chain of credential providers in order:"
                + " WebIdentityTokenFileCredentialsProvider,"
                + " ProfileCredentialsProvider,"
                + " AssumeInstanceRole,"
                + " ContainerCredentialsProvider");
        credentialsSource.add(new AssumeRoleWithWebIdentity(s3Data.iamConfig));
        credentialsSource.add(new IAMUserCredentials(s3Data.iamConfig.iamUserProfile));
        credentialsSource.add(new AssumeInstanceRole(s3Data.iamConfig));
        credentialsSource.add(new InstanceProfileCredentials());
        break;
      default:
        throw new RuntimeException(
            String.format(
                "Invalid IAM credential source option %s", s3Data.iamConfig.credentialSource));
    }

    int numRetries = confGetter.getStaticConf().getInt(NUM_RETRIES_CONFIG_PATH);
    List<String> errorMessages = new ArrayList<>();
    while (numRetries > 0) {
      errorMessages.clear();
      // Loop through credential sources and get working credentials
      for (CustomAWSCredentials credentialSource : credentialsSource) {
        try {
          log.info("Loading IAM credentials from: {}", credentialSource.getSourceName());
          AwsCredentials credentials = credentialSource.getCredentialsOrFail();
          log.info("Found IAM credentials in: {}", credentialSource.getSourceName());
          return credentials;
        } catch (Exception e) {
          String message = "Source '" + credentialSource.getSourceName() + "': " + e.getMessage();
          errorMessages.add(message);
        }
      }
      numRetries--;
      if (numRetries > 0) {
        log.debug("Fetching IAM credentials failed, will retry after 5 seconds");
        try {
          Thread.sleep(WAIT_EACH_RETRY_SECS * 1000);
        } catch (InterruptedException e) {
          throw new RuntimeException("Thread interrupted while sleeping!");
        }
      }
    }
    throw new RuntimeException("Unable to load AWS credentials: " + errorMessages);
  }

  private interface CustomAWSCredentials {
    AwsCredentials getCredentialsOrFail() throws Exception;

    String getSourceName();
  }

  private static class AssumeInstanceRole implements CustomAWSCredentials {
    IAMConfiguration iamConfig;
    String stsRegion;

    @Override
    public String getSourceName() {
      return "AssumeInstanceRole";
    }

    public AssumeInstanceRole(IAMConfiguration iamConfig) {
      this.iamConfig = iamConfig;
      this.stsRegion = iamConfig.stsRegion;
    }

    @Override
    public AwsCredentials getCredentialsOrFail() throws SdkClientException {
      int maxDuration = iamConfig.duration;
      String roleArn = null;
      StsClient stsService =
          getStsClientBuilderWithoutCredentials(stsRegion, iamConfig.regionalSTS)
              .credentialsProvider(ContainerCredentialsProvider.create())
              .build();

      try {
        // Fetch max session limit for the role.
        GetCallerIdentityResponse gcResult =
            stsService.getCallerIdentity(GetCallerIdentityRequest.builder().build());
        String assumedRoleArn = gcResult.arn();
        String[] arnSplit = assumedRoleArn.split("/", 0);
        String role = arnSplit[arnSplit.length - 2];
        IamClient iamClient =
            IamClient.builder().credentialsProvider(ContainerCredentialsProvider.create()).build();
        Role iamRole = iamClient.getRole(GetRoleRequest.builder().roleName(role).build()).role();
        roleArn = iamRole.arn();
        maxDuration = iamRole.maxSessionDuration();
      } catch (Exception e) {
        log.debug(
            "Could not get maximum duration for role arn: {}. Using default 1 hour instead.",
            roleArn);
      }
      // Generate temporary credentials valid until the max session duration.
      AssumeRoleRequest roleRequest =
          AssumeRoleRequest.builder()
              .durationSeconds(maxDuration)
              .roleArn(roleArn)
              .roleSessionName("aws-java-sdk-" + System.currentTimeMillis())
              .build();

      AssumeRoleResponse assumeRoleResponse = stsService.assumeRole(roleRequest);
      Credentials credentials = assumeRoleResponse.credentials();
      return AwsSessionCredentials.create(
          credentials.accessKeyId(), credentials.secretAccessKey(), credentials.sessionToken());
    }
  }

  private static class AssumeRoleWithWebIdentity implements CustomAWSCredentials {
    IAMConfiguration iamConfig;
    String stsRegion;

    @Override
    public String getSourceName() {
      return "WebIdentityTokenCredentialsProvider";
    }

    public AssumeRoleWithWebIdentity(IAMConfiguration iamConfig) {
      this.iamConfig = iamConfig;
      this.stsRegion = iamConfig.stsRegion;
    }

    @Override
    public AwsCredentials getCredentialsOrFail() throws Exception {
      String webIdentityRoleArn = null;
      String webToken = null;
      int maxDuration = iamConfig.duration;

      // Create STS client to make subsequent calls.
      StsClient stsService =
          getStsClientBuilderWithoutCredentials(stsRegion, iamConfig.regionalSTS)
              .credentialsProvider(AnonymousCredentialsProvider.create())
              .build();

      // Fetch AWS_ROLE_ARN from environment( Yugaware is required to have it if service account IAM
      // set).
      // This is how default chain fetches it.
      webIdentityRoleArn = System.getenv("AWS_ROLE_ARN");
      if (StringUtils.isBlank(webIdentityRoleArn)) {
        throw new RuntimeException("AWS_ROLE_ARN: blank variable value.");
      }

      // Fetch web-token for making the Assume role request.
      try {
        webToken =
            FileUtils.readFileToString(
                new File(System.getenv("AWS_WEB_IDENTITY_TOKEN_FILE")), "UTF-8");
      } catch (Exception e) {
        throw new RuntimeException(
            String.format("Fetching Web Identity token failed: %s", e.getMessage()));
      }
      try {
        String[] arnSplit = webIdentityRoleArn.split("/", 0);
        String roleName = arnSplit[arnSplit.length - 1];

        IamClient iamClient =
            IamClient.builder()
                .credentialsProvider(WebIdentityTokenFileCredentialsProvider.create())
                .build();
        ;
        Role iamRole =
            iamClient.getRole(GetRoleRequest.builder().roleName(roleName).build()).role();
        maxDuration = iamRole.maxSessionDuration();
      } catch (Exception e) {
        log.debug(
            "Could not get maximum duration for role arn: {}. Using default 1 hour instead.",
            webIdentityRoleArn);
      }

      // Generate temporary credentials valid until the max session duration.
      AssumeRoleWithWebIdentityRequest roleRequest =
          AssumeRoleWithWebIdentityRequest.builder()
              .durationSeconds(maxDuration)
              .roleArn(webIdentityRoleArn)
              .webIdentityToken(webToken)
              .roleSessionName("aws-java-sdk-" + System.currentTimeMillis())
              .build();

      AssumeRoleWithWebIdentityResponse assumeRoleResponse =
          stsService.assumeRoleWithWebIdentity(roleRequest);
      Credentials credentials = assumeRoleResponse.credentials();
      return AwsSessionCredentials.create(
          credentials.accessKeyId(), credentials.secretAccessKey(), credentials.sessionToken());
    }
  }

  private static class InstanceProfileCredentials implements CustomAWSCredentials {
    public InstanceProfileCredentials() {}

    @Override
    public String getSourceName() {
      return "EC2ContainerCredentialsProvider";
    }

    @Override
    public AwsCredentials getCredentialsOrFail() throws Exception {
      return InstanceProfileCredentialsProvider.create().resolveCredentials();
    }
  }

  private static class IAMUserCredentials implements CustomAWSCredentials {
    String profileName;

    @Override
    public String getSourceName() {
      return "ProfileCredentialsProvider";
    }

    public IAMUserCredentials(String profileName) {
      this.profileName = profileName;
    }

    @Override
    public AwsCredentials getCredentialsOrFail() throws Exception {
      return ProfileCredentialsProvider.builder()
          .profileName(profileName)
          .build()
          .resolveCredentials();
    }
  }

  private static StsClientBuilder getStsClientBuilderWithoutCredentials(
      String stsRegion, boolean regionalSTS) {
    String region = stsRegion;
    if (StringUtils.isBlank(region)) {
      try {
        region = new DefaultAwsRegionProviderChain().getRegion().id();
      } catch (SdkClientException e) {
        log.trace("No region found in Default region chain.");
      }
    }
    // If region still empty, revert to default region.
    region = StringUtils.isBlank(region) ? "us-east-1" : region;
    StsClientBuilder builder = StsClient.builder().region(Region.of(region));
    if (!regionalSTS) {
      builder.endpointOverride(URI.create("https://" + STS_DEFAULT_ENDPOINT));
    }
    return builder;
  }
}
