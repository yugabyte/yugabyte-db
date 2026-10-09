// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.common.operator.ResourceTracker;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.AuthCredentials;
import com.yugabyte.yw.models.helpers.telemetry.CompressionType;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.DynatraceConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExporterRetryConfig;
import com.yugabyte.yw.models.helpers.telemetry.GCPCloudMonitoringConfig;
import com.yugabyte.yw.models.helpers.telemetry.LokiConfig;
import com.yugabyte.yw.models.helpers.telemetry.OTLPConfig;
import com.yugabyte.yw.models.helpers.telemetry.OTLPMarshaler;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import com.yugabyte.yw.models.helpers.telemetry.S3Config;
import com.yugabyte.yw.models.helpers.telemetry.SplunkConfig;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import io.fabric8.kubernetes.api.model.ObjectMeta;
// io.yugabyte.operator.v1alpha1.TelemetryProvider is the custom resource. The YBA model of the same
// simple name (com.yugabyte.yw.models.TelemetryProvider) is not needed here; the converter only
// produces a TelemetryProviderConfig.
import io.yugabyte.operator.v1alpha1.TelemetryProvider;
import io.yugabyte.operator.v1alpha1.TelemetryProviderSpec;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.AwsCloudWatch;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.DataDog;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Dynatrace;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.GcpCloudMonitoring;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Loki;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Otlp;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.S3;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.Splunk;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.RetryOnFailure;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

/**
 * Unit tests for {@link TelemetryProviderCrConverter}.
 *
 * <p>{@link OperatorUtils} is mocked so that {@code getAndParseSecretForKey} answers from a canned
 * map keyed by (namespace, secret name, key). A lookup that is not in the map returns null, which
 * is exactly what the real method does for a Secret that does not exist or that has no such key.
 */
@RunWith(MockitoJUnitRunner.class)
public class TelemetryProviderCrConverterTest {

  private static final String CR_NAMESPACE = "yb-operator";
  private static final String OTHER_NAMESPACE = "shared-secrets";

  private OperatorUtils mockOperatorUtils;
  private ResourceTracker mockResourceTracker;
  private TelemetryProviderCrConverter converter;
  private UUID localInstanceUuid;

  /** Canned Secret contents, keyed by {@link #secretMapKey(String, String, String)}. */
  private final Map<String, String> cannedSecrets = new HashMap<>();

  @Before
  public void setup() {
    mockOperatorUtils = Mockito.mock(OperatorUtils.class);
    mockResourceTracker = Mockito.mock(ResourceTracker.class);
    localInstanceUuid = UUID.randomUUID();
    converter = new TelemetryProviderCrConverter(mockOperatorUtils);

    // lenient(): the error-path tests fail before any Secret is read.
    lenient()
        .when(
            mockOperatorUtils.getAndParseSecretForKey(
                anyString(), nullable(String.class), anyString(), any(), any(), any()))
        .thenAnswer(
            invocation -> {
              String name = invocation.getArgument(0);
              String namespace = invocation.getArgument(1);
              String key = invocation.getArgument(2);
              return cannedSecrets.get(secretMapKey(namespace, name, key));
            });
  }

  // ==================== Per provider mapping ====================

  @Test
  public void testDataDogConfig() {
    putSecret(CR_NAMESPACE, "datadog-creds", "api-key", "dd-api-key-value");

    TelemetryProvider cr = baseCr("dd", TelemetryProviderSpec.Provider.DATA_DOG);
    DataDog dataDog = new DataDog();
    dataDog.setSite("us3.datadoghq.com");
    dataDog.setApiKeySecret(dataDogApiKeyRef("datadog-creds", null, "api-key"));
    cr.getSpec().setDataDog(dataDog);

    TelemetryProviderConfig config = convert(cr);

    DataDogConfig dataDogConfig = assertConfigType(config, DataDogConfig.class);
    assertEquals(ProviderType.DATA_DOG, dataDogConfig.getType());
    assertEquals("us3.datadoghq.com", dataDogConfig.getSite());
    assertEquals("dd-api-key-value", dataDogConfig.getApiKey());
  }

