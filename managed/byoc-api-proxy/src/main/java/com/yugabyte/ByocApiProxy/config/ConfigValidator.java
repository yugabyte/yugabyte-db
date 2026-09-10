// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.ByocApiProxy.config;

import jakarta.validation.ConstraintViolation;
import jakarta.validation.Validation;
import jakarta.validation.Validator;
import jakarta.validation.ValidatorFactory;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.springframework.boot.Banner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.context.event.ApplicationEnvironmentPreparedEvent;
import org.springframework.boot.context.properties.bind.BindException;
import org.springframework.boot.context.properties.bind.Bindable;
import org.springframework.boot.context.properties.bind.Binder;
import org.springframework.boot.context.properties.bind.PropertySourcesPlaceholdersResolver;
import org.springframework.boot.context.properties.source.ConfigurationProperty;
import org.springframework.boot.context.properties.source.ConfigurationPropertyName;
import org.springframework.boot.context.properties.source.ConfigurationPropertySource;
import org.springframework.boot.context.properties.source.ConfigurationPropertySources;
import org.springframework.context.ApplicationListener;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.env.ConfigurableEnvironment;
import org.springframework.core.env.EnumerablePropertySource;
import org.springframework.core.env.PropertySource;
import org.springframework.validation.BeanPropertyBindingResult;
import org.springframework.validation.FieldError;
import org.springframework.validation.ObjectError;

/**
 * Validates BYOC API proxy configuration without starting poller / client beans.
 *
 * <p>Unlike a normal application start (which fails on the first invalid
 * {@code @ConfigurationProperties} bean), this path binds {@code yba} and {@code proxied-app}
 * independently, runs Jakarta Bean Validation on each, then runs any Spring {@link
 * org.springframework.validation.Validator} implemented by the bound object (e.g. auth cross-checks
 * on {@link ProxiedAppProperties}), and reports every problem found.
 *
 * <p>Invoke via {@code --validate-config} (see {@link
 * com.yugabyte.ByocApiProxy.ByocApiProxyApplication}).
 */
public final class ConfigValidator {

  public static final String FLAG = "--validate-config";

  /**
   * Matches a required placeholder with no default, e.g. {@code ${YBA_UUID}}. When the referenced
   * property is unset, Spring's binder often leaves this literal in place and then fails type
   * conversion (e.g. to {@link java.util.UUID}).
   */
  private static final Pattern REQUIRED_PLACEHOLDER = Pattern.compile("^\\$\\{([A-Za-z0-9._-]+)}$");

  private ConfigValidator() {}

  public static boolean isValidateConfigRequest(String[] args) {
    return args != null && Arrays.asList(args).contains(FLAG);
  }

  /**
   * Loads the Spring environment the same way the app would (application.yaml, env vars, {@code
   * spring.config.additional-location}, etc.), validates, and prints a report.
   *
   * @return {@code 0} if valid, {@code 1} if one or more problems were found
   */
  public static int validateAndReport(String[] args) {
    return validateAndReport(args, System.out, System.err);
  }

  static int validateAndReport(String[] args, PrintStream out, PrintStream err) {
    List<String> errors;
    try {
      errors = loadEnvironmentAndCollectErrors(stripFlag(args), out);
    } catch (Exception e) {
      err.printf("Failed to load configuration environment: %s%n", e.getMessage());
      e.printStackTrace(err);
      return 1;
    }

    if (errors.isEmpty()) {
      out.println("Configuration is valid.");
      return 0;
    }

    err.printf("Configuration validation failed with %d error(s):%n", errors.size());
    err.println("  - " + String.join("\n  - ", errors));
    return 1;
  }

  static List<String> collectErrors(ConfigurableEnvironment environment) {
    try (ValidatorFactory validatorFactory = Validation.buildDefaultValidatorFactory()) {
      Validator validator = validatorFactory.getValidator();
      Binder binder =
          new Binder(
              ConfigurationPropertySources.get(environment),
              new PropertySourcesPlaceholdersResolver(environment));

      // Preserve insertion order and de-dupe identical messages.
      Set<String> errors = new LinkedHashSet<>();

      // Surface missing env/config placeholders before bind so several can be reported together
      // instead of only the first conversion failure.
      addUnresolvedPlaceholderErrors(environment, errors);

      YbaProperties yba = bind(binder, "yba", YbaProperties.class, errors);
      if (yba != null) {
        addViolations("yba", validator.validate(yba), errors);
        addSpringValidationErrors("yba", yba, errors);
      }

      ProxiedAppProperties proxiedApp =
          bind(binder, "proxied-app", ProxiedAppProperties.class, errors);
      if (proxiedApp != null) {
        addViolations("proxied-app", validator.validate(proxiedApp), errors);
        addSpringValidationErrors("proxied-app", proxiedApp, errors);
      }

      return new ArrayList<>(errors);
    }
  }

