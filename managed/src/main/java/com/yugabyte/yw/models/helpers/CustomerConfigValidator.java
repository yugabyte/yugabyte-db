// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.models.CustomerConfig.ConfigType.PASSWORD_POLICY;
import static com.yugabyte.yw.models.CustomerConfig.ConfigType.STORAGE;
import static play.mvc.Http.Status.CONFLICT;

import com.amazonaws.SDKGlobalConfiguration;
import com.amazonaws.SdkClientException;
import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.api.gax.paging.Page;
import com.google.auth.Credentials;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.StorageException;
import com.google.cloud.storage.StorageOptions;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.forms.PasswordPolicyFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.Schedule;
import java.io.ByteArrayInputStream;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.DomainValidator;
import org.apache.commons.validator.routines.UrlValidator;

@Singleton
public class CustomerConfigValidator {

  private static final String NAME_S3 = "S3";

  private static final String NAME_GCS = "GCS";

  private static final String NAME_NFS = "NFS";

  private static final String NAME_AZURE = "AZ";

  private static final String[] S3_URL_SCHEMES = {"http", "https", "s3"};

  private static final String[] GCS_URL_SCHEMES = {"http", "https", "gs"};

  private static final String[] AZ_URL_SCHEMES = {"http", "https"};

  private static final String[] TLD_OVERRIDE = {"local"};

  private static final String AWS_HOST_BASE_FIELDNAME = "AWS_HOST_BASE";

  public static final String BACKUP_LOCATION_FIELDNAME = "BACKUP_LOCATION";

  public static final String AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";

  public static final String AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";

  public static final String GCS_CREDENTIALS_JSON_FIELDNAME = "GCS_CREDENTIALS_JSON";

  private static final String NFS_PATH_REGEXP = "^/|//|(/[\\w-]+)+$";

  public static final Integer MIN_PORT_VALUE = 0;

  public static final Integer MAX_PORT_VALUE = 65535;

  private final BeanValidator beanValidator;

  public abstract static class ConfigValidator {

    protected final String type;

    protected final String name;

    public ConfigValidator(String type, String name) {
      this.type = type;
      this.name = name;
    }

    public void validate(String type, String name, JsonNode data) {
      if (this.type.equals(type) && this.name.equals(name)) {
        doValidate(data);
      }
    }

    protected String fieldFullName(String fieldName) {
      if (StringUtils.isEmpty(fieldName)) {
        return "data";
      }
      return "data." + fieldName;
    }

    protected abstract void doValidate(JsonNode data);
  }

  public abstract static class ConfigFieldValidator extends ConfigValidator {

    protected final String fieldName;

    static {
      DomainValidator.updateTLDOverride(DomainValidator.ArrayType.LOCAL_PLUS, TLD_OVERRIDE);
    }

    public ConfigFieldValidator(String type, String name, String fieldName) {
      super(type, name);
      this.fieldName = fieldName;
    }

    @Override
    public void doValidate(JsonNode data) {
      JsonNode value = data.get(fieldName);
      doValidate(value == null ? "" : value.asText());
    }

    protected abstract void doValidate(String value);
  }

  public class ConfigS3PreflightCheckValidator extends ConfigValidator {

    protected final String fieldName;

    public ConfigS3PreflightCheckValidator(String type, String name, String fieldName) {
      super(type, name);
      this.fieldName = fieldName;
    }

