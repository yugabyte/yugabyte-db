package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints;
import software.amazon.awssdk.auth.credentials.AwsBasicCredentials;
import software.amazon.awssdk.auth.credentials.StaticCredentialsProvider;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.sts.StsClient;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityRequest;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "AWSCloudWatchConfig Config")
@Slf4j
public class AWSCloudWatchConfig extends TelemetryProviderConfig {

  @ApiModelProperty(value = "Log Group", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Log Group is required")
  private String logGroup;

  @ApiModelProperty(value = "Log Stream", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Log Stream is required")
  private String logStream;

  @ApiModelProperty(value = "Region", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Region is required")
  private String region;

  @ApiModelProperty(value = "Role ARN", accessMode = READ_WRITE)
  private String roleARN;

  @ApiModelProperty(value = "End Point", accessMode = READ_WRITE)
  private String endpoint;

  @ApiModelProperty(value = "Access Key", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Access Key is required")
  private String accessKey;

  @ApiModelProperty(value = "Secret Key", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Secret Key is required")
  private String secretKey;

  public AWSCloudWatchConfig() {
    setType(ProviderType.AWS_CLOUDWATCH);
  }

  @Override
  public void validateConnectivity(ApiHelper apiHelper) {
    try {
      StsClient stsClient =
          StsClient.builder()
              .credentialsProvider(
                  StaticCredentialsProvider.create(
                      AwsBasicCredentials.create(accessKey, secretKey)))
              .region(Region.of(region))
              .build();

      stsClient.getCallerIdentity(GetCallerIdentityRequest.builder().build());
    } catch (Exception e) {
      log.error("Encountered error while validating AWS CloudWatch credentials: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Validation failed. Ensure your AWS Access Key and Secret Key are valid: "
              + e.getMessage());
    }
  }
}