  @Test
  public void testSplunkConfig() {
    putSecret(CR_NAMESPACE, "splunk-creds", "hec-token", "splunk-token-value");

    TelemetryProvider cr = baseCr("splunk", TelemetryProviderSpec.Provider.SPLUNK);
    Splunk splunk = new Splunk();
    splunk.setEndpoint("https://splunk.example.com:8088/services/collector");
    splunk.setIndex("yb-index");
    splunk.setSource("yb-source");
    splunk.setSourceType("yb-source-type");
    splunk.setTokenSecret(splunkTokenRef("splunk-creds", null, "hec-token"));
    cr.getSpec().setSplunk(splunk);

    TelemetryProviderConfig config = convert(cr);

    SplunkConfig splunkConfig = assertConfigType(config, SplunkConfig.class);
    assertEquals(ProviderType.SPLUNK, splunkConfig.getType());
    assertEquals("https://splunk.example.com:8088/services/collector", splunkConfig.getEndpoint());
    assertEquals("yb-index", splunkConfig.getIndex());
    assertEquals("yb-source", splunkConfig.getSource());
    assertEquals("yb-source-type", splunkConfig.getSourceType());
    assertEquals("splunk-token-value", splunkConfig.getToken());
  }

  @Test
  public void testAwsCloudWatchConfig() {
    putSecret(CR_NAMESPACE, "aws-creds", "access-key-id", "AKIAEXAMPLE");
    putSecret(CR_NAMESPACE, "aws-creds", "secret-access-key", "aws-secret-value");

    TelemetryProvider cr = baseCr("cw", TelemetryProviderSpec.Provider.AWS_CLOUDWATCH);
    AwsCloudWatch awsCloudWatch = new AwsCloudWatch();
    awsCloudWatch.setLogGroup("yb-log-group");
    awsCloudWatch.setLogStream("yb-log-stream");
    awsCloudWatch.setRegion("us-west-2");
    awsCloudWatch.setRoleARN("arn:aws:iam::123456789012:role/yb-cloudwatch");
    awsCloudWatch.setEndpoint("https://logs.us-west-2.amazonaws.com");
    awsCloudWatch.setAccessKeyIdSecret(awsAccessKeyIdRef("aws-creds", null, "access-key-id"));
    awsCloudWatch.setSecretAccessKeySecret(
        awsSecretAccessKeyRef("aws-creds", null, "secret-access-key"));
    cr.getSpec().setAwsCloudWatch(awsCloudWatch);

    TelemetryProviderConfig config = convert(cr);

    AWSCloudWatchConfig cwConfig = assertConfigType(config, AWSCloudWatchConfig.class);
    assertEquals(ProviderType.AWS_CLOUDWATCH, cwConfig.getType());
    assertEquals("yb-log-group", cwConfig.getLogGroup());
    assertEquals("yb-log-stream", cwConfig.getLogStream());
    assertEquals("us-west-2", cwConfig.getRegion());
    assertEquals("arn:aws:iam::123456789012:role/yb-cloudwatch", cwConfig.getRoleARN());
    assertEquals("https://logs.us-west-2.amazonaws.com", cwConfig.getEndpoint());
    assertEquals("AKIAEXAMPLE", cwConfig.getAccessKey());
    assertEquals("aws-secret-value", cwConfig.getSecretKey());
  }

  @Test
  public void testGcpCloudMonitoringConfig() {
    String serviceAccountJson =
        "{\"type\":\"service_account\",\"project_id\":\"yb-project\","
            + "\"private_key_id\":\"abc123\"}";
    putSecret(CR_NAMESPACE, "gcp-creds", "credentials.json", serviceAccountJson);

    TelemetryProvider cr = baseCr("gcm", TelemetryProviderSpec.Provider.GCP_CLOUD_MONITORING);
    GcpCloudMonitoring gcpCloudMonitoring = new GcpCloudMonitoring();
    gcpCloudMonitoring.setProject("yb-project");
    gcpCloudMonitoring.setCredentialsSecret(
        gcpCredentialsRef("gcp-creds", null, "credentials.json"));
    cr.getSpec().setGcpCloudMonitoring(gcpCloudMonitoring);

    TelemetryProviderConfig config = convert(cr);

    GCPCloudMonitoringConfig gcpConfig = assertConfigType(config, GCPCloudMonitoringConfig.class);
    assertEquals(ProviderType.GCP_CLOUD_MONITORING, gcpConfig.getType());
    assertEquals("yb-project", gcpConfig.getProject());
    assertEquals(serviceAccountJson, gcpConfig.getCredentialsString());
    // GCPCloudMonitoringConfig.validateConfigFields() rejects a config that sets both the
    // deprecated JsonNode 'credentials' and 'credentialsString', so only the latter may be set.
    assertNull("deprecated 'credentials' JsonNode must stay unset", gcpConfig.getCredentials());
    // The credentials are still resolvable through the accessor that reads either field.
    assertNotNull(gcpConfig.getGcmCredentials());
    assertEquals("yb-project", gcpConfig.getGcmCredentials().get("project_id").asText());
  }

