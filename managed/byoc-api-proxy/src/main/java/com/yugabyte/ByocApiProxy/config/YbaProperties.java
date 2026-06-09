package com.yugabyte.ByocApiProxy.config;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotNull;
import java.util.UUID;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

@ConfigurationProperties(prefix = "yba")
@Validated
public record YbaProperties(@NotNull UUID uuid, @NotBlank String baseUrl) {}
