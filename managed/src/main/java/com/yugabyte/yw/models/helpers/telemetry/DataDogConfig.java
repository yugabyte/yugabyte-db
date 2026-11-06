package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collections;
import java.util.List;
import java.util.regex.Pattern;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "DataDog Config")
@Slf4j
public class DataDogConfig extends TelemetryProviderConfig {

  // The API key is sent to https://api.<site>, so only Datadog's own sites are accepted:
  // datadoghq.com and its regional subdomains (us3, us5, ap1, ...), datadoghq.eu and ddog-gov.com.
  private static final Pattern DATADOG_SITE =
      Pattern.compile(
          "^([a-z0-9-]+\\.)?(datadoghq\\.com|datadoghq\\.eu|ddog-gov\\.com)$",
          Pattern.CASE_INSENSITIVE);

  private static final String VALIDATION_ERROR =
      "Validation failed. Ensure your Datadog API Key and Datadog Site URL are valid.";

  @ApiModelProperty(value = "Site", accessMode = READ_WRITE)
  private String site;

  @ApiModelProperty(value = "API Key", accessMode = READ_WRITE)
  private String apiKey;

  public DataDogConfig() {
    setType(ProviderType.DATA_DOG);
  }

  @VisibleForTesting
  static String validationUrl(String site) {
    return "https://api." + site + "/api/v1/validate";
  }

  // The site and key are checked before anything is sent: the OTel collector's Datadog exporter
  // also sends the API key to this site.
  @Override
  public void validate(ApiHelper apiHelper) {
    if (site == null || !DATADOG_SITE.matcher(site).matches()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid Datadog site. Use a Datadog site such as datadoghq.com, us5.datadoghq.com,"
              + " datadoghq.eu or ddog-gov.com.");
    }
    if (StringUtils.isBlank(apiKey)) {
      throw new PlatformServiceException(BAD_REQUEST, "Datadog API Key is required.");
    }

    // Datadog's API key validation endpoint answers {"valid": true} for a usable key. Anything
    // else, including an error body or a failed request, means the key or site is wrong.
    JsonNode response;
    try {
      response =
          apiHelper.getRequest(validationUrl(site), Collections.singletonMap("DD-API-KEY", apiKey));
    } catch (Exception e) {
      log.error("Encountered error while validating Datadog API Key", e);
      throw new PlatformServiceException(BAD_REQUEST, VALIDATION_ERROR);
    }
    if (response == null || !response.path("valid").asBoolean(false)) {
      log.error("Datadog API Key validation failed with response: {}", response);
      throw new PlatformServiceException(BAD_REQUEST, VALIDATION_ERROR);
    }
    log.info("Successfully validated Datadog API Key and Site URL.");
  }

  @Override
  public List<String> secretValues() {
    return nonEmpty(apiKey);
  }
}