  @Test
  public void testLokiConfigWithBasicAuth() {
    putSecret(CR_NAMESPACE, "loki-creds", "password", "loki-password-value");

    TelemetryProvider cr = baseCr("loki", TelemetryProviderSpec.Provider.LOKI);
    Loki loki = new Loki();
    loki.setEndpoint("http://loki.example.com:3100");
    loki.setAuthType(Loki.AuthType.BASICAUTH);
    loki.setOrganizationID("tenant-1");
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.loki.BasicAuth basicAuth =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.loki.BasicAuth();
    basicAuth.setUsername("loki-user");
    basicAuth.setPasswordSecret(lokiPasswordRef("loki-creds", null, "password"));
    loki.setBasicAuth(basicAuth);
    cr.getSpec().setLoki(loki);

    TelemetryProviderConfig config = convert(cr);

    LokiConfig lokiConfig = assertConfigType(config, LokiConfig.class);
    assertEquals(ProviderType.LOKI, lokiConfig.getType());
    assertEquals("http://loki.example.com:3100", lokiConfig.getEndpoint());
    assertEquals(AuthCredentials.AuthType.BasicAuth, lokiConfig.getAuthType());
    assertEquals("tenant-1", lokiConfig.getOrganizationID());
    assertNotNull(lokiConfig.getBasicAuth());
    assertEquals("loki-user", lokiConfig.getBasicAuth().getUsername());
    assertEquals("loki-password-value", lokiConfig.getBasicAuth().getPassword());
  }

  @Test
  public void testLokiConfigWithNoAuthReadsNoSecret() {
    TelemetryProvider cr = baseCr("loki-noauth", TelemetryProviderSpec.Provider.LOKI);
    Loki loki = new Loki();
    loki.setEndpoint("http://loki.example.com:3100");
    loki.setAuthType(Loki.AuthType.NOAUTH);
    cr.getSpec().setLoki(loki);

    TelemetryProviderConfig config = convert(cr);

    LokiConfig lokiConfig = assertConfigType(config, LokiConfig.class);
    assertEquals(AuthCredentials.AuthType.NoAuth, lokiConfig.getAuthType());
    assertNull(lokiConfig.getBasicAuth());
    verifyNoSecretRead();
  }

  @Test
  public void testDynatraceConfig() {
    putSecret(CR_NAMESPACE, "dynatrace-creds", "api-token", "dt0c01.ABC123.XYZ789");

    TelemetryProvider cr = baseCr("dynatrace", TelemetryProviderSpec.Provider.DYNATRACE);
    Dynatrace dynatrace = new Dynatrace();
    dynatrace.setEndpoint("https://abc12345.live.dynatrace.com");
    dynatrace.setApiTokenSecret(dynatraceApiTokenRef("dynatrace-creds", null, "api-token"));
    cr.getSpec().setDynatrace(dynatrace);

    TelemetryProviderConfig config = convert(cr);

    DynatraceConfig dynatraceConfig = assertConfigType(config, DynatraceConfig.class);
    assertEquals(ProviderType.DYNATRACE, dynatraceConfig.getType());
    assertEquals("https://abc12345.live.dynatrace.com", dynatraceConfig.getEndpoint());
    assertEquals("dt0c01.ABC123.XYZ789", dynatraceConfig.getApiToken());
  }

