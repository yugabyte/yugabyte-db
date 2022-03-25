// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.models.CustomerConfig.ConfigType.PASSWORD_POLICY;
import static com.yugabyte.yw.models.CustomerConfig.ConfigType.STORAGE;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.REGION_LOCATION_FIELDNAME;
import static play.mvc.Http.Status.CONFLICT;

import com.amazonaws.SDKGlobalConfiguration;
import com.amazonaws.SdkClientException;
import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.client.builder.AwsClientBuilder.EndpointConfiguration;
import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobStorageException;
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
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.forms.PasswordPolicyFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.Schedule;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.net.UnknownHostException;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map.Entry;
import java.util.Queue;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.DomainValidator;
import org.apache.commons.validator.routines.UrlValidator;
import org.yb.util.Pair;

// TODO: S.Potachev: To refactor code to use Java classes instead of pure JSONs.

@Singleton
@Slf4j
public class CustomerConfigValidator {

  public static final String NAME_S3 = "S3";

  public static final String NAME_GCS = "GCS";

  public static final String NAME_NFS = "NFS";

  public static final String NAME_AZURE = "AZ";

  private static final String[] S3_URL_SCHEMES = {"http", "https", "s3"};

  private static final String[] GCS_URL_SCHEMES = {"http", "https", "gs"};

  private static final String[] AZ_URL_SCHEMES = {"http", "https"};

  private static final String[] TLD_OVERRIDE = {"local"};

  private static final String AWS_HOST_BASE_FIELDNAME = "AWS_HOST_BASE";

  public static final String AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";

  public static final String AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";

  public static final String AWS_PATH_STYLE_ACCESS = "PATH_STYLE_ACCESS";

  public static final String GCS_CREDENTIALS_JSON_FIELDNAME = "GCS_CREDENTIALS_JSON";

  private static final String NFS_PATH_REGEXP = "^/|//|(/[\\w-]+)+$";

  public static final Integer MIN_PORT_VALUE = 0;

  public static final Integer MAX_PORT_VALUE = 65535;

  public static final Integer HTTP_PORT = 80;

  public static final Integer HTTPS_PORT = 443;

  private final BeanValidator beanValidator;

  @VisibleForTesting
  static String fieldFullName(String fieldName) {
    if (StringUtils.isEmpty(fieldName)) {
      return "data";
    }
    return "data." + fieldName;
  }

  public abstract static class ConfigValidator {

    protected final String type;

    protected final String name;

    public ConfigValidator(String type, String name) {
      this.type = type;
      this.name = name;
    }

    public void validate(String type, String name, JsonNode data) {
      if (this.type.equals(type) && this.name.equals(name)) {
        doValidate(0, data);
      }
    }

    protected abstract void doValidate(int level, JsonNode data);
  }

  public abstract static class ConfigFieldValidator extends ConfigValidator {

    protected final String fieldName;

    protected boolean emptyAllowed;

    static {
      DomainValidator.updateTLDOverride(DomainValidator.ArrayType.LOCAL_PLUS, TLD_OVERRIDE);
    }

    public ConfigFieldValidator(String type, String name, String fieldName, boolean emptyAllowed) {
      super(type, name);
      this.fieldName = fieldName;
      this.emptyAllowed = emptyAllowed;
    }

    // @formatter:off
    /*
     * @param level is a nesting level where data is found. For child classes it is
     * used to check if we need to raise an exception for fields which don't allow
     * empty values.
     *
     * As example,
     *    new ConfigValidatorUrl(STORAGE.name(),
     *                           NAME_S3,
     *                           REGION_LOCATION_FIELDNAME,
     *                           S3_URL_SCHEMES, 1, true));
     * It means that for configurations of S3 storages on the second level of variables
     * we may have field REGION_LOCATION_FIELDNAME.
     *
     * And for:
     *    new ConfigValidatorUrl(STORAGE.name(),
     *                           NAME_S3,
     *                           BACKUP_LOCATION_FIELDNAME,
     *                           S3_URL_SCHEMES,
     *                           0,
     *                           false))
     * We have field BACKUP_LOCATION_FIELDNAME on the first level which can't be empty.
     *
     */
    // @formatter:on
    @Override
    public final void doValidate(int level, JsonNode data) {
      if (data.isArray()) {
        for (JsonNode item : data) {
          doValidate(level + 1, item);
        }
        return;
      }

      if (data.isObject()) {
        for (JsonNode item : data) {
          if (item.isArray()) {
            doValidate(level + 1, item);
          }
        }
      }

      JsonNode value = data.get(fieldName);
      doValidate(level, value == null ? "" : value.asText());
    }

