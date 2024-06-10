package com.yugabyte.yw.common.swagger;

import io.swagger.core.filter.AbstractSpecFilter;
import io.swagger.model.ApiDescription;
import io.swagger.models.Model;
import io.swagger.models.ModelImpl;
import io.swagger.models.Operation;
import io.swagger.models.parameters.Parameter;
import io.swagger.models.properties.Property;
import java.util.List;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PlatformSwaggerSpecFilter extends AbstractSpecFilter {
  public static final Logger LOG = LoggerFactory.getLogger(PlatformSwaggerSpecFilter.class);
  // Look for APIs marked with this string in its API annotation value
  private static final String INTERNAL_MARKER = "YbaApi Internal";
  private static final String DEPRECATED_MARKER = "Deprecated since YBA version";
  private boolean isStrictMode = false;

  public PlatformSwaggerSpecFilter() {
    PlatformModelConverter.register();
    isStrictMode = "all".equalsIgnoreCase(PlatformModelConverter.excludeYbaDeprecatedOption);
  }

  @Override
  public boolean isOperationAllowed(
      Operation operation,
      ApiDescription api,
      Map<String, List<String>> params,
      Map<String, String> cookies,
      Map<String, List<String>> headers) {
    if (operation.getDescription() != null
        && operation.getDescription().contains(INTERNAL_MARKER)) {
      LOG.info("Skipping swagger generation for internal method '{}'", operation.getOperationId());
      return false;
    }
    if (isStrictMode) {
        if (operation.getDescription() != null
            && operation.getDescription().contains(DEPRECATED_MARKER)) {
        LOG.info("Skipping deprecated method in strict mode '{}'", operation.getOperationId());
        return false;
      }
    }
    return true;
  }

  @Override
  public boolean isParamAllowed(
      Parameter parameter,
      Operation operation,
      ApiDescription api,
      Map<String, List<String>> params,
      Map<String, String> cookies,
      Map<String, List<String>> headers) {
    if (parameter.getDescription() != null
        && parameter.getDescription().contains(INTERNAL_MARKER)) {
      LOG.info(
          "Skipping swagger generation for internal param '{}' of operation '{}'",
          parameter.getName(),
          operation.getOperationId());
      return false;
    }
    if (isStrictMode) {
      if (parameter.getDescription() != null
          && parameter.getDescription().contains(DEPRECATED_MARKER)) {
        LOG.info(
            "Skipping deprecated param in strict mode '{}' of operation '{}",
            parameter.getName(),
            operation.getOperationId());
        return false;
      }
    }
    return true;
  }

  @Override
  public boolean isDefinitionAllowed(
      Model model,
      Map<String, List<String>> params,
      Map<String, String> cookies,
      Map<String, List<String>> headers) {
    if (model.getDescription() != null && model.getDescription().contains(INTERNAL_MARKER)) {
      LOG.info("Skipping swagger generation for model '{}'", ((ModelImpl) model).getName());
      return false;
    }
    if (isStrictMode) {
      if (model.getDescription() != null && model.getDescription().contains(DEPRECATED_MARKER)) {
        LOG.info("Skipping deprecated model in strict mode '{}'", ((ModelImpl) model).getName());
        return false;
      }
    }
    return true;
  }

  @Override
  public boolean isPropertyAllowed(
      Model model,
      Property property,
      String propertyName,
      Map<String, List<String>> params,
      Map<String, String> cookies,
      Map<String, List<String>> headers) {
    if (property.getDescription() != null && property.getDescription().contains(INTERNAL_MARKER)) {
      LOG.info(
          "Skipping swagger generation for property '{}' of model '{}'",
          property.getName(),
          ((ModelImpl) model).getName());
      return false;
    }
    // TODO: move deprecation skipping of model property from PlatformModelConverter to here
    return true;
  }
}
