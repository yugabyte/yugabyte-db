// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.amazonaws.SdkClientException;
import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSSessionCredentials;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.AnonymousAWSCredentials;
import com.amazonaws.auth.EC2ContainerCredentialsProviderWrapper;
import com.amazonaws.auth.WebIdentityTokenCredentialsProvider;
import com.amazonaws.auth.profile.ProfileCredentialsProvider;
import com.amazonaws.client.builder.AwsClientBuilder.EndpointConfiguration;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagement;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagementClientBuilder;
import com.amazonaws.services.identitymanagement.model.GetRoleRequest;
import com.amazonaws.services.identitymanagement.model.Role;
import com.amazonaws.services.securitytoken.AWSSecurityTokenService;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClientBuilder;
import com.amazonaws.services.securitytoken.model.AssumeRoleRequest;
import com.amazonaws.services.securitytoken.model.AssumeRoleResult;
import com.amazonaws.services.securitytoken.model.AssumeRoleWithWebIdentityRequest;
import com.amazonaws.services.securitytoken.model.AssumeRoleWithWebIdentityResult;
import com.amazonaws.services.securitytoken.model.Credentials;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityRequest;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityResult;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.IAMConfiguration;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;

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

  public AWSCredentials getTemporaryCredentials(CustomerConfigStorageS3Data s3Data) {
    List<CustomAWSCredentials> credentialsSource = new ArrayList<>();
    switch (s3Data.iamConfig.credentialSource) {
      case ASSUME_INSTANCE_ROLE:
        credentialsSource.add(new AssumeInstanceRole(s3Data.iamConfig, s3Data.fallbackRegion));
        break;
      case EC2_INSTANCE:
        credentialsSource.add(new InstanceProfileCredentials());
        break;
      case IAM_USER:
        credentialsSource.add(new IAMUserCredentials(s3Data.iamConfig.iamUserProfile));
        break;
      case WEB_TOKEN:
        credentialsSource.add(
            new AssumeRoleWithWebIdentity(s3Data.iamConfig, s3Data.fallbackRegion));
        break;
      case DEFAULT:
        log.debug(
            "Trying chain of credential providers in order: WebIdentityTokenCredentialsProvider,"
                + " ProfileCredentialsProvider, AssumeInstanceRole,"
                + " EC2ContainerCredentialsProvider");
        credentialsSource.add(
            new AssumeRoleWithWebIdentity(s3Data.iamConfig, s3Data.fallbackRegion));
        credentialsSource.add(new IAMUserCredentials(s3Data.iamConfig.iamUserProfile));
        credentialsSource.add(new AssumeInstanceRole(s3Data.iamConfig, s3Data.fallbackRegion));
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
          AWSCredentials credentials = credentialSource.getCredentialsOrFail();
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
    AWSCredentials getCredentialsOrFail() throws Exception;

    String getSourceName();
  }

  private static class AssumeInstanceRole implements CustomAWSCredentials {
    IAMConfiguration iamConfig;
    String signingRegion;

    @Override
    public String getSourceName() {
      return "AssumeInstanceRole";
    }

    public AssumeInstanceRole(IAMConfiguration iamConfig, String signingRegion) {
      this.iamConfig = iamConfig;
      this.signingRegion = signingRegion;
    }

    @Override
    public AWSCredentials getCredentialsOrFail() throws SdkClientException {
      int maxDuration = iamConfig.duration;
      String roleArn = null;
      AWSSecurityTokenService stsService =
          getStandardSTSClientWithoutCredentials(signingRegion, iamConfig.regionalSTS)
              .withCredentials(new EC2ContainerCredentialsProviderWrapper())
              .build();

      try {
        // Fetch max session limit for the role.
        GetCallerIdentityResult gcResult =
            stsService.getCallerIdentity(new GetCallerIdentityRequest());
        String assumedRoleArn = gcResult.getArn();
        String[] arnSplit = assumedRoleArn.split("/", 0);
        String role = arnSplit[arnSplit.length - 2];
        AmazonIdentityManagement iamClient =
            AmazonIdentityManagementClientBuilder.standard()
                .withCredentials(new EC2ContainerCredentialsProviderWrapper())
                .build();
        Role iamRole = iamClient.getRole(new GetRoleRequest().withRoleName(role)).getRole();
        roleArn = iamRole.getArn();
        maxDuration = iamRole.getMaxSessionDuration();
      } catch (Exception e) {
        log.debug(
            "Could not get maximum duration for role arn: {}. Using default 1 hour instead.",
            roleArn);
      }
      // Generate temporary credentials valid until the max session duration.
      AssumeRoleRequest roleRequest =
          new AssumeRoleRequest()
              .withDurationSeconds(maxDuration)
              .withRoleArn(roleArn)
              .withRoleSessionName("aws-java-sdk-" + System.currentTimeMillis());
      AssumeRoleResult roleResult = stsService.assumeRole(roleRequest);
      Credentials temporaryCredentials = roleResult.getCredentials();
      AWSSessionCredentials sessionCredentials =
          new AWSSessionCredentials() {
            @Override
            public String getAWSAccessKeyId() {
              return temporaryCredentials.getAccessKeyId();
            }

            @Override
            public String getAWSSecretKey() {
              return temporaryCredentials.getSecretAccessKey();
            }

            @Override
            public String getSessionToken() {
              return temporaryCredentials.getSessionToken();
            }
          };
      return sessionCredentials;
    }
  }

  private static class AssumeRoleWithWebIdentity implements CustomAWSCredentials {
    IAMConfiguration iamConfig;
    String signingRegion;

    @Override
    public String getSourceName() {
      return "WebIdentityTokenCredentialsProvider";
    }

    public AssumeRoleWithWebIdentity(IAMConfiguration iamConfig, String signingRegion) {
      this.iamConfig = iamConfig;
      this.signingRegion = signingRegion;
    }

    @Override
    public AWSCredentials getCredentialsOrFail() throws SdkClientException {
      String webIdentityRoleArn = null;
      String webToken = null;
      int maxDuration = iamConfig.duration;

      // Create STS client to make subsequent calls.
      AWSSecurityTokenService stsService =
          getStandardSTSClientWithoutCredentials(signingRegion, iamConfig.regionalSTS)
              .withCredentials(new AWSStaticCredentialsProvider(new AnonymousAWSCredentials()))
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
        // Fetch max session limit for the role.
        String[] arnSplit = webIdentityRoleArn.split("/", 0);
        AmazonIdentityManagement iamClient =
            AmazonIdentityManagementClientBuilder.standard()
                .withCredentials(new WebIdentityTokenCredentialsProvider())
                .build();
        Role iamRole =
            iamClient
                .getRole(new GetRoleRequest().withRoleName(arnSplit[arnSplit.length - 1]))
                .getRole();
        maxDuration = iamRole.getMaxSessionDuration();
      } catch (Exception e) {
        log.debug(
            "Could not get maximum duration for role arn: {}. Using default 1 hour instead.",
            webIdentityRoleArn);
      }
      AssumeRoleWithWebIdentityRequest stsWebIdentityRequest =
          new AssumeRoleWithWebIdentityRequest()
              .withRoleArn(webIdentityRoleArn)
              .withWebIdentityToken(webToken)
              .withRoleSessionName("aws-java-sdk-" + System.currentTimeMillis())
              .withDurationSeconds(maxDuration);
      AssumeRoleWithWebIdentityResult roleResult =
          stsService.assumeRoleWithWebIdentity(stsWebIdentityRequest);
      Credentials temporaryCredentials = roleResult.getCredentials();
      AWSSessionCredentials sessionCredentials =
          new AWSSessionCredentials() {
            @Override
            public String getAWSAccessKeyId() {
              return temporaryCredentials.getAccessKeyId();
            }

            @Override
            public String getAWSSecretKey() {
              return temporaryCredentials.getSecretAccessKey();
            }

            @Override
            public String getSessionToken() {
              return temporaryCredentials.getSessionToken();
            }
          };
      return sessionCredentials;
    }
  }

  private static class InstanceProfileCredentials implements CustomAWSCredentials {

    public InstanceProfileCredentials() {}

    @Override
    public String getSourceName() {
      return "EC2ContainerCredentialsProvider";
    }

    @Override
    public AWSCredentials getCredentialsOrFail() throws Exception {
      AWSCredentialsProvider ec2CredentialsProvider = new EC2ContainerCredentialsProviderWrapper();
      ec2CredentialsProvider.refresh();
      return ec2CredentialsProvider.getCredentials();
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
    public AWSCredentials getCredentialsOrFail() throws Exception {
      return new ProfileCredentialsProvider(profileName).getCredentials();
    }
  }

  private static AWSSecurityTokenServiceClientBuilder getStandardSTSClientWithoutCredentials(
      String fallbackRegion, boolean regionalSTS) {
    fallbackRegion = AWSUtil.getClientRegion(fallbackRegion);
    String stsEndpoint = STS_DEFAULT_ENDPOINT;
    if (regionalSTS) {
      stsEndpoint = String.format("sts.%s.amazonaws.com", fallbackRegion);
    }
    return AWSSecurityTokenServiceClientBuilder.standard()
        .withEndpointConfiguration(new EndpointConfiguration(stsEndpoint, fallbackRegion));
  }
}