    protected abstract void doValidate(int level, String value);
  }

  public abstract static class ConfigValidatorWithFieldNames extends ConfigValidator {

    protected final List<String> fieldNames;

    public ConfigValidatorWithFieldNames(String type, String name, String[] fieldNames) {
      super(type, name);
      this.fieldNames = Arrays.asList(fieldNames);
    }

    // Collecting fields to check (searching fields with keys from fieldNames in the
    // passed JsonNode and all its descendants).<br>
    // Returns list of pairs - <fieldName, text value>.
    protected List<Pair<String, String>> getCheckedFields(JsonNode data) {
      List<Pair<String, String>> result = new ArrayList<>();

      Queue<JsonNode> queue = new LinkedList<>();
      queue.add(data);
      while (!queue.isEmpty()) {
        JsonNode item = queue.poll();
        Iterator<Entry<String, JsonNode>> it = item.fields();
        while (it.hasNext()) {
          Entry<String, JsonNode> subItem = it.next();
          if (fieldNames.contains(subItem.getKey())) {
            result.add(new Pair<>(subItem.getKey(), subItem.getValue().asText()));
            continue;
          }

          if (subItem.getValue().isArray()) {
            for (JsonNode arrayItem : subItem.getValue()) {
              queue.add(arrayItem);
            }
          }
        }
      }
      return result;
    }
  }

  public class ConfigS3PreflightCheckValidator extends ConfigValidatorWithFieldNames {

    public ConfigS3PreflightCheckValidator(String type, String name, String[] fieldNames) {
      super(type, name, fieldNames);
    }

