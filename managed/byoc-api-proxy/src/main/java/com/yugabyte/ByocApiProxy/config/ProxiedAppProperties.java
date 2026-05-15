package com.yugabyte.ByocApiProxy.config;

import jakarta.validation.Valid;
import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotNull;
import java.time.Duration;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.bind.DefaultValue;
import org.springframework.validation.annotation.Validated;

@ConfigurationProperties(prefix = "proxied-app")
@Validated
public record ProxiedAppProperties(
    @NotBlank String baseUrl,
    @NotNull Duration readTimeout,
    @DefaultValue("10") @Min(1) @Max(10_000) int pollBatchSize,
    @NotNull @Valid Auth auth) {

  public record Auth(
      @NotNull ProxiedAppAuthType type,
      @Valid ServiceAccount serviceAccount,
      @Valid String apiKey) {}

  public record ServiceAccount(
      @NotBlank String email, @NotBlank String password, @NotNull Duration refreshInterval) {}
}
