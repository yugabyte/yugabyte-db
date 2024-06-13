package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.datadog.api.client.ApiClient;
import com.datadog.api.client.v1.api.AuthenticationApi;
import com.datadog.api.client.v1.model.AuthenticationValidationResponse;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collections;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "DataDog Config")
@Slf4j
public class DataDogConfig extends TelemetryProviderConfig {

  @ApiModelProperty(value = "Site", accessMode = READ_WRITE)
  private String site;

  @ApiModelProperty(value = "API Key", accessMode = READ_WRITE)
  private String apiKey;

  public DataDogConfig() {
    setType(ProviderType.DATA_DOG);
  }

  @Override
  public void validate() {
    ApiClient dataDogApiClient = new ApiClient();
    dataDogApiClient.setServerVariables(Collections.singletonMap("site", site));
    dataDogApiClient.configureApiKeys(Collections.singletonMap("apiKeyAuth", apiKey));

    AuthenticationApi apiInstance = new AuthenticationApi(dataDogApiClient);
    AuthenticationValidationResponse response;
    try {
      response = apiInstance.validate();
      if (!response.getValid()) {
        throw new RuntimeException("Validation failed. Got invalid response from DataDog.");
      }
    } catch (Exception e) {
      log.error("Encountered error while validating Datadog API Key: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Validation failed. Ensure your Datadog API Key and Datadog Site URL are valid.");
    }
    log.info("Successfully validated Datadog API Key and Site URL.");
  }
}
