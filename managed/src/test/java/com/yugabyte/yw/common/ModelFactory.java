// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelParams;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfigurationTarget;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import play.libs.Json;

public class ModelFactory {

  /*
   * Customer creation helpers.
   */

  public static Customer testCustomer() {
    return testCustomer("test@customer.com");
  }

  public static Customer testCustomer(String name) {
    return testCustomer("tc", name);
  }

  public static Customer testCustomer(String code, String name) {
    return Customer.create(code, name);
  }

  /*
   * Users creation helpers.
   */
  public static Users testUser(Customer customer) {
    return testUser(customer, "test@customer.com");
  }

  public static Users testUser(Customer customer, String email) {
    return testUser(customer, email, Role.Admin);
  }

  public static Users testUser(Customer customer, Role role) {
    return testUser(customer, "test@customer.com", role);
  }

  public static Users testUser(Customer customer, String email, Role role) {
    return Users.create(email, "password", role, customer.uuid, false);
  }

  /*
   * Provider creation helpers.
   */

  public static Provider awsProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.aws, "Amazon");
  }

  public static Provider gcpProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.gcp, "Google");
  }

  public static Provider azuProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.azu, "Azure");
  }

  public static Provider onpremProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.onprem, "OnPrem");
  }

  public static Provider kubernetesProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.kubernetes, "Kubernetes");
  }

  public static Provider newProvider(Customer customer, Common.CloudType cloud) {
    return Provider.create(customer.uuid, cloud, cloud.toString());
  }

  public static Provider newProvider(Customer customer, Common.CloudType cloud, String name) {
    return Provider.create(customer.uuid, cloud, name);
  }

  public static Provider newProvider(
      Customer customer, Common.CloudType cloud, Map<String, String> config) {
    return Provider.create(customer.uuid, cloud, cloud.toString(), config);
  }

  /*
   * Universe creation helpers.
   */

  public static Universe createUniverse() {
    return createUniverse("Test Universe", 1L);
  }

  public static Universe createUniverse(String universeName) {
    return createUniverse(universeName, 1L);
  }

  public static Universe createUniverse(long customerId) {
    return createUniverse("Test Universe", customerId);
  }

  public static Universe createUniverse(long customerId, UUID rootCA) {
    return createUniverse(
        "Test Universe", UUID.randomUUID(), customerId, Common.CloudType.aws, null, rootCA);
  }

  public static Universe createUniverse(String universeName, long customerId) {
    return createUniverse(universeName, UUID.randomUUID(), customerId, Common.CloudType.aws);
  }

  public static Universe createUniverse(String universeName, UUID universeUUID) {
    return createUniverse(universeName, universeUUID, 1L, Common.CloudType.aws);
  }

  public static Universe createUniverse(
      String universeName, long customerId, Common.CloudType cloudType) {
    return createUniverse(universeName, UUID.randomUUID(), customerId, cloudType);
  }

  public static Universe createUniverse(
      String universeName, UUID universeUUID, long customerId, Common.CloudType cloudType) {
    return createUniverse(universeName, universeUUID, customerId, cloudType, null);
  }

  public static Universe createUniverse(
      String universeName,
      UUID universeUUID,
      long customerId,
      Common.CloudType cloudType,
      PlacementInfo pi) {
    return createUniverse(universeName, universeUUID, customerId, cloudType, pi, null);
  }

  public static Universe createUniverse(
      String universeName,
      UUID universeUUID,
      long customerId,
      Common.CloudType cloudType,
      PlacementInfo pi,
      UUID rootCA) {
    Customer c = Customer.get(customerId);
    // Custom setup a default AWS provider, can be overridden later.
    List<Provider> providerList = Provider.get(c.uuid, cloudType);
    Provider p = providerList.isEmpty() ? newProvider(c, cloudType) : providerList.get(0);

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.universeName = universeName;
    userIntent.provider = p.uuid.toString();
    userIntent.providerType = cloudType;
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.universeUUID = universeUUID;
    params.nodeDetailsSet = new HashSet<>();
    params.nodePrefix = universeName;
    params.rootCA = rootCA;
    params.upsertPrimaryCluster(userIntent, pi);
    Universe u = Universe.create(params, customerId);
    c.addUniverseUUID(u.universeUUID);
    c.save();
    return u;
  }

  public static CustomerConfig createS3StorageConfig(Customer customer, String configName) {
    JsonNode formData =
        Json.parse(
            "{\"configName\": \""
                + configName
                + "\", \"name\": \"S3\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\": \"s3://foo\","
                + " \"ACCESS_KEY\": \"A-KEY\", \"ACCESS_SECRET\": \"A-SECRET\"}}");
    return CustomerConfig.createWithFormData(customer.uuid, formData);
  }

  public static CustomerConfig createNfsStorageConfig(Customer customer, String configName) {
    JsonNode formData =
        Json.parse(
            "{\"configName\": \""
                + configName
                + "\", \"name\": \"NFS\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\": \"/foo/bar\"}}");
    return CustomerConfig.createWithFormData(customer.uuid, formData);
  }

  public static CustomerConfig createGcsStorageConfig(Customer customer, String configName) {
    JsonNode formData =
        Json.parse(
            "{\"configName\": \""
                + configName
                + "\", \"name\": \"GCS\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\": \"gs://foo\","
                + " \"GCS_CREDENTIALS_JSON\": \"G-CREDS\"}}");
    return CustomerConfig.createWithFormData(customer.uuid, formData);
  }

  public static Backup createBackup(UUID customerUUID, UUID universeUUID, UUID configUUID) {
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = configUUID;
    params.universeUUID = universeUUID;
    params.setKeyspace("foo");
    params.setTableName("bar");
    params.tableUUID = UUID.randomUUID();
    return Backup.create(customerUUID, params);
  }

  public static Backup createBackupWithExpiry(
      UUID customerUUID, UUID universeUUID, UUID configUUID) {
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = configUUID;
    params.universeUUID = universeUUID;
    params.setKeyspace("foo");
    params.setTableName("bar");
    params.tableUUID = UUID.randomUUID();
    params.timeBeforeDelete = -100L;
    return Backup.create(customerUUID, params);
  }

  public static Schedule createScheduleBackup(
      UUID customerUUID, UUID universeUUID, UUID configUUID) {
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = configUUID;
    params.universeUUID = universeUUID;
    params.setKeyspace("foo");
    params.setTableName("bar");
    params.tableUUID = UUID.randomUUID();
    return Schedule.create(customerUUID, params, TaskType.BackupUniverse, 1000, null);
  }

  public static CustomerConfig setCallhomeLevel(Customer customer, String level) {
    return CustomerConfig.createCallHomeConfig(customer.uuid, level);
  }

  public static CustomerConfig createAlertConfig(
      Customer customer, String alertingEmail, boolean sendAlertsToYb, boolean reportOnlyErrors) {
    AlertingData data = new AlertingData();
    data.sendAlertsToYb = sendAlertsToYb;
    data.alertingEmail = alertingEmail;
    data.reportOnlyErrors = reportOnlyErrors;

    return CustomerConfig.createAlertConfig(customer.uuid, Json.toJson(data));
  }

  public static AlertConfiguration createAlertConfiguration(
      Customer customer, Universe universe, Consumer<AlertConfiguration> modifier) {
    AlertConfiguration configuration =
        new AlertConfiguration()
            .setName("alertConfiguration")
            .setDescription("alertConfiguration description")
            .setCustomerUUID(customer.getUuid())
            .setTargetType(AlertConfiguration.TargetType.UNIVERSE)
            .setCreateTime(nowWithoutMillis())
            .setThresholds(
                ImmutableMap.of(
                    AlertConfiguration.Severity.SEVERE,
                    new AlertConfigurationThreshold()
                        .setCondition(Condition.GREATER_THAN)
                        .setThreshold(1D)))
            .setThresholdUnit(Unit.PERCENT)
            .setDefaultDestination(true)
            .generateUUID();
    if (universe != null) {
      configuration
          .setTarget(
              new AlertConfigurationTarget().setUuids(ImmutableSet.of(universe.getUniverseUUID())))
          .setTemplate(AlertTemplate.MEMORY_CONSUMPTION);
    } else {
      configuration
          .setTarget(new AlertConfigurationTarget().setAll(true))
          .setTemplate(AlertTemplate.BACKUP_FAILURE);
    }
    modifier.accept(configuration);
    configuration.save();
    return configuration;
  }

  public static AlertConfiguration createAlertConfiguration(Customer customer, Universe universe) {
    return createAlertConfiguration(customer, universe, c -> {});
  }

  public static AlertDefinition createAlertDefinition(Customer customer, Universe universe) {
    AlertConfiguration configuration = createAlertConfiguration(customer, universe);
    return createAlertDefinition(customer, universe, configuration);
  }

  public static AlertDefinition createAlertDefinition(
      Customer customer, Universe universe, AlertConfiguration configuration) {
    AlertDefinition alertDefinition =
        new AlertDefinition()
            .setConfigurationUUID(configuration.getUuid())
            .setCustomerUUID(customer.getUuid())
            .setQuery("query {{ query_condition }} {{ query_threshold }}")
            .generateUUID();
    if (universe != null) {
      alertDefinition.setLabels(
          MetricLabelsBuilder.create().appendSource(universe).getDefinitionLabels());
    } else {
      alertDefinition.setLabels(
          MetricLabelsBuilder.create().appendSource(customer).getDefinitionLabels());
    }
    alertDefinition.save();
    return alertDefinition;
  }

  public static Alert createAlert(Customer customer) {
    return createAlert(customer, null, null, null);
  }

  public static Alert createAlert(Customer customer, Universe universe) {
    return createAlert(customer, universe, null);
  }

  public static Alert createAlert(Customer customer, AlertDefinition definition) {
    return createAlert(customer, null, definition);
  }

  public static Alert createAlert(
      Customer customer, AlertDefinition definition, Consumer<Alert> modifier) {
    return createAlert(customer, null, definition, modifier);
  }

  public static Alert createAlert(
      Customer customer, Universe universe, AlertDefinition definition) {
    return createAlert(customer, universe, definition, a -> {});
  }

  public static Alert createAlert(
      Customer customer, Universe universe, AlertDefinition definition, Consumer<Alert> modifier) {
    Alert alert =
        new Alert()
            .setCustomerUUID(customer.getUuid())
            .setName("Alert 1")
            .setSourceName("Source 1")
            .setSeverity(AlertConfiguration.Severity.SEVERE)
            .setMessage("Universe on fire!")
            .generateUUID();
    if (definition == null) {
      AlertConfiguration configuration = createAlertConfiguration(customer, universe);
      definition = createAlertDefinition(customer, universe, configuration);
    }
    AlertConfiguration configuration =
        AlertConfiguration.db().find(AlertConfiguration.class, definition.getConfigurationUUID());
    alert.setConfigurationUuid(definition.getConfigurationUUID());
    alert.setConfigurationType(configuration.getTargetType());
    alert.setDefinitionUuid(definition.getUuid());
    List<AlertLabel> labels =
        definition
            .getEffectiveLabels(configuration, AlertConfiguration.Severity.SEVERE)
            .stream()
            .map(l -> new AlertLabel(l.getName(), l.getValue()))
            .collect(Collectors.toList());
    alert.setLabels(labels);
    if (modifier != null) {
      modifier.accept(alert);
    }
    alert.save();
    return alert;
  }

  public static AlertChannel createAlertChannel(
      UUID customerUUID, String name, AlertChannelParams params) {
    AlertChannel channel =
        new AlertChannel()
            .generateUUID()
            .setCustomerUUID(customerUUID)
            .setName(name)
            .setParams(params);
    channel.save();
    return channel;
  }

  public static AlertChannel createEmailChannel(UUID customerUUID, String name) {
    return createAlertChannel(customerUUID, name, createEmailChannelParams());
  }

  public static AlertChannel createSlackChannel(UUID customerUUID, String name) {
    return createAlertChannel(customerUUID, name, createSlackChannelParams());
  }

  public static AlertChannelEmailParams createEmailChannelParams() {
    AlertChannelEmailParams params = new AlertChannelEmailParams();
    params.recipients = Collections.singletonList("test@test.com");
    params.smtpData = EmailFixtures.createSmtpData();
    return params;
  }

  public static AlertChannelSlackParams createSlackChannelParams() {
    AlertChannelSlackParams params = new AlertChannelSlackParams();
    params.username = "channel";
    params.webhookUrl = "http://www.google.com";
    return params;
  }

  public static AlertDestination createAlertDestination(
      UUID customerUUID, String name, List<AlertChannel> channels) {
    AlertDestination destination =
        new AlertDestination()
            .generateUUID()
            .setCustomerUUID(customerUUID)
            .setName(name)
            .setChannelsList(channels);
    destination.save();
    return destination;
  }

  /*
   * KMS Configuration creation helpers.
   */
  public static KmsConfig createKMSConfig(
      UUID customerUUID, String keyProvider, ObjectNode authConfig) {
    EncryptionAtRestManager keyManager = new EncryptionAtRestManager();
    EncryptionAtRestService keyService = keyManager.getServiceInstance(keyProvider);
    return keyService.createAuthConfig(customerUUID, "Test KMS Configuration", authConfig);
  }

  /*
   * CertificateInfo creation helpers.
   */
  public static CertificateInfo createCertificateInfo(
      UUID customerUUID, String certificate, CertificateInfo.Type certType)
      throws IOException, NoSuchAlgorithmException {
    return CertificateInfo.create(
        UUID.randomUUID(), customerUUID, "test", new Date(), new Date(), "", certificate, certType);
  }
}
