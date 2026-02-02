package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.io.ByteArrayInputStream;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.data.validation.Constraints;
import software.amazon.awssdk.auth.credentials.AwsBasicCredentials;
import software.amazon.awssdk.auth.credentials.StaticCredentialsProvider;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.PutObjectRequest;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "S3 Config")
@Slf4j
public class S3Config extends TelemetryProviderConfig {

  @ApiModelProperty(value = "Region", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Region is required")
  @Size(min = 1)
  private String region;

  @ApiModelProperty(value = "S3 bucket", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Bucket is required")
  @Size(min = 1)
  private String bucket;

  @ApiModelProperty(value = "S3 Prefix Key (root directory inside bucket)", accessMode = READ_WRITE)
  private String directoryPrefix = "yb-logs/";

  @Getter
  public enum S3Partition {
    hour("hour"),
    minute("minute");

    private final String granularity;

    S3Partition(String granularity) {
      this.granularity = granularity;
    }

    @Override
    public String toString() {
      return this.name();
    }

    public static S3Partition fromString(String input) {
      for (S3Partition granularity : S3Partition.values()) {
        if (granularity.granularity.equalsIgnoreCase(input)) {
          return granularity;
        }
      }
      throw new IllegalArgumentException(
          "No enum constant " + S3Partition.class.getName() + "." + input);
    }
  }

  @ApiModelProperty(value = "S3 Partition", accessMode = READ_WRITE)
  private S3Partition partition = S3Partition.minute;

  @ApiModelProperty(value = "Role ARN", accessMode = READ_WRITE)
  private String roleArn;

  @ApiModelProperty(value = "File prefix", accessMode = READ_WRITE)
  private String filePrefix = "yb-otel-";

  @ApiModelProperty(value = "Marshaler", accessMode = READ_WRITE)
  private OTLPMarshaler marshaler = OTLPMarshaler.OTLP_JSON;

  @ApiModelProperty(value = "Disable SSL", accessMode = READ_WRITE)
  private Boolean disableSSL = false;

  @ApiModelProperty(value = "Access Key", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Access Key is required")
  @Size(min = 1)
  private String accessKey;

  @ApiModelProperty(value = "Secret Key", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Secret Key is required")
  @Size(min = 1)
  private String secretKey;

  @ApiModelProperty(
      value =
          "Force Path Style. Set this to true to force the request to use path-style addressing",
      accessMode = READ_WRITE)
  private Boolean forcePathStyle = false;

  @ApiModelProperty(
      value =
          "Endpoint. Overrides the endpoint used by the exporter instead of constructing it from"
              + " region and bucket",
      accessMode = READ_WRITE)
  private String endpoint;

  @ApiModelProperty(
      value =
          "When true, appends universe UUID and node name to the S3 prefix."
              + " Example: 'yb-logs/<universe-uuid>/<node-name>'",
      accessMode = READ_WRITE)
  private Boolean includeUniverseAndNodeInPrefix = false;

  public S3Config() {
    setType(ProviderType.S3);
  }

  /**
   * Normalizes an endpoint URL by adding the protocol if not present. If the endpoint already has
   * http:// or https://, it's used as-is. Otherwise, https:// is prepended as the default.
   *
   * @param endpoint the endpoint URL to normalize
   * @return the normalized endpoint URL with protocol
   */
  public static String normalizeEndpoint(String endpoint) {
    if (StringUtils.isBlank(endpoint)) {
      return endpoint;
    }
    if (!endpoint.startsWith("http://") && !endpoint.startsWith("https://")) {
      return "https://" + endpoint;
    }
    return endpoint;
  }

  @Override
  public void validateConfigFields() {
    if (StringUtils.isBlank(region)) {
      throw new PlatformServiceException(BAD_REQUEST, "Region is required");
    }

    if (StringUtils.isBlank(bucket)) {
      throw new PlatformServiceException(BAD_REQUEST, "Bucket is required");
    }

    if (StringUtils.isBlank(accessKey)) {
      throw new PlatformServiceException(BAD_REQUEST, "Access Key is required");
    }

    if (StringUtils.isBlank(secretKey)) {
      throw new PlatformServiceException(BAD_REQUEST, "Secret Key is required");
    }

    if (StringUtils.isNotBlank(roleArn) && !roleArn.startsWith("arn:")) {
      throw new PlatformServiceException(BAD_REQUEST, "Role ARN is invalid");
    }
  }

  @Override
  public void validateConnectivity(ApiHelper apiHelper) {
    var builder =
        S3Client.builder()
            .region(Region.of(region))
            .credentialsProvider(
                StaticCredentialsProvider.create(AwsBasicCredentials.create(accessKey, secretKey)));

    if (forcePathStyle) {
      builder.forcePathStyle(true);
    }

    if (StringUtils.isNotBlank(endpoint)) {
      builder.endpointOverride(URI.create(normalizeEndpoint(endpoint)));
    }

    S3Client s3 = builder.build();

    // Create a unique key with prefix so we don't overwrite anything
    String key = directoryPrefix + "otel-preflight-" + Instant.now().toEpochMilli() + ".txt";

    try {
      byte[] data =
          "otel preflight test - put only. This file can be safely deleted."
              .getBytes(StandardCharsets.UTF_8);

      s3.putObject(
          PutObjectRequest.builder().bucket(bucket).key(key).build(),
          software.amazon.awssdk.core.sync.RequestBody.fromInputStream(
              new ByteArrayInputStream(data), data.length));

      log.info(
          "Successfully performed PutObject to s3://{}/{}. This file can be safely deleted.",
          bucket,
          key);

    } catch (Exception e) {
      StringBuilder errorMsg =
          new StringBuilder("S3 PutObject validation failed. Please check the following: ");
      errorMsg.append(e.getMessage());
      log.error(errorMsg.toString(), e);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg.toString());
    } finally {
      s3.close();
    }
  }
}