  private static List<String> loadEnvironmentAndCollectErrors(String[] args, PrintStream out) {
    AtomicReference<List<String>> collected = new AtomicReference<>();

    SpringApplication app = new SpringApplication(NoOpConfiguration.class);
    app.setWebApplicationType(WebApplicationType.NONE);
    app.setBannerMode(Banner.Mode.OFF);
    app.setLogStartupInfo(false);
    app.setRegisterShutdownHook(false);
    app.addListeners(
        (ApplicationListener<ApplicationEnvironmentPreparedEvent>)
            event -> collected.set(collectErrors(event.getEnvironment())));

    try (ConfigurableApplicationContext ignored = app.run(args)) {
      // Environment is prepared (and validated) before the context refreshes.
      out.println("Loaded application context");
    }

    List<String> errors = collected.get();
    if (errors == null) {
      throw new IllegalStateException(
          "Environment was not prepared; cannot validate configuration");
    }

    return errors;
  }

  private static <T> T bind(Binder binder, String prefix, Class<T> type, Set<String> errors) {
    try {
      return binder
          .bind(prefix, Bindable.of(type))
          .orElseGet(
              () -> {
                errors.add(String.format("%s: missing or empty configuration", prefix));
                return null;
              });
    } catch (BindException e) {
      errors.add(formatBindException(e));
      return null;
    } catch (Exception e) {
      errors.add(String.format("%s: failed to bind (%s)", prefix, e.getMessage()));
      return null;
    }
  }

  private static String formatBindException(BindException e) {
    ConfigurationPropertyName name = e.getName();
    String property = name.toString();
    Object value = e.getProperty() != null ? e.getProperty().getValue() : null;

    String unresolved = unresolvedPlaceholderMessage(property, value);
    if (unresolved != null) {
      return unresolved;
    }

    if (value instanceof String s && s.isBlank()) {
      return String.format("%s: must not be blank", property);
    }

    return String.format("%s: invalid value '%s'", property, value);
  }

  /**
   * Finds {@code ${NAME}} placeholders under {@code yba} / {@code proxied-app} whose referenced
   * property is unset, so missing env vars are reported clearly and can be aggregated.
   *
   * <p>Auth-specific placeholders are only reported for the active {@code proxied-app.auth.type}
   * (default {@code service_account}, matching {@code application.yaml}). Otherwise optional blocks
   * like {@code api_key} / {@code service_account} would be flagged even when unused.
   *
   * <p>A {@code ${VAR}} default is only reported when the property has no actual value: e.g. an
   * installer-generated application.yaml overlay setting {@code yba.uuid} silences the bundled
   * {@code ${YBA_UUID}} default.
   */
  private static void addUnresolvedPlaceholderErrors(
      ConfigurableEnvironment environment, Set<String> errors) {
    ProxiedAppAuthType authType = resolveAuthType(environment);
    Iterable<ConfigurationPropertySource> configSources =
        ConfigurationPropertySources.get(environment);

    for (PropertySource<?> source : environment.getPropertySources()) {
      if (!(source instanceof EnumerablePropertySource<?> enumerable)) {
        continue;
      }
      for (String propertyName : enumerable.getPropertyNames()) {
        if (!isAppConfigProperty(propertyName)) {
          continue;
        }
        String normalized = normalizePropertyName(propertyName);
        if (!isPlaceholderRelevantForAuth(normalized, authType)) {
          continue;
        }
        Object raw = enumerable.getProperty(propertyName);
        String placeholder = placeholderName(raw);
        if (placeholder == null || environment.getProperty(placeholder) != null) {
          continue;
        }
        if (isPropertySet(configSources, normalized)) {
          continue;
        }
        errors.add(unresolvedPlaceholderMessage(normalized, raw));
      }
    }
  }

