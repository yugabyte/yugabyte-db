// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;
import static play.mvc.Http.Status.UNPROCESSABLE_ENTITY;

import com.azure.core.exception.HttpResponseException;
import com.azure.storage.blob.models.BlobStorageException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.cloud.BaseServiceException;
import com.google.common.collect.ImmutableMap;
import com.oracle.bmc.model.BmcException;
import com.yugabyte.yw.common.PlatformServiceException;
import java.net.ConnectException;
import java.net.NoRouteToHostException;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.net.http.HttpTimeoutException;
import java.util.Iterator;
import java.util.Map;
import javax.net.ssl.SSLException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import software.amazon.awssdk.awscore.exception.AwsServiceException;
import software.amazon.awssdk.core.exception.SdkClientException;

/**
 * Turns a failure of the v1 storage validators into the v2 storage config error.
 *
 * <ul>
 *   <li>The cloud provider answered or couldn't be reached: the validators keep the SDK's exception
 *       as the cause of their error. A provider that is throttling or failing (HTTP 429 or 5xx, or
 *       a request that timed out or whose connection was reset) gives 503
 *       STORAGE_PROVIDER_UNAVAILABLE, any other provider error 422 STORAGE_VALIDATION_FAILED. Both
 *       carry the provider's HTTP status and error code when it gave them. The provider's status
 *       isn't relayed as YBA's own: its 403 or 404 would read as YBA's.
 *   <li>A validator error without such a cause, answered 400 by v1, is a malformed field found
 *       without reaching the storage: 400 STORAGE_CONFIG_INVALID.
 *   <li>Other validator errors (a test object read back wrong, a listing that fails): 422
 *       STORAGE_VALIDATION_FAILED.
 *   <li>Anything else is a YBA bug: 500.
 * </ul>
 *
 * Field errors are keyed by v2 property, in error.errors.
 */
@Slf4j
final class StorageValidationErrors {

  static final String CONFIG_INVALID = "STORAGE_CONFIG_INVALID";
  static final String VALIDATION_FAILED = "STORAGE_VALIDATION_FAILED";
  static final String PROVIDER_UNAVAILABLE = "STORAGE_PROVIDER_UNAVAILABLE";

  // Field names in the v1 validators' errors (keys of the v1 data, prefixed by "data.") and the
  // v2 property they come from.
  private static final Map<String, String> V2_FIELDS =
      ImmutableMap.<String, String>builder()
          .put("BACKUP_LOCATION", "storage.backup_location")
          .put("REGION_LOCATIONS", "storage.region_locations")
          .put("REGION", "storage.region_locations")
          .put("LOCATION", "storage.region_locations")
          .put("AWS_HOST_BASE", "storage.host_base")
          .put("AWS_ACCESS_KEY_ID", "storage.credentials.access_key_id")
          .put("AWS_SECRET_ACCESS_KEY", "storage.credentials.secret_access_key")
          .put("IAM_INSTANCE_PROFILE", "storage.credentials")
          .put("GCS_CREDENTIALS_JSON", "storage.credentials.credentials_json")
          .put("USE_GCP_IAM", "storage.credentials")
          .put("AZURE_STORAGE_SAS_TOKEN", "storage.credentials.sas_token")
          .put("USE_AZURE_IAM", "storage.credentials")
          .put("AZURE_CLIENT_ID", "storage.credentials.client_id")
          .put("NFS_BUCKET", "storage.bucket")
          .put("OCI_REGION", "storage.region")
          .put("OCI_NAMESPACE", "storage.namespace")
          .put("USE_OCI_IAM", "storage.credentials")
          .put("OCI_S3_ACCESS_KEY_ID", "storage.credentials.access_key_id")
          .put("OCI_S3_SECRET_ACCESS_KEY", "storage.credentials.secret_access_key")
          .put("OCI_S3_HOST_BASE", "storage.credentials.host_base")
          .build();

  // Depth of the cause chain searched for an SDK exception.
  private static final int MAX_CAUSE_DEPTH = 8;

  private StorageValidationErrors() {}

  /** The provider's answer to a storage check, or the network failure that prevented one. */
  record ProviderError(int status, String errorCode, boolean unavailable, String message) {}

  static PlatformServiceException toV2Error(Throwable failure) {
    if (failure instanceof PlatformServiceException) {
      PlatformServiceException pse = (PlatformServiceException) failure;
      ProviderError provider = providerError(pse.getCause());
      ObjectNode errors = fieldErrors(pse);
      if (provider != null) {
        return providerFailure(provider, errors);
      }
      if (pse.getHttpStatus() == BAD_REQUEST) {
        return error(BAD_REQUEST, CONFIG_INVALID, errors, pse.getUserVisibleMessage());
      }
      return error(UNPROCESSABLE_ENTITY, VALIDATION_FAILED, errors, pse.getUserVisibleMessage());
    }
    ProviderError provider = providerError(failure);
    if (provider != null) {
      ObjectNode errors = Json.newObject();
      errors.putArray("storage").add(provider.message());
      return providerFailure(provider, errors);
    }
    log.error("Unexpected failure of a storage config check", failure);
    return new PlatformServiceException(
        INTERNAL_SERVER_ERROR, "Unexpected error checking the storage: " + failure.getMessage());
  }