  @Test
  public void testS3Config() {
    putSecret(CR_NAMESPACE, "s3-creds", "access-key-id", "AKIAS3EXAMPLE");
    putSecret(CR_NAMESPACE, "s3-creds", "secret-access-key", "s3-secret-value");

    TelemetryProvider cr = baseCr("s3", TelemetryProviderSpec.Provider.S3);
    S3 s3 = new S3();
    s3.setBucket("yb-telemetry-bucket");
    s3.setRegion("eu-central-1");
    s3.setRoleArn("arn:aws:iam::123456789012:role/yb-s3");
    s3.setEndpoint("https://s3.custom.example.com");
    s3.setDirectoryPrefix("custom-logs/");
    s3.setFilePrefix("custom-otel-");
    s3.setPartition(S3.Partition.HOUR);
    s3.setMarshaler(S3.Marshaler.SUMO_IC);
    s3.setDisableSSL(true);
    s3.setForcePathStyle(true);
    s3.setIncludeUniverseAndNodeInPrefix(true);
    s3.setAccessKeyIdSecret(s3AccessKeyIdRef("s3-creds", null, "access-key-id"));
    s3.setSecretAccessKeySecret(s3SecretAccessKeyRef("s3-creds", null, "secret-access-key"));
    cr.getSpec().setS3(s3);

    TelemetryProviderConfig config = convert(cr);

    S3Config s3Config = assertConfigType(config, S3Config.class);
    assertEquals(ProviderType.S3, s3Config.getType());
    assertEquals("yb-telemetry-bucket", s3Config.getBucket());
    assertEquals("eu-central-1", s3Config.getRegion());
    assertEquals("arn:aws:iam::123456789012:role/yb-s3", s3Config.getRoleArn());
    assertEquals("https://s3.custom.example.com", s3Config.getEndpoint());
    assertEquals("custom-logs/", s3Config.getDirectoryPrefix());
    assertEquals("custom-otel-", s3Config.getFilePrefix());
    assertEquals(S3Config.S3Partition.hour, s3Config.getPartition());
    assertEquals(OTLPMarshaler.SUMO_IC, s3Config.getMarshaler());
    assertEquals(Boolean.TRUE, s3Config.getDisableSSL());
    assertEquals(Boolean.TRUE, s3Config.getForcePathStyle());
    assertEquals(Boolean.TRUE, s3Config.getIncludeUniverseAndNodeInPrefix());
    assertEquals("AKIAS3EXAMPLE", s3Config.getAccessKey());
    assertEquals("s3-secret-value", s3Config.getSecretKey());
  }

  @Test
  public void testOtlpConfigWithBasicAuth() {
    putSecret(CR_NAMESPACE, "otlp-creds", "password", "otlp-password-value");

    TelemetryProvider cr = baseCr("otlp", TelemetryProviderSpec.Provider.OTLP);
    Otlp otlp = new Otlp();
    otlp.setEndpoint("https://otel.example.com:4318");
    otlp.setAuthType(Otlp.AuthType.BASICAUTH);
    otlp.setProtocol(Otlp.Protocol.HTTP);
    otlp.setCompression(Otlp.Compression.ZSTD);
    otlp.setTimeoutSeconds(30);
    otlp.setLogsEndpoint("https://otel.example.com:4318/v1/logs");
    otlp.setMetricsEndpoint("https://otel.example.com:4318/v1/metrics");
    otlp.setHeaders(Map.of("x-tenant", "yb"));
    RetryOnFailure retryOnFailure = new RetryOnFailure();
    retryOnFailure.setEnabled(true);
    retryOnFailure.setInitialInterval("30s");
    retryOnFailure.setMaxInterval("10m");
    retryOnFailure.setMaxElapsedTime("60m");
    otlp.setRetryOnFailure(retryOnFailure);
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.BasicAuth basicAuth =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.BasicAuth();
    basicAuth.setUsername("otlp-user");
    basicAuth.setPasswordSecret(otlpPasswordRef("otlp-creds", null, "password"));
    otlp.setBasicAuth(basicAuth);
    cr.getSpec().setOtlp(otlp);

    TelemetryProviderConfig config = convert(cr);

    OTLPConfig otlpConfig = assertConfigType(config, OTLPConfig.class);
    assertEquals(ProviderType.OTLP, otlpConfig.getType());
    assertEquals("https://otel.example.com:4318", otlpConfig.getEndpoint());
    assertEquals(AuthCredentials.AuthType.BasicAuth, otlpConfig.getAuthType());
    assertEquals(OTLPConfig.Protocol.HTTP, otlpConfig.getProtocol());
    assertEquals(CompressionType.zstd, otlpConfig.getCompression());
    assertEquals(Integer.valueOf(30), otlpConfig.getTimeoutSeconds());
    assertEquals("https://otel.example.com:4318/v1/logs", otlpConfig.getLogsEndpoint());
    assertEquals("https://otel.example.com:4318/v1/metrics", otlpConfig.getMetricsEndpoint());
    assertEquals(Map.of("x-tenant", "yb"), otlpConfig.getHeaders());
    ExporterRetryConfig retryConfig = otlpConfig.getRetryOnFailure();
    assertNotNull(retryConfig);
    assertEquals(Boolean.TRUE, retryConfig.getEnabled());
    assertEquals("30s", retryConfig.getInitialInterval());
    assertEquals("10m", retryConfig.getMaxInterval());
    assertEquals("60m", retryConfig.getMaxElapsedTime());
    assertNotNull(otlpConfig.getBasicAuth());
    assertEquals("otlp-user", otlpConfig.getBasicAuth().getUsername());
    assertEquals("otlp-password-value", otlpConfig.getBasicAuth().getPassword());
    assertNull(otlpConfig.getBearerToken());
  }