    @Override
    public void doValidate(int level, JsonNode data) {
      try {
        // Disable cert checking while connecting with s3
        // Enabling it can potentially fail when s3 compatible storages like
        // Dell ECS are provided and custom certs are needed to connect
        // Reference: https://yugabyte.atlassian.net/browse/PLAT-2497
        System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");

        if (this.name.equals("S3") && data.get(AWS_ACCESS_KEY_ID_FIELDNAME) != null) {
          // Get fields to check.
          List<Pair<String, String>> toCheck = getCheckedFields(data);
          if (toCheck.isEmpty()) {
            return;
          }

          AmazonS3 s3Client = null;
          String exceptionMsg = null;
          try {
            s3Client = create(data);
          } catch (AmazonS3Exception s3Exception) {
            exceptionMsg = s3Exception.getErrorMessage();
            throwBeanValidatorError(fieldNames.get(0), exceptionMsg);
          }

          // Check each field.
          for (Pair<String, String> item : toCheck) {
            String fieldName = item.getFirst();
            String s3UriPath = item.getSecond();
            String s3Uri = s3UriPath;
            // Assuming bucket name will always start with s3:// otherwise that will be
            // invalid
            if (s3UriPath.length() < 5 || !s3UriPath.startsWith("s3://")) {
              exceptionMsg = "Invalid s3UriPath format: " + s3UriPath;
              throwBeanValidatorError(fieldName, exceptionMsg);
            } else {
              try {
                s3UriPath = s3UriPath.substring(5);
                String[] bucketSplit = s3UriPath.split("/", 2);
                String bucketName = bucketSplit.length > 0 ? bucketSplit[0] : "";
                String prefix = bucketSplit.length > 1 ? bucketSplit[1] : "";

                // Only the bucket has been given, with no subdir.
                if (bucketSplit.length == 1) {
                  if (!s3Client.doesBucketExistV2(bucketName)) {
                    exceptionMsg = "S3 URI path " + s3Uri + " doesn't exist";
                    throwBeanValidatorError(fieldName, exceptionMsg);
                  }
                } else {
                  ListObjectsV2Result result = s3Client.listObjectsV2(bucketName, prefix);
                  if (result.getKeyCount() == 0) {
                    exceptionMsg = "S3 URI path " + s3Uri + " doesn't exist";
                    throwBeanValidatorError(fieldName, exceptionMsg);
                  }
                }
              } catch (AmazonS3Exception s3Exception) {
                exceptionMsg = s3Exception.getErrorMessage();
                if (exceptionMsg.contains("Denied") || exceptionMsg.contains("bucket"))
                  exceptionMsg += " " + s3Uri;
                throwBeanValidatorError(fieldName, exceptionMsg);
              } catch (SdkClientException e) {
                throwBeanValidatorError(fieldName, e.getMessage());
              }
            }
          }
        }
      } finally {
        // Re-enable cert checking as it applies globally
        System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
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
    protected void doValidate(int level, JsonNode data) {
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
      super(type, name, fieldName, false);
      pattern = Pattern.compile(regex);
    }

    @Override
    protected void doValidate(int level, String value) {
      if (!pattern.matcher(value).matches()) {
        String errorMsg = "Invalid field value '" + value + "'.";
        throwBeanValidatorError(fieldName, errorMsg);
      }
    }
  }

  public class ConfigValidatorUrl extends ConfigFieldValidator {

    private static final String DEFAULT_SCHEME = "https://";

    private final UrlValidator urlValidator;

    private final int level;

    public ConfigValidatorUrl(
        String type,
        String name,
        String fieldName,
        String[] schemes,
        int level,
        boolean emptyAllowed) {
      super(type, name, fieldName, emptyAllowed);
      this.level = level;
      this.emptyAllowed = emptyAllowed;
      DomainValidator domainValidator = DomainValidator.getInstance(true);
      urlValidator =
          new UrlValidator(schemes, null, UrlValidator.ALLOW_LOCAL_URLS, domainValidator);
    }

    @Override
    protected void doValidate(int level, String value) {
      if (StringUtils.isEmpty(value)) {
        if (!emptyAllowed && (this.level == level)) {
          String errorMsg = "This field is required.";
          throwBeanValidatorError(fieldName, errorMsg);
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
              && (port < MIN_PORT_VALUE
                  || port > MAX_PORT_VALUE
                  || port == HTTPS_PORT
                  || port == HTTP_PORT)) {
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
        String errorMsg = "Invalid field value '" + value + "'.";
        throwBeanValidatorError(fieldName, errorMsg);
      }
    }
  }

  public class ConfigGCSPreflightCheckValidator extends ConfigValidatorWithFieldNames {

    public ConfigGCSPreflightCheckValidator(String type, String name, String[] fieldNames) {
      super(type, name, fieldNames);
    }

    @Override
    public void doValidate(int level, JsonNode data) {
      if (this.name.equals(NAME_GCS) && data.get(GCS_CREDENTIALS_JSON_FIELDNAME) != null) {
        // Get fields to check.
        List<Pair<String, String>> toCheck = getCheckedFields(data);
        if (toCheck.isEmpty()) {
          return;
        }

        String gcpCredentials = data.get(GCS_CREDENTIALS_JSON_FIELDNAME).asText();
        Storage storage = null;
        try {
          storage = createGcpStorage(gcpCredentials);
        } catch (IOException ex) {
          throwBeanValidatorError(fieldNames.get(0), ex.getMessage());
        }

        for (Pair<String, String> item : toCheck) {
          String fieldName = item.getFirst();
          String gsUriPath = item.getSecond();

          String gsUri = gsUriPath;
          // Assuming bucket name will always start with gs:// otherwise that will be
          // invalid
          if (gsUriPath.length() < 5 || !gsUriPath.startsWith("gs://")) {
            String exceptionMsg = "Invalid gsUriPath format: " + gsUriPath;
            throwBeanValidatorError(fieldName, exceptionMsg);
          } else {
            String[] bucketSplit = GCPUtil.getSplitLocationValue(gsUriPath);
            String bucketName = bucketSplit.length > 0 ? bucketSplit[0] : "";
            String prefix = bucketSplit.length > 1 ? bucketSplit[1] : "";
            try {
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
                  String exceptionMsg = "GS Uri path " + gsUri + " doesn't exist";
                  throwBeanValidatorError(fieldName, exceptionMsg);
                }
              }
            } catch (StorageException exp) {
              throwBeanValidatorError(fieldName, exp.getMessage());
            } catch (Exception e) {
              String exceptionMsg = "Invalid GCP Credential Json.";
              throwBeanValidatorError(fieldName, exceptionMsg);
            }
          }
        }
      }
    }
  }

  public class ConfigAZPreflightCheckValidator extends ConfigValidatorWithFieldNames {

    public ConfigAZPreflightCheckValidator(String type, String name, String[] fieldNames) {
      super(type, name, fieldNames);
    }

