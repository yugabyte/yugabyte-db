package com.yugabyte.troubleshoot.ts.configuration;

import io.ebean.Database;
import io.ebean.DatabaseFactory;
import io.ebean.config.DatabaseConfig;
import io.ebean.spring.txn.SpringJdbcTransactionManager;
import javax.sql.DataSource;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class EbeanConfiguration {

  @Bean
  public DatabaseConfig databaseConfig(DataSource dataSource) {
    DatabaseConfig config = new DatabaseConfig();
    config.setDefaultServer(true);
    config.setDataSource(dataSource);
    config.addPackage("com.yugabyte.troubleshoot.ts.models");
    config.setExternalTransactionManager(new SpringJdbcTransactionManager());
    config.setExpressionNativeIlike(true);
    return config;
  }

  @Bean
  public Database database(DatabaseConfig config) {
    return DatabaseFactory.create(config);
  }
}