  @Test
  public void testOtlpConfigWithBearerToken() {
    putSecret(CR_NAMESPACE, "otlp-bearer", "token", "otlp-bearer-token-value");

    TelemetryProvider cr = baseCr("otlp-bearer", TelemetryProviderSpec.Provider.OTLP);
    Otlp otlp = new Otlp();
    otlp.setEndpoint("https://otel.example.com:4317");
    otlp.setAuthType(Otlp.AuthType.BEARERTOKEN);
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.BearerToken bearerToken =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.BearerToken();
    bearerToken.setTokenSecret(otlpBearerTokenRef("otlp-bearer", null, "token"));
    otlp.setBearerToken(bearerToken);
    cr.getSpec().setOtlp(otlp);

    TelemetryProviderConfig config = convert(cr);

    OTLPConfig otlpConfig = assertConfigType(config, OTLPConfig.class);
    assertEquals(AuthCredentials.AuthType.BearerToken, otlpConfig.getAuthType());
    assertNotNull(otlpConfig.getBearerToken());
    assertEquals("otlp-bearer-token-value", otlpConfig.getBearerToken().getToken());
    assertNull(otlpConfig.getBasicAuth());
  }

  /**
   * awsCloudWatch spells its role field {@code roleARN} while s3 spells it {@code roleArn},
   * matching the two Java config classes. Both have to survive the generic Jackson round trip.
   */
  @Test
  public void testRoleArnSpellingDiffersBetweenAwsCloudWatchAndS3() {
    putSecret(CR_NAMESPACE, "aws-creds", "access-key-id", "AKIAEXAMPLE");
    putSecret(CR_NAMESPACE, "aws-creds", "secret-access-key", "aws-secret-value");

    TelemetryProvider cwCr = baseCr("cw", TelemetryProviderSpec.Provider.AWS_CLOUDWATCH);
    AwsCloudWatch awsCloudWatch = new AwsCloudWatch();
    awsCloudWatch.setLogGroup("yb-log-group");
    awsCloudWatch.setLogStream("yb-log-stream");
    awsCloudWatch.setRegion("us-west-2");
    awsCloudWatch.setRoleARN("arn:aws:iam::123456789012:role/cloudwatch-role");
    awsCloudWatch.setAccessKeyIdSecret(awsAccessKeyIdRef("aws-creds", null, "access-key-id"));
    awsCloudWatch.setSecretAccessKeySecret(
        awsSecretAccessKeyRef("aws-creds", null, "secret-access-key"));
    cwCr.getSpec().setAwsCloudWatch(awsCloudWatch);

    AWSCloudWatchConfig cwConfig = assertConfigType(convert(cwCr), AWSCloudWatchConfig.class);
    assertEquals("arn:aws:iam::123456789012:role/cloudwatch-role", cwConfig.getRoleARN());

    TelemetryProvider s3Cr = baseCr("s3", TelemetryProviderSpec.Provider.S3);
    S3 s3 = new S3();
    s3.setBucket("yb-telemetry-bucket");
    s3.setRegion("us-west-2");
    s3.setRoleArn("arn:aws:iam::123456789012:role/s3-role");
    s3.setAccessKeyIdSecret(s3AccessKeyIdRef("aws-creds", null, "access-key-id"));
    s3.setSecretAccessKeySecret(s3SecretAccessKeyRef("aws-creds", null, "secret-access-key"));
    s3Cr.getSpec().setS3(s3);

    S3Config s3Config = assertConfigType(convert(s3Cr), S3Config.class);
    assertEquals("arn:aws:iam::123456789012:role/s3-role", s3Config.getRoleArn());
  }

