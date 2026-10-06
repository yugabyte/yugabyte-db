// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonDeserializer;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.deser.DeserializationProblemHandler;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.common.operator.ResourceTracker;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import io.yugabyte.operator.v1alpha1.TelemetryProviderSpec;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.AwsCloudWatch;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.DataDog;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Dynatrace;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.GcpCloudMonitoring;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Loki;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Otlp;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.S3;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Splunk;
import java.io.IOException;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Converts the spec of a {@code TelemetryProvider} custom resource into the YBA {@link
 * TelemetryProviderConfig} that {@code com.yugabyte.yw.models.TelemetryProvider} stores.
 *
 * <p>The CRD deliberately names its per-provider properties after the fields of the matching {@code
 * TelemetryProviderConfig} subclass, so the bulk of the conversion is a generic Jackson round trip
 * of the spec sub-object (see {@code ExportTelemetryConfigMapper} for the same technique). Only the
 * credential fields differ, because the CRD models every credential as a reference to a Kubernetes
 * Secret rather than as an inline value. Those references are stripped from the intermediate JSON
 * and replaced with the value read out of the Secret, under the name the Java config uses:
 *
 * <ul>
 *   <li>{@code dataDog.apiKeySecret} -> {@code DataDogConfig.apiKey}
 *   <li>{@code splunk.tokenSecret} -> {@code SplunkConfig.token}
 *   <li>{@code awsCloudWatch.accessKeyIdSecret} -> {@code AWSCloudWatchConfig.accessKey}
 *   <li>{@code awsCloudWatch.secretAccessKeySecret} -> {@code AWSCloudWatchConfig.secretKey}
 *   <li>{@code gcpCloudMonitoring.credentialsSecret} -> {@code
 *       GCPCloudMonitoringConfig.credentialsString}
 *   <li>{@code loki.basicAuth.passwordSecret} -> {@code LokiConfig.basicAuth.password}
 *   <li>{@code dynatrace.apiTokenSecret} -> {@code DynatraceConfig.apiToken}
 *   <li>{@code s3.accessKeyIdSecret} -> {@code S3Config.accessKey}
 *   <li>{@code s3.secretAccessKeySecret} -> {@code S3Config.secretKey}
 *   <li>{@code otlp.basicAuth.passwordSecret} -> {@code OTLPConfig.basicAuth.password}
 *   <li>{@code otlp.bearerToken.tokenSecret} -> {@code OTLPConfig.bearerToken.token}
 * </ul>
 *
 * <p>Note that {@code awsCloudWatch} spells its role field {@code roleARN} while {@code s3} spells
 * it {@code roleArn}: that mirrors the Java classes exactly, which is why the generic conversion
 * handles both without a special case.
 *
 * <p>Secrets are read through {@link OperatorUtils#getAndParseSecretForKey}, which registers the
 * Secret as a tracked dependency of the resource being reconciled. A reference without an explicit
 * namespace resolves in the namespace of the CR. A Secret that does not exist, or that does not
 * hold the requested key, reads as null, and the resulting config is rejected by the YBA config
 * validation the reconciler runs before saving the provider.
 */
@Slf4j
public class TelemetryProviderCrConverter {

  private static final ObjectMapper MAPPER = createMapper();

  private final OperatorUtils operatorUtils;

  @Inject
  public TelemetryProviderCrConverter(OperatorUtils operatorUtils) {
    this.operatorUtils = operatorUtils;
  }

  /**
   * Builds the YBA telemetry provider config described by the given custom resource.
   *
   * @param cr the TelemetryProvider custom resource to convert
   * @param resourceTracker tracker the referenced Secrets are registered with
   * @param owner the resource the Secrets become dependencies of
   * @param localInstanceUuid local PlatformInstance UUID, null when HA is not configured
   * @return the config for the provider named by {@code spec.provider}
   * @throws IllegalArgumentException if the spec is incomplete
   */
  public TelemetryProviderConfig toConfig(
      io.yugabyte.operator.v1alpha1.TelemetryProvider cr,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    if (cr == null || cr.getSpec() == null) {
      throw new IllegalArgumentException("TelemetryProvider resource has no spec");
    }
    TelemetryProviderSpec spec = cr.getSpec();
    TelemetryProviderSpec.Provider provider = spec.getProvider();
    // A Secret reference without an explicit namespace resolves in the namespace of the CR.
    String defaultNamespace = cr.getMetadata() == null ? null : cr.getMetadata().getNamespace();

    switch (provider) {
      case DATA_DOG:
        return toDataDogConfig(
            requireSection(spec.getDataDog(), "dataDog", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      case SPLUNK:
        return toSplunkConfig(
            requireSection(spec.getSplunk(), "splunk", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      case AWS_CLOUDWATCH:
        return toAwsCloudWatchConfig(
            requireSection(spec.getAwsCloudWatch(), "awsCloudWatch", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      case GCP_CLOUD_MONITORING:
        return toGcpCloudMonitoringConfig(
            requireSection(spec.getGcpCloudMonitoring(), "gcpCloudMonitoring", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      case LOKI:
        return toLokiConfig(
            requireSection(spec.getLoki(), "loki", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      case DYNATRACE:
        return toDynatraceConfig(
            requireSection(spec.getDynatrace(), "dynatrace", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      case S3:
        return toS3Config(
            requireSection(spec.getS3(), "s3", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      case OTLP:
        return toOtlpConfig(
            requireSection(spec.getOtlp(), "otlp", provider),
            defaultNamespace,
            resourceTracker,
            owner,
            localInstanceUuid);
      default:
        throw new IllegalArgumentException(
            "Unsupported telemetry provider: " + provider.getValue());
    }
  }

  // ==================== Per provider conversion ====================

  private TelemetryProviderConfig toDataDogConfig(
      DataDog dataDog,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(dataDog);
    node.remove("apiKeySecret");
    var apiKeyRef = requireField(dataDog.getApiKeySecret(), "dataDog.apiKeySecret");
    node.put(
        "apiKey",
        operatorUtils.getAndParseSecretForKey(
            apiKeyRef.getName(),
            StringUtils.defaultIfBlank(apiKeyRef.getNamespace(), defaultNamespace),
            apiKeyRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    return build(node, ProviderType.DATA_DOG);
  }

  private TelemetryProviderConfig toSplunkConfig(
      Splunk splunk,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(splunk);
    node.remove("tokenSecret");
    var tokenRef = requireField(splunk.getTokenSecret(), "splunk.tokenSecret");
    node.put(
        "token",
        operatorUtils.getAndParseSecretForKey(
            tokenRef.getName(),
            StringUtils.defaultIfBlank(tokenRef.getNamespace(), defaultNamespace),
            tokenRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    return build(node, ProviderType.SPLUNK);
  }

  private TelemetryProviderConfig toAwsCloudWatchConfig(
      AwsCloudWatch awsCloudWatch,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(awsCloudWatch);
    node.remove("accessKeyIdSecret");
    node.remove("secretAccessKeySecret");
    var accessKeyRef =
        requireField(awsCloudWatch.getAccessKeyIdSecret(), "awsCloudWatch.accessKeyIdSecret");
    node.put(
        "accessKey",
        operatorUtils.getAndParseSecretForKey(
            accessKeyRef.getName(),
            StringUtils.defaultIfBlank(accessKeyRef.getNamespace(), defaultNamespace),
            accessKeyRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    var secretKeyRef =
        requireField(
            awsCloudWatch.getSecretAccessKeySecret(), "awsCloudWatch.secretAccessKeySecret");
    node.put(
        "secretKey",
        operatorUtils.getAndParseSecretForKey(
            secretKeyRef.getName(),
            StringUtils.defaultIfBlank(secretKeyRef.getNamespace(), defaultNamespace),
            secretKeyRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    return build(node, ProviderType.AWS_CLOUDWATCH);
  }

  private TelemetryProviderConfig toGcpCloudMonitoringConfig(
      GcpCloudMonitoring gcpCloudMonitoring,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(gcpCloudMonitoring);
    node.remove("credentialsSecret");
    var credentialsRef =
        requireField(
            gcpCloudMonitoring.getCredentialsSecret(), "gcpCloudMonitoring.credentialsSecret");
    // credentialsString takes the raw service account JSON; the deprecated JsonNode 'credentials'
    // field is intentionally left unset, setting both is rejected by the config validation.
    node.put(
        "credentialsString",
        operatorUtils.getAndParseSecretForKey(
            credentialsRef.getName(),
            StringUtils.defaultIfBlank(credentialsRef.getNamespace(), defaultNamespace),
            credentialsRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    return build(node, ProviderType.GCP_CLOUD_MONITORING);
  }

  private TelemetryProviderConfig toLokiConfig(
      Loki loki,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(loki);
    var basicAuth = loki.getBasicAuth();
    if (basicAuth != null) {
      ObjectNode basicAuthNode = objectAt(node, "basicAuth");
      basicAuthNode.remove("passwordSecret");
      var passwordRef =
          requireField(basicAuth.getPasswordSecret(), "loki.basicAuth.passwordSecret");
      basicAuthNode.put(
          "password",
          operatorUtils.getAndParseSecretForKey(
              passwordRef.getName(),
              StringUtils.defaultIfBlank(passwordRef.getNamespace(), defaultNamespace),
              passwordRef.getKey(),
              resourceTracker,
              owner,
              localInstanceUuid));
    }
    return build(node, ProviderType.LOKI);
  }

  private TelemetryProviderConfig toDynatraceConfig(
      Dynatrace dynatrace,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(dynatrace);
    node.remove("apiTokenSecret");
    var apiTokenRef = requireField(dynatrace.getApiTokenSecret(), "dynatrace.apiTokenSecret");
    node.put(
        "apiToken",
        operatorUtils.getAndParseSecretForKey(
            apiTokenRef.getName(),
            StringUtils.defaultIfBlank(apiTokenRef.getNamespace(), defaultNamespace),
            apiTokenRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    return build(node, ProviderType.DYNATRACE);
  }

  private TelemetryProviderConfig toS3Config(
      S3 s3,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(s3);
    node.remove("accessKeyIdSecret");
    node.remove("secretAccessKeySecret");
    var accessKeyRef = requireField(s3.getAccessKeyIdSecret(), "s3.accessKeyIdSecret");
    node.put(
        "accessKey",
        operatorUtils.getAndParseSecretForKey(
            accessKeyRef.getName(),
            StringUtils.defaultIfBlank(accessKeyRef.getNamespace(), defaultNamespace),
            accessKeyRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    var secretKeyRef = requireField(s3.getSecretAccessKeySecret(), "s3.secretAccessKeySecret");
    node.put(
        "secretKey",
        operatorUtils.getAndParseSecretForKey(
            secretKeyRef.getName(),
            StringUtils.defaultIfBlank(secretKeyRef.getNamespace(), defaultNamespace),
            secretKeyRef.getKey(),
            resourceTracker,
            owner,
            localInstanceUuid));
    return build(node, ProviderType.S3);
  }

  private TelemetryProviderConfig toOtlpConfig(
      Otlp otlp,
      String defaultNamespace,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    ObjectNode node = toNode(otlp);
    var basicAuth = otlp.getBasicAuth();
    if (basicAuth != null) {
      ObjectNode basicAuthNode = objectAt(node, "basicAuth");
      basicAuthNode.remove("passwordSecret");
      var passwordRef =
          requireField(basicAuth.getPasswordSecret(), "otlp.basicAuth.passwordSecret");
      basicAuthNode.put(
          "password",
          operatorUtils.getAndParseSecretForKey(
              passwordRef.getName(),
              StringUtils.defaultIfBlank(passwordRef.getNamespace(), defaultNamespace),
              passwordRef.getKey(),
              resourceTracker,
              owner,
              localInstanceUuid));
    }
    var bearerToken = otlp.getBearerToken();
    if (bearerToken != null) {
      ObjectNode bearerTokenNode = objectAt(node, "bearerToken");
      bearerTokenNode.remove("tokenSecret");
      var tokenRef = requireField(bearerToken.getTokenSecret(), "otlp.bearerToken.tokenSecret");
      bearerTokenNode.put(
          "token",
          operatorUtils.getAndParseSecretForKey(
              tokenRef.getName(),
              StringUtils.defaultIfBlank(tokenRef.getNamespace(), defaultNamespace),
              tokenRef.getKey(),
              resourceTracker,
              owner,
              localInstanceUuid));
    }
    return build(node, ProviderType.OTLP);
  }

  // ==================== Helpers ====================

  /**
   * Deserializes the prepared JSON into the config subclass registered for the given provider type.
   * The polymorphic {@code type} discriminator is added here rather than taken from the spec, so
   * that the object Jackson builds is always the subclass matching {@code spec.provider}. Each
   * subclass constructor sets its own type, so the value is only used for the subtype lookup.
   */
  private TelemetryProviderConfig build(ObjectNode node, ProviderType type) {
    node.put("type", type.name());
    try {
      return MAPPER.treeToValue(node, TelemetryProviderConfig.class);
    } catch (Exception e) {
      String detail =
          e instanceof JsonProcessingException
              ? ((JsonProcessingException) e).getOriginalMessage()
              : e.getMessage();
      throw new IllegalArgumentException(
          "Failed to convert " + type.name() + " telemetry provider spec: " + detail, e);
    }
  }

  private static ObjectNode toNode(Object specSection) {
    JsonNode node = MAPPER.valueToTree(specSection);
    if (!(node instanceof ObjectNode)) {
      throw new IllegalArgumentException(
          "Expected an object for telemetry provider spec section "
              + specSection.getClass().getSimpleName());
    }
    return (ObjectNode) node;
  }

  /** Returns the named child object, creating an empty one if it is absent. */
  private static ObjectNode objectAt(ObjectNode parent, String field) {
    JsonNode existing = parent.get(field);
    return existing instanceof ObjectNode ? (ObjectNode) existing : parent.putObject(field);
  }

  private static <T> T requireField(T value, String path) {
    if (value == null) {
      throw new IllegalArgumentException(
          "Telemetry provider spec is missing required field '" + path + "'");
    }
    return value;
  }

  private static <T> T requireSection(
      T section, String path, TelemetryProviderSpec.Provider provider) {
    if (section == null) {
      throw new IllegalArgumentException(
          String.format(
              "Telemetry provider spec is missing the '%s' section, which is required for provider"
                  + " %s",
              path, provider.getValue()));
    }
    return section;
  }

  private static ObjectMapper createMapper() {
    ObjectMapper mapper = new ObjectMapper();
    // A CRD property with no counterpart on the YBA config is dropped instead of failing the whole
    // conversion, so that a newer CRD applied to an older YBA still reconciles. The problem handler
    // logs whatever is dropped, so a name that stopped matching is visible in the operator logs.
    mapper.disable(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES);
    mapper.addHandler(
        new DeserializationProblemHandler() {
          @Override
          public boolean handleUnknownProperty(
              DeserializationContext ctxt,
              JsonParser p,
              JsonDeserializer<?> deserializer,
              Object beanOrClass,
              String propertyName)
              throws IOException {
            Class<?> target =
                beanOrClass instanceof Class ? (Class<?>) beanOrClass : beanOrClass.getClass();
            log.warn(
                "Ignoring telemetry provider spec property '{}': {} has no matching field",
                propertyName,
                target.getSimpleName());
            p.skipChildren();
            return true;
          }
        });
    return mapper;
  }
}
