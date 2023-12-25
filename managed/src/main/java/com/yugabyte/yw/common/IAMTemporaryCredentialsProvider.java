package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.FAILED_DEPENDENCY;
import static play.mvc.Http.Status.BAD_REQUEST;

import java.io.File;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.TimeZone;
import java.util.stream.Stream;

import org.apache.commons.collections4.MapUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;

import com.amazonaws.SDKGlobalConfiguration;
import com.amazonaws.SdkClientException;
import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSSessionCredentials;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.AnonymousAWSCredentials;
import com.amazonaws.auth.EC2ContainerCredentialsProviderWrapper;
import com.amazonaws.auth.InstanceProfileCredentialsProvider;
import com.amazonaws.auth.WebIdentityTokenCredentialsProvider;
import com.amazonaws.auth.profile.ProfileCredentialsProvider;
import com.amazonaws.client.builder.AwsClientBuilder;
import com.amazonaws.client.builder.AwsClientBuilder.EndpointConfiguration;
import com.amazonaws.regions.AwsRegionProvider;
import com.amazonaws.regions.AwsRegionProviderChain;
import com.amazonaws.regions.DefaultAwsRegionProviderChain;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagement;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagementClientBuilder;
import com.amazonaws.services.identitymanagement.model.GetRoleRequest;
import com.amazonaws.services.identitymanagement.model.Role;
import com.amazonaws.services.s3.internal.RegionalEndpointsOptionResolver;
import com.amazonaws.services.securitytoken.AWSSecurityTokenService;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClient;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClientBuilder;
import com.amazonaws.services.securitytoken.model.AssumeRoleRequest;
import com.amazonaws.services.securitytoken.model.AssumeRoleResult;
import com.amazonaws.services.securitytoken.model.AssumeRoleWithWebIdentityRequest;
import com.amazonaws.services.securitytoken.model.AssumeRoleWithWebIdentityResult;
import com.amazonaws.services.securitytoken.model.Credentials;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityRequest;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityResult;
import com.amazonaws.util.EC2MetadataUtils;
import com.amazonaws.util.EC2MetadataUtils.IAMSecurityCredential;
import com.google.auth.oauth2.AwsCredentials;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.IAMConfiguration;

import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class IAMTemporaryCredentialsProvider {

  public static final String STS_DEFAULT_ENDPOINT = "sts.amazonaws.com";

  public enum IAMCredentialSource {
    DEFAULT("default"),
    IAM_USER("iam_user"),
    WEB_TOKEN("web_token"),
    ASSUME_INSTANCE_ROLE("assume_instance_role"),
    EC2_INSTANCE("ec2_instance");

    private final String enumVal;

    IAMCredentialSource(String enumVal) {
      this.enumVal = enumVal;
    }

    public String getValue() {
      return enumVal;
    }
  }

  public AWSCredentials getTemporaryCredentials(CustomerConfigStorageS3Data s3Data)
      throws Exception {
    return CustomAWSCredentials.getTemporaryCredentials(s3Data);
  }

  private interface CustomAWSCredentials {
    AWSCredentials getCredentialsOrFail() throws Exception;

    default Optional<AWSCredentials> optionalGetCredentials() {
      try {
        return Optional.of(getCredentialsOrFail());
      } catch (Exception e) {
        return Optional.empty();
      }
    }

    static AWSCredentials getTemporaryCredentials(CustomerConfigStorageS3Data s3Data) {
      try {
        switch (s3Data.iamConfig.credentialSource) {
          case ASSUME_INSTANCE_ROLE:
            return new AssumeInstanceRole(s3Data.iamConfig, s3Data.fallbackRegion)
                .getCredentialsOrFail();
          case EC2_INSTANCE:
            return new InstanceProfileCredentials().getCredentialsOrFail();
          case IAM_USER:
            return new IAMUserCredentials(s3Data.iamConfig.iamUserProfile).getCredentialsOrFail();
          case WEB_TOKEN:
            return new AssumeRoleWithWebIdentity(s3Data.iamConfig, s3Data.fallbackRegion)
                .getCredentialsOrFail();
          case DEFAULT:
            Optional<AWSCredentials> creds =
                Stream.of(
                        new AssumeRoleWithWebIdentity(s3Data.iamConfig, s3Data.fallbackRegion),
                        new IAMUserCredentials(s3Data.iamConfig.iamUserProfile),
                        new AssumeInstanceRole(s3Data.iamConfig, s3Data.fallbackRegion),
                        new InstanceProfileCredentials())
                    .map(CustomAWSCredentials::optionalGetCredentials)
                    .filter(Optional::isPresent)
                    .findFirst()
                    .get();
            if (creds.isPresent()) {
              return creds.get();
            } else {
              throw new RuntimeException("No credential found in chain.");
            }
          default:
            throw new RuntimeException("Invalid IAM credential source option");
        }
      } catch (Exception e) {
        throw new RuntimeException(e.getCause());
      }
    }
  }

  private static class AssumeInstanceRole implements CustomAWSCredentials {
    IAMConfiguration iamConfig;
    String signingRegion;

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
        log.error(
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

    public AssumeRoleWithWebIdentity(IAMConfiguration iamConfig, String signingRegion) {
      this.iamConfig = iamConfig;
      this.signingRegion = signingRegion;
    }

    @Override
    public AWSCredentials getCredentialsOrFail() throws Exception {
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
      try {
        webIdentityRoleArn = System.getenv("AWS_ROLE_ARN");
        if (StringUtils.isBlank(webIdentityRoleArn)) {
          throw new RuntimeException("AWS_ROLE_ARN: blank variable value.");
        }
      } catch (Exception e) {
        throw new RuntimeException(
            "AWS_ROLE_ARN not found for Web Identity assume role.", e.getCause());
      }

      // Fetch web-token for making the Assume role request.
      try {
        webToken =
            FileUtils.readFileToString(
                new File(System.getenv("AWS_WEB_IDENTITY_TOKEN_FILE")), "UTF-8");
      } catch (Exception e) {
        throw new RuntimeException(
            "Could not get web token for Assume role request.", e.getCause());
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
        log.error(
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
    public AWSCredentials getCredentialsOrFail() throws Exception {
      AWSCredentialsProvider ec2CredentialsProvider = new EC2ContainerCredentialsProviderWrapper();
      ec2CredentialsProvider.refresh();
      return ec2CredentialsProvider.getCredentials();
    }
  }

  private static class IAMUserCredentials implements CustomAWSCredentials {

    String profileName;

    public IAMUserCredentials(String profileName) {
      this.profileName = profileName;
    }

    @Override
    public AWSCredentials getCredentialsOrFail() throws Exception {
      return new ProfileCredentialsProvider(profileName).getCredentials();
    }
  }

  public static AWSSecurityTokenServiceClientBuilder getStandardSTSClientWithoutCredentials(
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