  // ==================== CRD defaults ====================

  @Test
  public void testOtlpDefaultsSurviveConversion() {
    TelemetryProvider cr = baseCr("otlp-defaults", TelemetryProviderSpec.Provider.OTLP);
    Otlp otlp = new Otlp();
    otlp.setEndpoint("https://otel.example.com:4317");
    otlp.setAuthType(Otlp.AuthType.NOAUTH);
    cr.getSpec().setOtlp(otlp);

    OTLPConfig otlpConfig = assertConfigType(convert(cr), OTLPConfig.class);

    assertEquals(Integer.valueOf(5), otlpConfig.getTimeoutSeconds());
    assertEquals(CompressionType.gzip, otlpConfig.getCompression());
    assertEquals(OTLPConfig.Protocol.gRPC, otlpConfig.getProtocol());
    assertNull(otlpConfig.getBasicAuth());
    assertNull(otlpConfig.getBearerToken());
    assertNull(otlpConfig.getRetryOnFailure());
    verifyNoSecretRead();
  }

  @Test
  public void testS3DefaultsSurviveConversion() {
    putSecret(CR_NAMESPACE, "s3-creds", "access-key-id", "AKIAS3EXAMPLE");
    putSecret(CR_NAMESPACE, "s3-creds", "secret-access-key", "s3-secret-value");

    TelemetryProvider cr = baseCr("s3-defaults", TelemetryProviderSpec.Provider.S3);
    S3 s3 = new S3();
    s3.setBucket("yb-telemetry-bucket");
    s3.setRegion("us-west-2");
    s3.setAccessKeyIdSecret(s3AccessKeyIdRef("s3-creds", null, "access-key-id"));
    s3.setSecretAccessKeySecret(s3SecretAccessKeyRef("s3-creds", null, "secret-access-key"));
    cr.getSpec().setS3(s3);

    S3Config s3Config = assertConfigType(convert(cr), S3Config.class);

    assertEquals("yb-logs/", s3Config.getDirectoryPrefix());
    assertEquals("yb-otel-", s3Config.getFilePrefix());
    assertEquals(S3Config.S3Partition.minute, s3Config.getPartition());
    assertEquals(OTLPMarshaler.OTLP_JSON, s3Config.getMarshaler());
    assertEquals(Boolean.FALSE, s3Config.getDisableSSL());
    assertEquals(Boolean.FALSE, s3Config.getForcePathStyle());
    assertEquals(Boolean.FALSE, s3Config.getIncludeUniverseAndNodeInPrefix());
    assertNull(s3Config.getRoleArn());
    assertNull(s3Config.getEndpoint());
  }

  // ==================== Secret namespace resolution ====================

  @Test
  public void testSecretRefWithoutNamespaceResolvesAgainstCrNamespace() {
    putSecret(CR_NAMESPACE, "datadog-creds", "api-key", "dd-api-key-value");

    TelemetryProvider cr = baseCr("dd", TelemetryProviderSpec.Provider.DATA_DOG);
    DataDog dataDog = new DataDog();
    dataDog.setSite("datadoghq.com");
    dataDog.setApiKeySecret(dataDogApiKeyRef("datadog-creds", null, "api-key"));
    cr.getSpec().setDataDog(dataDog);
    KubernetesResourceDetails owner = KubernetesResourceDetails.fromResource(cr);

    TelemetryProviderConfig config =
        converter.toConfig(cr, mockResourceTracker, owner, localInstanceUuid);

    assertEquals("dd-api-key-value", assertConfigType(config, DataDogConfig.class).getApiKey());

    ArgumentCaptor<String> nameCaptor = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> namespaceCaptor = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> keyCaptor = ArgumentCaptor.forClass(String.class);
    verify(mockOperatorUtils)
        .getAndParseSecretForKey(
            nameCaptor.capture(),
            namespaceCaptor.capture(),
            keyCaptor.capture(),
            eq(mockResourceTracker),
            eq(owner),
            eq(localInstanceUuid));
    assertEquals("datadog-creds", nameCaptor.getValue());
    assertEquals(CR_NAMESPACE, namespaceCaptor.getValue());
    assertEquals("api-key", keyCaptor.getValue());
  }