    @Override
    public void doValidate(int level, JsonNode data) {
      if (this.name.equals(NAME_AZURE)
          && data.get(AZUtil.AZURE_STORAGE_SAS_TOKEN_FIELDNAME) != null) {
        // Get fields to check.
        List<Pair<String, String>> toCheck = getCheckedFields(data);
        if (toCheck.isEmpty()) {
          return;
        }

        String azSasToken = data.get(AZUtil.AZURE_STORAGE_SAS_TOKEN_FIELDNAME).asText();
        for (Pair<String, String> item : toCheck) {
          String fieldName = item.getFirst();
          String azUriPath = item.getSecond();

          String exceptionMsg = null;
          // Assuming azure backup location will always start with https://
          if (azUriPath.length() < 8 || !azUriPath.startsWith("https://")) {
            exceptionMsg = "Invalid azUriPath format: " + azUriPath;
            throwBeanValidatorError(fieldName, exceptionMsg);
          } else {
            String[] splitLocation = AZUtil.getSplitLocationValue(azUriPath, false);
            int splitLength = splitLocation.length;
            if (splitLength < 2) {
              // azUrl and container should be there in backup location.
              exceptionMsg = "Invalid azUriPath format: " + azUriPath;
              throwBeanValidatorError(fieldName, exceptionMsg);
            }

            String azUrl = "https://" + splitLocation[0];
            String container = splitLocation[1];

            try {
              BlobContainerClient blobContainerClient =
                  createBlobContainerClient(azUrl, azSasToken, container);
              if (!blobContainerClient.exists()) {
                exceptionMsg = "Blob container " + container + " doesn't exist";
                throwBeanValidatorError(fieldName, exceptionMsg);
              }
            } catch (BlobStorageException e) {
              exceptionMsg = "Invalid SAS token!";
              throwBeanValidatorError(fieldName, exceptionMsg);
            } catch (Exception e) {
              if (e.getCause() != null && e.getCause() instanceof UnknownHostException) {
                exceptionMsg = "Cannot access " + azUrl;
                throwBeanValidatorError(fieldName, exceptionMsg);
              }
              throw e;
            }
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
            STORAGE.name(), NAME_S3, BACKUP_LOCATION_FIELDNAME, S3_URL_SCHEMES, 0, false));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_S3, AWS_HOST_BASE_FIELDNAME, S3_URL_SCHEMES, 0, true));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_S3, REGION_LOCATION_FIELDNAME, S3_URL_SCHEMES, 1, true));

    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_GCS, BACKUP_LOCATION_FIELDNAME, GCS_URL_SCHEMES, 0, false));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_GCS, REGION_LOCATION_FIELDNAME, GCS_URL_SCHEMES, 1, true));

    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_AZURE, BACKUP_LOCATION_FIELDNAME, AZ_URL_SCHEMES, 0, false));
    validators.add(
        new ConfigValidatorUrl(
            STORAGE.name(), NAME_AZURE, REGION_LOCATION_FIELDNAME, AZ_URL_SCHEMES, 1, true));

    validators.add(
        new ConfigObjectValidator<>(
            PASSWORD_POLICY.name(), CustomerConfig.PASSWORD_POLICY, PasswordPolicyFormData.class));

    validators.add(
        new ConfigS3PreflightCheckValidator(
            STORAGE.name(),
            NAME_S3,
            new String[] {BACKUP_LOCATION_FIELDNAME, REGION_LOCATION_FIELDNAME}));

    validators.add(
        new ConfigGCSPreflightCheckValidator(
            STORAGE.name(),
            NAME_GCS,
            new String[] {BACKUP_LOCATION_FIELDNAME, REGION_LOCATION_FIELDNAME}));

    validators.add(
        new ConfigAZPreflightCheckValidator(
            STORAGE.name(),
            NAME_AZURE,
            new String[] {BACKUP_LOCATION_FIELDNAME, REGION_LOCATION_FIELDNAME}));
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

      JsonNode newBackupLocation = customerConfig.getData().get(BACKUP_LOCATION_FIELDNAME);
      JsonNode oldBackupLocation = existentConfig.getData().get(BACKUP_LOCATION_FIELDNAME);
      if (newBackupLocation != null
          && oldBackupLocation != null
          && !StringUtils.equals(newBackupLocation.textValue(), oldBackupLocation.textValue())) {
        String errorMsg = "Field is read-only.";
        throwBeanValidatorError(BACKUP_LOCATION_FIELDNAME, errorMsg);
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
  protected AmazonS3 create(JsonNode data) {
    return AWSUtil.createS3Client(data);
  }

  protected Storage createGcpStorage(String gcpCredentials)
      throws IOException, UnsupportedEncodingException {
    return GCPUtil.getStorageService(gcpCredentials);
  }

  protected BlobContainerClient createBlobContainerClient(
      String azUrl, String azSasToken, String container) {
    return AZUtil.createBlobContainerClient(azUrl, azSasToken, container);
  }

  private void throwBeanValidatorError(String fieldName, String exceptionMsg) {
    beanValidator.error().forField(fieldFullName(fieldName), exceptionMsg).throwError();
  }
}