    @Override
    public void doValidate(JsonNode data) {
      if (this.name.equals("S3") && data.get(AWS_ACCESS_KEY_ID_FIELDNAME) != null) {
        String s3UriPath = data.get(BACKUP_LOCATION_FIELDNAME).asText();
        String s3Uri = s3UriPath;
        // Assuming bucket name will always start with s3:// otherwise that will be invalid
        if (s3UriPath.length() < 5 || !s3UriPath.startsWith("s3://")) {
          beanValidator
              .error()
              .forField(fieldFullName(fieldName), "Invalid s3UriPath format: " + s3UriPath)
              .throwError();
        } else {
          try {
            // Disable cert checking while connecting with s3
            // Enabling it can potentially fail when s3 compatible storages like
            // Dell ECS are provided and custom certs are needed to connect
            // Reference: https://yugabyte.atlassian.net/browse/PLAT-2497
            System.setProperty(
                SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");
            s3UriPath = s3UriPath.substring(5);
            String[] bucketSplit = s3UriPath.split("/", 2);
            String bucketName = bucketSplit.length > 0 ? bucketSplit[0] : "";
            String prefix = bucketSplit.length > 1 ? bucketSplit[1] : "";
            AmazonS3Client s3Client =
                create(
                    data.get(AWS_ACCESS_KEY_ID_FIELDNAME).asText(),
                    data.get(AWS_SECRET_ACCESS_KEY_FIELDNAME).asText());
            if (data.get(AWS_HOST_BASE_FIELDNAME) != null
                && !StringUtils.isBlank(data.get(AWS_HOST_BASE_FIELDNAME).textValue())) {
              s3Client.setEndpoint(data.get(AWS_HOST_BASE_FIELDNAME).textValue());
            }
            // Only the bucket has been given, with no subdir.
            if (bucketSplit.length == 1) {
              if (!s3Client.doesBucketExistV2(bucketName)) {
                beanValidator
                    .error()
                    .forField(fieldFullName(fieldName), "S3 URI path " + s3Uri + " doesn't exist")
                    .throwError();
              }
            } else {
              ListObjectsV2Result result = s3Client.listObjectsV2(bucketName, prefix);
              if (result.getKeyCount() == 0) {
                beanValidator
                    .error()
                    .forField(fieldFullName(fieldName), "S3 URI path " + s3Uri + " doesn't exist")
                    .throwError();
              }
            }
          } catch (AmazonS3Exception s3Exception) {
            String errMessage = s3Exception.getErrorMessage();
            if (errMessage.contains("Denied") || errMessage.contains("bucket"))
              errMessage += " " + s3Uri;
            beanValidator.error().forField(fieldFullName(fieldName), errMessage).throwError();
          } catch (SdkClientException e) {
            beanValidator.error().forField(fieldFullName(fieldName), e.getMessage()).throwError();
          } finally {
            // Re-enable cert checking as it applies globally
            System.setProperty(
                SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
          }
        }
      }
    }
  }

  public class ConfigObjectValidator<T> extends ConfigValidator {
    private Class<T> configClass;

    public ConfigObjectValidator(String type, String name, Class<T> configClass) {
      super(type, name);
      this.configClass = configClass;
    }

    @Override
    protected void doValidate(JsonNode data) {
      ObjectMapper mapper = new ObjectMapper();
      try {
        T config = mapper.treeToValue(data, configClass);
        beanValidator.validate(config, "data");
      } catch (JsonProcessingException e) {
        beanValidator
            .error()
            .forField("data", "Invalid json for type '" + configClass.getSimpleName() + "'.")
            .throwError();
      }
    }
  }

  public class ConfigValidatorRegEx extends ConfigFieldValidator {

    private Pattern pattern;

    public ConfigValidatorRegEx(String type, String name, String fieldName, String regex) {
      super(type, name, fieldName);
      pattern = Pattern.compile(regex);
    }

    @Override
    protected void doValidate(String value) {
      if (!pattern.matcher(value).matches()) {
        beanValidator
            .error()
            .forField(fieldFullName(fieldName), "Invalid field value '" + value + "'.")
            .throwError();
      }
    }
  }

  public class ConfigValidatorUrl extends ConfigFieldValidator {

    private static final String DEFAULT_SCHEME = "https://";

    private final UrlValidator urlValidator;

    private final boolean emptyAllowed;

    public ConfigValidatorUrl(
        String type, String name, String fieldName, String[] schemes, boolean emptyAllowed) {
      super(type, name, fieldName);
      this.emptyAllowed = emptyAllowed;
      DomainValidator domainValidator = DomainValidator.getInstance(true);
      urlValidator =
          new UrlValidator(schemes, null, UrlValidator.ALLOW_LOCAL_URLS, domainValidator);
    }

    @Override
    protected void doValidate(String value) {
      if (StringUtils.isEmpty(value)) {
        if (!emptyAllowed) {
          beanValidator
              .error()
              .forField(fieldFullName(fieldName), "This field is required.")
              .throwError();
        }
        return;
      }

      boolean valid = false;
      try {
        URI uri = new URI(value);
        if (fieldName.equals(AWS_HOST_BASE_FIELDNAME)) {
          if (StringUtils.isEmpty(uri.getHost())) {
            uri = new URI(DEFAULT_SCHEME + value);
          }
          String host = uri.getHost();
          String scheme = uri.getScheme() + "://";
          String uriToValidate = scheme + host;
          Integer port = new Integer(uri.getPort());
          boolean validPort = true;
          if (!uri.toString().equals(uriToValidate)
              && (port < MIN_PORT_VALUE || port > MAX_PORT_VALUE)) {
            validPort = false;
          }
          valid = validPort && urlValidator.isValid(uriToValidate);
        } else {
          valid =
              urlValidator.isValid(
                  StringUtils.isEmpty(uri.getScheme()) ? DEFAULT_SCHEME + value : value);
        }
      } catch (URISyntaxException e) {
      }

      if (!valid) {
        beanValidator
            .error()
            .forField(fieldFullName(fieldName), "Invalid field value '" + value + "'.")
            .throwError();
      }
    }
  }

  public class ConfigGCSPreflightCheckValidator extends ConfigValidator {

    protected final String fieldName;

    public ConfigGCSPreflightCheckValidator(String type, String name, String fieldName) {
      super(type, name);
      this.fieldName = fieldName;
    }

    @Override
    public void doValidate(JsonNode data) {
      if (this.name.equals(NAME_GCS) && data.get(GCS_CREDENTIALS_JSON_FIELDNAME) != null) {
        String gsUriPath = data.get(BACKUP_LOCATION_FIELDNAME).asText();
        String gsUri = gsUriPath;
        // Assuming bucket name will always start with gs:// otherwise that will be invalid
        if (gsUriPath.length() < 5 || !gsUriPath.startsWith("gs://")) {
          beanValidator
              .error()
              .forField(fieldFullName(fieldName), "Invalid gsUriPath format: " + gsUriPath)
              .throwError();
        } else {
          gsUriPath = gsUriPath.substring(5);
          String[] bucketSplit = gsUriPath.split("/", 2);
          String bucketName = bucketSplit.length > 0 ? bucketSplit[0] : "";
          String prefix = bucketSplit.length > 1 ? bucketSplit[1] : "";
          String gcpCredentials = data.get(GCS_CREDENTIALS_JSON_FIELDNAME).asText();
          try {
            Credentials credentials =
                GoogleCredentials.fromStream(
                    new ByteArrayInputStream(gcpCredentials.getBytes("UTF-8")));
            Storage storage =
                StorageOptions.newBuilder().setCredentials(credentials).build().getService();
            // Only the bucket has been given, with no subdir.
            if (bucketSplit.length == 1) {
              // Check if the bucket exists by calling a list.
              // If the bucket exists, the call will return nothing,
              // If the creds are incorrect, it will throw an exception
              // saying no access.
              storage.list(bucketName);
            } else {
              Page<Blob> blobs =
                  storage.list(
                      bucketName,
                      Storage.BlobListOption.prefix(prefix),
                      Storage.BlobListOption.currentDirectory());
              if (!blobs.getValues().iterator().hasNext()) {
                beanValidator
                    .error()
                    .forField(fieldFullName(fieldName), "GS Uri path " + gsUri + " doesn't exist")
                    .throwError();
              }
            }
          } catch (StorageException exp) {
            beanValidator.error().forField(fieldFullName(fieldName), exp.getMessage()).throwError();
          } catch (Exception e) {
            beanValidator
                .error()
                .forField(fieldFullName(fieldName), "Invalid GCP Credential Json.")
                .throwError();
          }
        }
      }
    }
  }

  private final List<ConfigValidator> validators = new ArrayList<>();

  @Inject
  public CustomerConfigValidator(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
    validators.add(
        new ConfigValidatorRegEx(
            STORAGE.name(), NAME_NFS, BACKUP_LOCATION_FIELDNAME, NFS_PATH_REGEXP));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_S3, BACKUP_LOCATION_FIELDNAME, S3_URL_SCHEMES, false));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_S3, AWS_HOST_BASE_FIELDNAME, S3_URL_SCHEMES, true));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_GCS, BACKUP_LOCATION_FIELDNAME, GCS_URL_SCHEMES, false));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_AZURE, BACKUP_LOCATION_FIELDNAME, AZ_URL_SCHEMES, false));
    validators.add(
        new ConfigObjectValidator<>(
            PASSWORD_POLICY.name(), CustomerConfig.PASSWORD_POLICY, PasswordPolicyFormData.class));
    validators.add(
        new ConfigS3PreflightCheckValidator(STORAGE.name(), NAME_S3, BACKUP_LOCATION_FIELDNAME));
    validators.add(
        new ConfigGCSPreflightCheckValidator(STORAGE.name(), NAME_GCS, BACKUP_LOCATION_FIELDNAME));
  }

  /**
   * Validates data which is contained in formData. During the procedure it calls all the registered
   * validators. Errors are collected and returned back as a result. Empty result object means no
   * errors.
   *
   * <p>Currently are checked: - NFS - NFS Storage Path (against regexp NFS_PATH_REGEXP); - S3/AWS -
   * S3 Bucket, S3 Bucket Host Base (both as URLs); - GCS - GCS Bucket (as URL); - AZURE - Container
   * URL (as URL).
   *
   * <p>The URLs validation allows empty scheme. In such case the check is made with DEFAULT_SCHEME
   * added before the URL.
   *
   * @param customerConfig
   */
  public void validateConfig(CustomerConfig customerConfig) {
    beanValidator.validate(customerConfig);

    String configName = customerConfig.getConfigName();
    CustomerConfig existentConfig = CustomerConfig.get(customerConfig.customerUUID, configName);
    if (existentConfig != null) {
      if (!existentConfig.getConfigUUID().equals(customerConfig.getConfigUUID())) {
        beanValidator
            .error()
            .code(CONFLICT)
            .forField("configName", String.format("Configuration %s already exists", configName))
            .throwError();
      }

      JsonNode newBackupLocation = customerConfig.getData().get("BACKUP_LOCATION");
      JsonNode oldBackupLocation = existentConfig.getData().get("BACKUP_LOCATION");
      if (newBackupLocation != null
          && oldBackupLocation != null
          && !StringUtils.equals(newBackupLocation.textValue(), oldBackupLocation.textValue())) {
        beanValidator.error().forField("data.BACKUP_LOCATION", "Field is read-only.").throwError();
      }
    }

    validators.forEach(
        v ->
            v.validate(
                customerConfig.getType().name(),
                customerConfig.getName(),
                customerConfig.getData()));
  }

  public void validateConfigRemoval(CustomerConfig customerConfig) {
    if (customerConfig.getType() == ConfigType.STORAGE) {
      List<Backup> backupList = Backup.getInProgressAndCompleted(customerConfig.getCustomerUUID());
      backupList =
          backupList
              .stream()
              .filter(
                  b -> b.getBackupInfo().storageConfigUUID.equals(customerConfig.getConfigUUID()))
              .collect(Collectors.toList());
      if (!backupList.isEmpty()) {
        beanValidator
            .error()
            .global(
                String.format(
                    "Configuration %s is used in backup and can't be deleted",
                    customerConfig.getConfigName()))
            .throwError();
      }
      List<Schedule> scheduleList =
          Schedule.getActiveBackupSchedules(customerConfig.getCustomerUUID());
      // This should be safe to do since storageConfigUUID is a required constraint.
      scheduleList =
          scheduleList
              .stream()
              .filter(
                  s ->
                      s.getTaskParams()
                          .path("storageConfigUUID")
                          .asText()
                          .equals(customerConfig.getConfigUUID().toString()))
              .collect(Collectors.toList());
      if (!scheduleList.isEmpty()) {
        beanValidator
            .error()
            .global(
                String.format(
                    "Configuration %s is used in scheduled backup and can't be deleted",
                    customerConfig.getConfigName()))
            .throwError();
      }
    }
  }

  // TODO: move this out to some common util file.
  public static AmazonS3Client create(String key, String secret) {
    AWSCredentials credentials = new BasicAWSCredentials(key, secret);
    return new AmazonS3Client(credentials);
  }
}