  @Test
  public void testSecretRefWithExplicitNamespaceUsesThatNamespace() {
    // Deliberately also present in the CR namespace with a different value, so that a converter
    // that ignored the explicit namespace would still resolve, but to the wrong value.
    putSecret(CR_NAMESPACE, "datadog-creds", "api-key", "wrong-namespace-value");
    putSecret(OTHER_NAMESPACE, "datadog-creds", "api-key", "explicit-namespace-value");

    TelemetryProvider cr = baseCr("dd", TelemetryProviderSpec.Provider.DATA_DOG);
    DataDog dataDog = new DataDog();
    dataDog.setSite("datadoghq.com");
    dataDog.setApiKeySecret(dataDogApiKeyRef("datadog-creds", OTHER_NAMESPACE, "api-key"));
    cr.getSpec().setDataDog(dataDog);

    TelemetryProviderConfig config = convert(cr);

    assertEquals(
        "explicit-namespace-value", assertConfigType(config, DataDogConfig.class).getApiKey());

    ArgumentCaptor<String> namespaceCaptor = ArgumentCaptor.forClass(String.class);
    verify(mockOperatorUtils)
        .getAndParseSecretForKey(
            eq("datadog-creds"), namespaceCaptor.capture(), eq("api-key"), any(), any(), any());
    assertEquals(OTHER_NAMESPACE, namespaceCaptor.getValue());
  }

  // ==================== Error paths ====================

  @Test
  public void testMissingSecretResolvesToNullCredential() {
    // Nothing registered: the Secret does not exist. The YBA config validation the reconciler runs
    // before saving is what rejects the resulting config.
    TelemetryProvider cr = baseCr("dd", TelemetryProviderSpec.Provider.DATA_DOG);
    DataDog dataDog = new DataDog();
    dataDog.setSite("datadoghq.com");
    dataDog.setApiKeySecret(dataDogApiKeyRef("no-such-secret", null, "api-key"));
    cr.getSpec().setDataDog(dataDog);

    assertNull(assertConfigType(convert(cr), DataDogConfig.class).getApiKey());
  }

  @Test
  public void testSecretWithoutRequestedKeyResolvesToNullCredential() {
    // The Secret exists, but only holds a different key.
    putSecret(CR_NAMESPACE, "splunk-creds", "some-other-key", "unrelated-value");

    TelemetryProvider cr = baseCr("splunk", TelemetryProviderSpec.Provider.SPLUNK);
    Splunk splunk = new Splunk();
    splunk.setEndpoint("https://splunk.example.com:8088/services/collector");
    splunk.setTokenSecret(splunkTokenRef("splunk-creds", null, "hec-token"));
    cr.getSpec().setSplunk(splunk);

    assertNull(assertConfigType(convert(cr), SplunkConfig.class).getToken());
    // The lookup used the requested key, not the one the Secret happens to hold.
    verify(mockOperatorUtils)
        .getAndParseSecretForKey(
            eq("splunk-creds"), eq(CR_NAMESPACE), eq("hec-token"), any(), any(), any());
  }

  @Test
  public void testMissingCredentialRefFails() {
    TelemetryProvider cr = baseCr("cw", TelemetryProviderSpec.Provider.AWS_CLOUDWATCH);
    AwsCloudWatch awsCloudWatch = new AwsCloudWatch();
    awsCloudWatch.setLogGroup("yb-log-group");
    awsCloudWatch.setLogStream("yb-log-stream");
    awsCloudWatch.setRegion("us-west-2");
    // accessKeyIdSecret is set, secretAccessKeySecret is left out entirely.
    putSecret(CR_NAMESPACE, "aws-creds", "access-key-id", "AKIAEXAMPLE");
    awsCloudWatch.setAccessKeyIdSecret(awsAccessKeyIdRef("aws-creds", null, "access-key-id"));
    cr.getSpec().setAwsCloudWatch(awsCloudWatch);

    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class, () -> convert(cr));

