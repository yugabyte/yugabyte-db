package com.yugabyte.ByocApiProxy;

import com.yugabyte.ByocApiProxy.config.ConfigValidator;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.ConfigurationPropertiesScan;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication
@ConfigurationPropertiesScan
@EnableScheduling
public class ByocApiProxyApplication {

  public static void main(String[] args) {
    if (ConfigValidator.isValidateConfigRequest(args)) {
      System.exit(ConfigValidator.validateAndReport(args));
    }

    SpringApplication.run(ByocApiProxyApplication.class, args);
  }
}
