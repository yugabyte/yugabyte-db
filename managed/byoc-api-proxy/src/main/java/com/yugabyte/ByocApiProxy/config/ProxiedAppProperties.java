package com.yugabyte.ByocApiProxy.config;

import jakarta.validation.Valid;
import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotNull;
import java.time.Duration;
import java.util.Locale;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.bind.DefaultValue;
import org.springframework.validation.Errors;
import org.springframework.validation.Validator;
import org.springframework.validation.annotation.Validated;

@ConfigurationProperties(prefix = "proxied-app")
@Validated
public record ProxiedAppProperties(
    @NotBlank String baseUrl,
    @NotNull Duration readTimeout,
    @DefaultValue("10") @Min(1) @Max(10_000) int pollBatchSize,
    String certificate,
    @NotNull @Valid Auth auth)
    implements Validator {

  public record Auth(
      @NotNull ProxiedAppAuthType type,
      @Valid ServiceAccount serviceAccount,
      @Valid String apiKey) {}

  public record ServiceAccount(
      @NotBlank String email, @NotBlank String password, @NotNull Duration refreshInterval) {}

  @Override
  public boolean supports(Class<?> clazz) {
    return ProxiedAppProperties.class.isAssignableFrom(clazz);
  }

  /**
   * Cross-field auth checks that Bean Validation cannot express: which nested auth block is
   * required depends on {@code auth.type}. Nested {@code @Valid} does not cascade into null fields,
   * so missing {@code service_account} / {@code api_key} material is enforced here.
   */
  @Override
  public void validate(Object target, Errors errors) {
    Auth auth = ((ProxiedAppProperties) target).auth();
    if (auth == null || auth.type() == null) {
      // Covered by @NotNull on auth / type.
      return;
    }

    switch (auth.type()) {
      case SERVICE_ACCOUNT -> rejectIfAbsent(
          errors, "auth.serviceAccount", auth.serviceAccount() != null, auth.type());
      case API_KEY -> rejectIfAbsent(
          errors, "auth.apiKey", auth.apiKey() != null && !auth.apiKey().isBlank(), auth.type());
    }
  }

  private static void rejectIfAbsent(
      Errors errors, String field, boolean present, ProxiedAppAuthType type) {
    if (present) {
      return;
    }

    errors.rejectValue(
        field,
        "proxied-app.auth.required",
        String.format(
            "is required when proxied-app.auth.type is %s", type.name().toLowerCase(Locale.ROOT)));
  }
}