  /**
   * The provider error at the root of a failure, or null when the failure didn't come from a cloud
   * SDK or the network.
   */
  static ProviderError providerError(Throwable failure) {
    Throwable t = failure;
    for (int depth = 0; t != null && depth < MAX_CAUSE_DEPTH; depth++, t = t.getCause()) {
      if (t instanceof AwsServiceException) {
        AwsServiceException aws = (AwsServiceException) t;
        String code = aws.awsErrorDetails() != null ? aws.awsErrorDetails().errorCode() : null;
        return fromStatus(aws.statusCode(), code, aws.getMessage());
      }
      if (t instanceof BaseServiceException && ((BaseServiceException) t).getCode() > 0) {
        BaseServiceException gcp = (BaseServiceException) t;
        return fromStatus(gcp.getCode(), gcp.getReason(), gcp.getMessage());
      }
      if (t instanceof HttpResponseException && ((HttpResponseException) t).getResponse() != null) {
        HttpResponseException azure = (HttpResponseException) t;
        String code =
            t instanceof BlobStorageException && ((BlobStorageException) t).getErrorCode() != null
                ? ((BlobStorageException) t).getErrorCode().toString()
                : null;
        return fromStatus(azure.getResponse().getStatusCode(), code, azure.getMessage());
      }
      if (t instanceof BmcException && ((BmcException) t).getStatusCode() > 0) {
        BmcException oci = (BmcException) t;
        return fromStatus(oci.getStatusCode(), oci.getServiceCode(), oci.getMessage());
      }
    }
    // No HTTP answer: a failure on the way to the provider. The network exceptions count on their
    // own, since the Azure SDK doesn't wrap them in an SDK type as the others do.
    boolean sdkFailure = false;
    t = failure;
    for (int depth = 0; t != null && depth < MAX_CAUSE_DEPTH; depth++, t = t.getCause()) {
      sdkFailure |=
          t instanceof SdkClientException
              || t instanceof BaseServiceException
              || t instanceof HttpResponseException
              || t instanceof BmcException
              || t instanceof UnknownHostException
              || t instanceof ConnectException
              || t instanceof NoRouteToHostException
              || t instanceof SSLException;
      if (isTransientNetworkFailure(t)) {
        return new ProviderError(0, null, true, failure.getMessage());
      }
    }
    return sdkFailure ? new ProviderError(0, null, false, failure.getMessage()) : null;
  }

  private static ProviderError fromStatus(int status, String errorCode, String message) {
    return new ProviderError(status, errorCode, status == 429 || status >= 500, message);
  }

  // A request that timed out or whose connection was reset. An unknown host or a refused
  // connection is more likely a wrong endpoint in the config, so it isn't counted.
  private static boolean isTransientNetworkFailure(Throwable t) {
    if (t instanceof SocketTimeoutException || t instanceof HttpTimeoutException) {
      return true;
    }
    if (t instanceof BmcException && ((BmcException) t).isTimeout()) {
      return true;
    }
    String message = StringUtils.defaultString(t.getMessage());
    return (t instanceof SocketException && message.contains("Connection reset"))
        || message.contains("timed out");
  }

  private static PlatformServiceException providerFailure(
      ProviderError provider, ObjectNode errors) {
    int status = provider.unavailable() ? SERVICE_UNAVAILABLE : UNPROCESSABLE_ENTITY;
    String code = provider.unavailable() ? PROVIDER_UNAVAILABLE : VALIDATION_FAILED;
    ObjectNode error = errorJson(code, errors, provider.message());
    if (provider.status() > 0) {
      error.put("provider_status", provider.status());
    }
    if (provider.errorCode() != null) {
      error.put("provider_error_code", provider.errorCode());
    }
    return new PlatformServiceException(status, error);
  }

  private static PlatformServiceException error(
      int status, String code, ObjectNode errors, String fallbackMessage) {
    return new PlatformServiceException(status, errorJson(code, errors, fallbackMessage));
  }

  private static ObjectNode errorJson(String code, ObjectNode errors, String fallbackMessage) {
    ObjectNode error = Json.newObject();
    error.put("code", code);
    String message = firstMessage(errors);
    error.put("message", message != null ? message : fallbackMessage);
    error.set("errors", errors);
    return error;
  }

  private static String firstMessage(ObjectNode errors) {
    Iterator<Map.Entry<String, JsonNode>> fields = errors.fields();
    if (!fields.hasNext()) {
      return null;
    }
    Map.Entry<String, JsonNode> first = fields.next();
    String message = first.getValue().get(0).asText();
    return first.getKey().isEmpty() ? message : first.getKey() + ": " + message;
  }

  /**
   * The field errors of a v1 validator error, {"data.<V1_FIELD>": [messages], "errorSource": [...]}
   * or a plain message, keyed by v2 property.
   */
  private static ObjectNode fieldErrors(PlatformServiceException e) {
    ObjectNode errors = Json.newObject();
    JsonNode v1Errors = e.getContentJson().get("error");
    if (v1Errors != null && v1Errors.isObject()) {
      Iterator<Map.Entry<String, JsonNode>> fields = v1Errors.fields();
      while (fields.hasNext()) {
        Map.Entry<String, JsonNode> field = fields.next();
        if (field.getKey().equals("errorSource")) {
          continue;
        }
        String v2Field = toV2Field(field.getKey());
        ArrayNode messages =
            errors.has(v2Field) ? (ArrayNode) errors.get(v2Field) : errors.putArray(v2Field);
        if (field.getValue().isArray()) {
          field.getValue().forEach(message -> messages.add(message.asText()));
        } else {
          messages.add(field.getValue().asText());
        }
      }
    } else if (v1Errors != null) {
      errors.putArray("storage").add(v1Errors.asText());
    }
    return errors;
  }

  private static String toV2Field(String v1Field) {
    if (v1Field.isEmpty()) {
      return "";
    }
    String key = StringUtils.removeStart(v1Field, "data.");
    // Field constraint errors name the Java field, e.g. regionLocations[0].location.
    if (key.startsWith("regionLocations")) {
      return "storage.region_locations";
    }
    if (key.equals("backupLocation")) {
      return "storage.backup_location";
    }
    return V2_FIELDS.getOrDefault(key, "storage");
  }
}
