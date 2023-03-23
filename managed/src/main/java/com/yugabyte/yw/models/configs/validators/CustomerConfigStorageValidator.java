// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.Collection;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.DomainValidator;
import org.apache.commons.validator.routines.UrlValidator;

public class CustomerConfigStorageValidator extends ConfigDataValidator {

  private static final String DEFAULT_SCHEME = "https://";

  private static final Integer MIN_PORT_VALUE = 0;

  private static final Integer MAX_PORT_VALUE = 65535;

  private static final Integer HTTP_PORT = 80;

  private static final Integer HTTPS_PORT = 443;

  private final UrlValidator urlValidator;

  @Inject
  public CustomerConfigStorageValidator(BeanValidator beanValidator, Collection<String> schemes) {
    super(beanValidator);

    DomainValidator domainValidator = DomainValidator.getInstance(true);
    urlValidator =
        new UrlValidator(
            schemes.toArray(new String[0]), null, UrlValidator.ALLOW_LOCAL_URLS, domainValidator);
  }

  @Override
  public void validate(CustomerConfigData data) {
    validateUrl(
        CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME,
        ((CustomerConfigStorageData) data).backupLocation,
        false,
        false);
  }

  protected void validateUrl(
      String fieldName, String value, boolean emptyAllowed, boolean validatePort) {
    if (StringUtils.isEmpty(value)) {
      if (!emptyAllowed) {
        throwBeanValidatorError(fieldName, "This field is required.");
      }
      return;
    }

    boolean valid = false;
    try {
      URI uri = new URI(value);
      if (validatePort) {
        if (StringUtils.isEmpty(uri.getHost())) {
          uri = new URI(DEFAULT_SCHEME + value);
        }
        String host = uri.getHost();
        String scheme = uri.getScheme() + "://";
        String uriToValidate = scheme + host;
        Integer port = uri.getPort();
        boolean validPort = true;
        if (!uri.toString().equals(uriToValidate)
            && (port < MIN_PORT_VALUE
                || port > MAX_PORT_VALUE
                || HTTPS_PORT.equals(port)
                || HTTP_PORT.equals(port))) {
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
      String errorMsg = "Invalid field value '" + value + "'";
      throwBeanValidatorError(fieldName, errorMsg);
    }
  }
}
