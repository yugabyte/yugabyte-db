package com.yugabyte.troubleshoot.ts.configuration;

import io.prometheus.client.CollectorRegistry;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Primary;

@Configuration
public class MetricsConfig {

  @Bean
  @Primary
  public CollectorRegistry collectorRegistry() {
    return CollectorRegistry.defaultRegistry;
  }
}