  /**
   * True when the property has an actual value. Sources are checked highest-precedence first (the
   * same order and relaxed name matching the binder uses, so snake_case yaml keys count), and the
   * first source that defines the property decides.
   */
  private static boolean isPropertySet(
      Iterable<ConfigurationPropertySource> configSources, String property) {
    ConfigurationPropertyName name;
    try {
      name = ConfigurationPropertyName.of(property);
    } catch (RuntimeException e) {
      return false;
    }
    for (ConfigurationPropertySource source : configSources) {
      ConfigurationProperty resolved = source.getConfigurationProperty(name);
      if (resolved == null) {
        continue;
      }
      if (placeholderName(resolved.getValue()) != null) {
        // The winning value is itself a bare ${VAR} - still not set.
        return false;
      }
      return true;
    }
    // No source defines the property at all.
    return false;
  }

  private static ProxiedAppAuthType resolveAuthType(ConfigurableEnvironment environment) {
    String raw =
        firstNonBlank(
            environment.getProperty("proxied-app.auth.type"),
            environment.getProperty("proxied_app.auth.type"),
            environment.getProperty("PROXIED_APP_AUTH_TYPE"));
    if (raw == null) {
      // Matches application.yaml default: ${PROXIED_APP_AUTH_TYPE:service_account}
      return ProxiedAppAuthType.SERVICE_ACCOUNT;
    }
    try {
      return ProxiedAppAuthType.valueOf(raw.trim().toUpperCase(Locale.ROOT));
    } catch (IllegalArgumentException e) {
      // Invalid type is reported by binding / Bean Validation; don't filter placeholders.
      return null;
    }
  }

  private static String firstNonBlank(String... values) {
    for (String value : values) {
      if (value != null && !value.isBlank()) {
        return value;
      }
    }
    return null;
  }

  private static boolean isPlaceholderRelevantForAuth(
      String normalizedProperty, ProxiedAppAuthType authType) {
    if (authType == null) {
      return true;
    }
    boolean serviceAccountProperty =
        normalizedProperty.startsWith("proxied-app.auth.service-account");
    boolean apiKeyProperty = normalizedProperty.equals("proxied-app.auth.api-key");
    return switch (authType) {
      case SERVICE_ACCOUNT -> !apiKeyProperty;
      case API_KEY -> !serviceAccountProperty;
    };
  }

  private static boolean isAppConfigProperty(String propertyName) {
    String normalized = normalizePropertyName(propertyName);
    return normalized.startsWith("yba.") || normalized.startsWith("proxied-app.");
  }

  /** Relaxed names like {@code proxied_app.base_url} -> {@code proxied-app.base-url}. */
  private static String normalizePropertyName(String propertyName) {
    return propertyName.replace('_', '-');
  }

  private static String placeholderName(Object value) {
    if (!(value instanceof String s)) {
      return null;
    }
    Matcher matcher = REQUIRED_PLACEHOLDER.matcher(s.trim());
    return matcher.matches() ? matcher.group(1) : null;
  }

  private static String unresolvedPlaceholderMessage(String property, Object value) {
    String placeholder = placeholderName(value);
    if (placeholder == null) {
      return null;
    }
    return String.format(
        "%s: not set (set environment variable %s or property %s)",
        property, placeholder, property);
  }

  private static void addViolations(
      String prefix, Set<? extends ConstraintViolation<?>> violations, Set<String> errors) {
    for (ConstraintViolation<?> violation : violations) {
      String path = violation.getPropertyPath().toString();
      String property = path.isEmpty() ? prefix : String.format("%s.%s", prefix, path);
      errors.add(String.format("%s: %s", property, violation.getMessage()));
    }
  }

  /**
   * Runs Spring {@link org.springframework.validation.Validator} logic when the bound object
   * implements it (same hook Spring Boot uses for {@code @Validated}
   * {@code @ConfigurationProperties}).
   */
  private static void addSpringValidationErrors(
      String objectName, Object target, Set<String> errors) {
    if (!(target instanceof org.springframework.validation.Validator springValidator)
        || !springValidator.supports(target.getClass())) {
      return;
    }

    BeanPropertyBindingResult result = new BeanPropertyBindingResult(target, objectName);
    springValidator.validate(target, result);

    for (FieldError error : result.getFieldErrors()) {
      errors.add(
          String.format("%s.%s: %s", objectName, error.getField(), error.getDefaultMessage()));
    }

    for (ObjectError error : result.getGlobalErrors()) {
      errors.add(String.format("%s: %s", objectName, error.getDefaultMessage()));
    }
  }

  private static String[] stripFlag(String[] args) {
    if (args == null || args.length == 0) {
      return new String[0];
    }

    return Arrays.stream(args).filter(arg -> !FLAG.equals(arg)).toArray(String[]::new);
  }

  /** Marker configuration so {@link SpringApplication} can prepare an environment only. */
  @Configuration
  static class NoOpConfiguration {}
}