    String message = exception.getMessage();
    assertTrue(message, message.contains("missing required field"));
    assertTrue(message, message.contains("awsCloudWatch.secretAccessKeySecret"));
  }

  @Test
  public void testMissingProviderSectionFails() {
    TelemetryProvider cr = baseCr("loki", TelemetryProviderSpec.Provider.LOKI);
    // spec.loki is left unset.

    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class, () -> convert(cr));

    String message = exception.getMessage();
    assertTrue(message, message.contains("'loki' section"));
    assertTrue(message, message.contains("LOKI"));
    verifyNoSecretRead();
  }

  @Test
  public void testMissingSpecFails() {
    TelemetryProvider cr = baseCr("nope", TelemetryProviderSpec.Provider.DATA_DOG);
    cr.setSpec(null);

    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class, () -> convert(cr));

    assertTrue(exception.getMessage(), exception.getMessage().contains("has no spec"));
    verifyNoSecretRead();
  }

  // ==================== Helpers ====================

  private TelemetryProviderConfig convert(TelemetryProvider cr) {
    return converter.toConfig(
        cr,
        mockResourceTracker,
        cr.getMetadata() == null ? null : KubernetesResourceDetails.fromResource(cr),
        localInstanceUuid);
  }

  private void verifyNoSecretRead() {
    verify(mockOperatorUtils, never())
        .getAndParseSecretForKey(
            anyString(), nullable(String.class), anyString(), any(), any(), any());
  }

  private static <T extends TelemetryProviderConfig> T assertConfigType(
      TelemetryProviderConfig config, Class<T> expected) {
    assertNotNull(config);
    assertTrue(
        "expected " + expected.getSimpleName() + " but got " + config.getClass().getSimpleName(),
        expected.isInstance(config));
    return expected.cast(config);
  }

  private TelemetryProvider baseCr(String name, TelemetryProviderSpec.Provider provider) {
    TelemetryProvider cr = new TelemetryProvider();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(CR_NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    metadata.setGeneration(1L);
    cr.setMetadata(metadata);
    TelemetryProviderSpec spec = new TelemetryProviderSpec();
    spec.setProvider(provider);
    cr.setSpec(spec);
    return cr;
  }

  private void putSecret(String namespace, String name, String key, String value) {
    cannedSecrets.put(secretMapKey(namespace, name, key), value);
  }

  private static String secretMapKey(String namespace, String name, String key) {
    return namespace + "/" + name + "/" + key;
  }

  // Secret reference factories. Every provider gets its own generated reference class, all with the
  // same name/namespace/key shape, so they are built here to keep the fully qualified names in one
  // place.

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.datadog.ApiKeySecret
      dataDogApiKeyRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.datadog.ApiKeySecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.datadog.ApiKeySecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.splunk.TokenSecret
      splunkTokenRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.splunk.TokenSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.splunk.TokenSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.awscloudwatch.AccessKeyIdSecret
      awsAccessKeyIdRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.awscloudwatch.AccessKeyIdSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.awscloudwatch.AccessKeyIdSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.awscloudwatch
          .SecretAccessKeySecret
      awsSecretAccessKeyRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.awscloudwatch.SecretAccessKeySecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.awscloudwatch
            .SecretAccessKeySecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.gcpcloudmonitoring
          .CredentialsSecret
      gcpCredentialsRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.gcpcloudmonitoring.CredentialsSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.gcpcloudmonitoring
            .CredentialsSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.loki.basicauth.PasswordSecret
      lokiPasswordRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.loki.basicauth.PasswordSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.loki.basicauth.PasswordSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.dynatrace.ApiTokenSecret
      dynatraceApiTokenRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.dynatrace.ApiTokenSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.dynatrace.ApiTokenSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.s3.AccessKeyIdSecret
      s3AccessKeyIdRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.s3.AccessKeyIdSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.s3.AccessKeyIdSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.s3.SecretAccessKeySecret
      s3SecretAccessKeyRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.s3.SecretAccessKeySecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.s3.SecretAccessKeySecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.basicauth.PasswordSecret
      otlpPasswordRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.basicauth.PasswordSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.basicauth.PasswordSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }

  private static io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.bearertoken.TokenSecret
      otlpBearerTokenRef(String name, String namespace, String key) {
    io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.bearertoken.TokenSecret ref =
        new io.yugabyte.operator.v1alpha1.telemetryproviderspec.otlp.bearertoken.TokenSecret();
    ref.setName(name);
    ref.setNamespace(namespace);
    ref.setKey(key);
    return ref;
  }
}
