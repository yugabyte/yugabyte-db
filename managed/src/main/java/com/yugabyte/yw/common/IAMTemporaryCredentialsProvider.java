// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.common.base.Throwables;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.IAMConfiguration;
import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import software.amazon.awssdk.auth.credentials.*;
import software.amazon.awssdk.awscore.exception.AwsServiceException;
import software.amazon.awssdk.core.exception.AbortedException;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.core.exception.SdkServiceException;
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
  private static final String IDP_COMMUNICATION_ERROR = "IDPCommunicationError";

  /** How long {@link #hasCredentialsOffAws} may take in all. */
  static final Duration OFF_AWS_CHECK_TIMEOUT = Duration.ofSeconds(10);

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

  private List<CustomAWSCredentials> credentialSources(CustomerConfigStorageS3Data s3Data) {
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
    return credentialsSource;
  }

  public AwsCredentials getTemporaryCredentials(CustomerConfigStorageS3Data s3Data) {
    List<CustomAWSCredentials> credentialsSource = credentialSources(s3Data);
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

  /**
   * Whether the config's credential sources have something to get AWS credentials from on a host
   * that is not on AWS, so whether {@link #getTemporaryCredentials} is worth leaving to its retries
   * there. Each source is resolved once, through the same SDK calls. One that gives credentials
   * counts, and so does one that fails on an error those retries can get past: an AWS throttle or
   * server error, STS failing to reach the token's identity provider, or a network error such as a
   * timeout. One that fails otherwise does not: the host has nothing for it (no AWS_ROLE_ARN or web
   * identity token file, no credentials in the profile, no container credentials endpoint), or AWS
   * rejects what it has, as for a role that does not trust the host's token or a token that has
   * expired. Neither does AWS instance metadata, which only AWS serves. As this runs on request
   * threads, it runs on {@code executor}, a bounded shared pool, and gives up after {@link
   * #OFF_AWS_CHECK_TIMEOUT} or when the pool has no room: a source still waiting on AWS then
   * counts, as for a timeout.
   */
  public boolean hasCredentialsOffAws(
      CustomerConfigStorageS3Data s3Data, ExecutorService executor) {
    Optional<Boolean> found =
        resultWithin(() -> resolveOffAws(s3Data), OFF_AWS_CHECK_TIMEOUT, executor);
    if (found.isEmpty()) {
      log.warn(
          "Resolving the IAM credential sources did not finish within {}, leaving them to the"
              + " client's retries",
          OFF_AWS_CHECK_TIMEOUT);
      return true;
    }
    return found.get();
  }

  private boolean resolveOffAws(CustomerConfigStorageS3Data s3Data) {
    List<String> absent = new ArrayList<>();
    boolean rejectedByAws = false;
    for (CustomAWSCredentials source : credentialSources(s3Data)) {
      if (Thread.currentThread().isInterrupted()) {
        // The caller gave up waiting: make no more calls for an answer nobody reads.
        return true;
      }
      if (source instanceof InstanceProfileCredentials) {
        absent.add(source.getSourceName() + ": no AWS instance metadata off AWS");
        continue;
      }
      try {
        source.getCredentialsOrFail();
        return true;
      } catch (Exception e) {
        if (isInterruption(e)) {
          // The caller gave up waiting, and the SDK may have cleared the interrupt.
          return true;
        }
        rejectedByAws |=
            Throwables.getCausalChain(e).stream().anyMatch(SdkServiceException.class::isInstance);
        if (isRetryableFailure(e)) {
          log.warn(
              "IAM credential source {} is set up on this host but failed, leaving it to the"
                  + " client's retries: {}",
              source.getSourceName(),
              e.getMessage());
          return true;
        }
        absent.add(source.getSourceName() + ": " + e.getMessage());
      }
    }
    if (rejectedByAws) {
      log.warn("AWS refused the IAM credential sources set up on this host: {}", absent);
    } else {
      log.info("No IAM credential source has anything to use on this host: {}", absent);
    }
    return false;
  }

  /**
   * The result of {@code work} run on {@code executor}, or empty if {@code executor} has no room
   * for it or it takes longer than {@code timeout}, in which case it is interrupted. What {@code
   * work} throws is thrown here.
   */
  static <T> Optional<T> resultWithin(
      Callable<T> work, Duration timeout, ExecutorService executor) {
    Future<T> result;
    try {
      result = executor.submit(work);
    } catch (RejectedExecutionException e) {
      return Optional.empty();
    }
    try {
      return Optional.of(result.get(timeout.toMillis(), TimeUnit.MILLISECONDS));
    } catch (TimeoutException e) {
      result.cancel(true /* mayInterruptIfRunning */);
      return Optional.empty();
    } catch (InterruptedException e) {
      result.cancel(true /* mayInterruptIfRunning */);
      Thread.currentThread().interrupt();
      throw new RuntimeException("Interrupted while resolving the IAM credential sources", e);
    } catch (ExecutionException e) {
      Throwables.throwIfUnchecked(e.getCause());
      throw new RuntimeException(e.getCause());
    }
  }

  /**
   * Whether {@code e} is an error that trying again can get past: a throttle or a server error in
   * the reply from AWS, STS failing to reach the token's identity provider (IDPCommunicationError,
   * which AWS documents as often transient), or an I/O error the SDK hit on the way. Any other
   * reply, such as AccessDenied or InvalidIdentityToken, is a rejection that every retry gets
   * again, and an I/O error outside an SDK call, such as an unreadable profile file, is the host's
   * own.
   */
  static boolean isRetryableFailure(Throwable e) {
    boolean inSdkCall = false;
    for (Throwable t = e; t != null; t = t.getCause()) {
      if (t instanceof SdkServiceException) {
        SdkServiceException reply = (SdkServiceException) t;
        return reply.isThrottlingException()
            || reply.statusCode() >= 500
            || (reply instanceof AwsServiceException
                && ((AwsServiceException) reply).awsErrorDetails() != null
                && IDP_COMMUNICATION_ERROR.equals(
                    ((AwsServiceException) reply).awsErrorDetails().errorCode()));
      }
      inSdkCall |= t instanceof SdkClientException;
      if (t instanceof IOException && inSdkCall) {
        return true;
      }
    }
    return false;
  }

  /** Whether {@code e} comes from the thread being interrupted. */
  private static boolean isInterruption(Throwable e) {
    return Throwables.getCausalChain(e).stream()
        .anyMatch(t -> t instanceof InterruptedException || t instanceof AbortedException);
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
      RuntimeException roleLookupFailure = null;
      try (ContainerCredentialsProvider containerCredentials =
              ContainerCredentialsProvider.create();
          StsClient stsService =
              getStsClientBuilderWithoutCredentials(stsRegion, iamConfig.regionalSTS)
                  .credentialsProvider(containerCredentials)
                  .build()) {
        try {
          // Fetch max session limit for the role.
          GetCallerIdentityResponse gcResult =
              stsService.getCallerIdentity(GetCallerIdentityRequest.builder().build());
          String assumedRoleArn = gcResult.arn();
          String[] arnSplit = assumedRoleArn.split("/", 0);
          String role = arnSplit[arnSplit.length - 2];
          try (IamClient iamClient =
              IamClient.builder().credentialsProvider(containerCredentials).build()) {
            Role iamRole =
                iamClient.getRole(GetRoleRequest.builder().roleName(role).build()).role();
            roleArn = iamRole.arn();
            maxDuration = iamRole.maxSessionDuration();
          }
        } catch (RuntimeException e) {
          roleLookupFailure = e;
          log.debug(
              "Could not get maximum duration for role arn: {}. Using default 1 hour instead.",
              roleArn);
        }
        if (roleArn == null && roleLookupFailure != null) {
          // AssumeRole needs the role ARN, so it could only fail: report why there is none.
          throw roleLookupFailure;
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
      int maxDuration = iamConfig.duration;

      // Create STS client to make subsequent calls.
      try (StsClient stsService =
          getStsClientBuilderWithoutCredentials(stsRegion, iamConfig.regionalSTS)
              .credentialsProvider(AnonymousCredentialsProvider.create())
              .build()) {
        return assumeRoleWithWebIdentity(stsService, maxDuration);
      }
    }

    private AwsCredentials assumeRoleWithWebIdentity(StsClient stsService, int maxDuration) {
      String webIdentityRoleArn = null;
      String webToken = null;

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

        try (WebIdentityTokenFileCredentialsProvider tokenCredentials =
                WebIdentityTokenFileCredentialsProvider.create();
            IamClient iamClient =
                IamClient.builder().credentialsProvider(tokenCredentials).build()) {
          Role iamRole =
              iamClient.getRole(GetRoleRequest.builder().roleName(roleName).build()).role();
          maxDuration = iamRole.maxSessionDuration();
        }
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
      try (InstanceProfileCredentialsProvider instanceCredentials =
          InstanceProfileCredentialsProvider.create()) {
        return instanceCredentials.resolveCredentials();
      }
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
      try (ProfileCredentialsProvider profileCredentials =
          ProfileCredentialsProvider.builder().profileName(profileName).build()) {
        return profileCredentials.resolveCredentials();
      }
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
