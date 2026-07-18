package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.net.URI;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "Dynatrace Config")
@Slf4j
public class DynatraceConfig extends TelemetryProviderConfig {

  // Required scopes for Dynatrace API token validation
  private static final List<String> REQUIRED_SCOPES =
      Arrays.asList("metrics.ingest", "logs.ingest", "openTelemetryTrace.ingest");

  @ApiModelProperty(value = "Endpoint", accessMode = READ_WRITE)
  private String endpoint;

  @ApiModelProperty(value = "API Token", accessMode = READ_WRITE)
  private String apiToken;

  /**
   * Returns the endpoint with any trailing slashes removed.
   *
   * @return the cleaned endpoint without trailing slashes
   */
  @JsonIgnore
  public String getCleanEndpoint() {
    if (endpoint == null) {
      return null;
    }

    String cleanEndpoint = endpoint;
    while (cleanEndpoint.endsWith("/")) {
      cleanEndpoint = cleanEndpoint.substring(0, cleanEndpoint.length() - 1);
    }
    return cleanEndpoint;
  }

  public DynatraceConfig() {
    setType(ProviderType.DYNATRACE);
  }

  @Override
  public void validateConfigFields() {
    // Validate endpoint format
    URI endpointUri = URI.create(endpoint);
    if (endpointUri.getScheme() == null || endpointUri.getHost() == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid endpoint format. Must be a valid URL.");
    }
  }

  @Override
  public void validateConnectivity(ApiHelper apiHelper) {
    try {
      // Validate API token by checking its scopes
      validateApiTokenScopes(apiHelper);

    } catch (Exception e) {
      log.error("Encountered error while validating Dynatrace configuration: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Validation failed. Ensure your Dynatrace API Token and Endpoint are valid with all"
              + " required permissions.");
    }
  }

  private void validateApiTokenScopes(ApiHelper apiHelper) {
    try {
      // Extract API token ID from the token
      String[] parts = apiToken.split("\\.");
      if (parts.length < 2) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Invalid API token format for Dynatrace. Expected format: <id>.<token>");
      }

      String apiTokenId = parts[0] + "." + parts[1];

      // Build validation URL to check token scopes
      String validationUrl = getCleanEndpoint() + "/api/v2/apiTokens/" + apiTokenId;

      Map<String, String> headers = new HashMap<>();
      headers.put("Authorization", "Api-Token " + apiToken);
      JsonNode response = apiHelper.getRequest(validationUrl, headers);

      // Log response for debugging
      log.debug("Dynatrace read API token response - Body: {}", response.toPrettyString());

      if (response.has("error")) {
        log.error(
            "Dynatrace API validation failed with response body: {}", response.toPrettyString());
        throw new PlatformServiceException(BAD_REQUEST, "Dynatrace API validation failed.");
      }

      // Parse response to check token scopes
      String responseBody = response.toString();
      if (responseBody == null || responseBody.trim().isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Empty response from Dynatrace API. Cannot validate token scopes.");
      }

      // Validate token scopes from the JSON response
      validateTokenScopes(responseBody);

    } catch (Exception e) {
      log.error("Error during Dynatrace API token scope validation: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to validate Dynatrace API token scopes: " + e.getMessage());
    }
  }

  /**
   * Validates that the Dynatrace API token has all required scopes.
   *
   * @param responseBody The JSON response body from the Dynatrace API
   * @throws PlatformServiceException if validation fails
   */
  private void validateTokenScopes(String responseBody) {
    try {
      ObjectMapper mapper = new ObjectMapper();
      JsonNode responseJson = mapper.readTree(responseBody);

      if (!responseJson.has("scopes") || !responseJson.get("scopes").isArray()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Invalid response format from Dynatrace API. Missing or invalid 'scopes' field.");
      }

      ArrayNode scopesArray = (ArrayNode) responseJson.get("scopes");
      List<String> actualScopes = new ArrayList<>();
      for (JsonNode scope : scopesArray) {
        actualScopes.add(scope.asText());
      }

      boolean hasAllRequiredScopes =
          REQUIRED_SCOPES.stream().allMatch(requiredScope -> actualScopes.contains(requiredScope));

      if (!hasAllRequiredScopes) {
        List<String> missingScopes =
            REQUIRED_SCOPES.stream()
                .filter(requiredScope -> !actualScopes.contains(requiredScope))
                .collect(Collectors.toList());

        log.error(
            "Dynatrace API token missing required scopes: {}. Available scopes: {}. Response body:"
                + " {}",
            missingScopes,
            actualScopes,
            responseBody);
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Dynatrace API token missing required scopes: "
                + missingScopes
                + ". Available scopes: "
                + actualScopes);
      }

      log.info(
          "Dynatrace API token validation successful. Token has all required scopes: {}. Available"
              + " scopes: {}",
          REQUIRED_SCOPES,
          actualScopes);

    } catch (Exception e) {
      log.error("Failed to parse Dynatrace API response: {}", responseBody, e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to parse Dynatrace API response: " + e.getMessage());
    }
  }
}
