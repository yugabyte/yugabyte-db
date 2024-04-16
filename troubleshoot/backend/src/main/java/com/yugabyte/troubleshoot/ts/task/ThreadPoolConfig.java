package com.yugabyte.troubleshoot.ts.task;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;

@Configuration
public class ThreadPoolConfig {

  @Bean
  public ThreadPoolTaskExecutor pgStatStatementsQueryExecutor(
      @Value("${task.pg_stat_statements_query.threads}") int threads) {
    ThreadPoolTaskExecutor taskExecutor = new ThreadPoolTaskExecutor();
    taskExecutor.setCorePoolSize(threads);
    taskExecutor.setMaxPoolSize(threads);
    taskExecutor.setThreadNamePrefix("pss_query");
    taskExecutor.initialize();
    return taskExecutor;
  }

  @Bean
  public ThreadPoolTaskExecutor pgStatStatementsNodesQueryExecutor(
      @Value("${task.pg_stat_statements_nodes_query.threads}") int threads) {
    ThreadPoolTaskExecutor taskExecutor = new ThreadPoolTaskExecutor();
    taskExecutor.setCorePoolSize(threads);
    taskExecutor.setMaxPoolSize(threads);
    taskExecutor.setThreadNamePrefix("pss_nodes_query");
    taskExecutor.initialize();
    return taskExecutor;
  }

  @Bean
  public ThreadPoolTaskExecutor activeSessionHistoryQueryExecutor(
      @Value("${task.active_session_history_query.threads}") int threads) {
    ThreadPoolTaskExecutor taskExecutor = new ThreadPoolTaskExecutor();
    taskExecutor.setCorePoolSize(threads);
    taskExecutor.setMaxPoolSize(threads);
    taskExecutor.setThreadNamePrefix("ash_query");
    taskExecutor.initialize();
    return taskExecutor;
  }

  @Bean
  public ThreadPoolTaskExecutor activeSessionHistoryNodesQueryExecutor(
      @Value("${task.active_session_history_nodes_query.threads}") int threads) {
    ThreadPoolTaskExecutor taskExecutor = new ThreadPoolTaskExecutor();
    taskExecutor.setCorePoolSize(threads);
    taskExecutor.setMaxPoolSize(threads);
    taskExecutor.setThreadNamePrefix("ash_nodes_query");
    taskExecutor.initialize();
    return taskExecutor;
  }

  @Bean
  public ThreadPoolTaskExecutor universeDetailsQueryExecutor(
      @Value("${task.universe_details_query.threads}") int threads) {
    ThreadPoolTaskExecutor taskExecutor = new ThreadPoolTaskExecutor();
    taskExecutor.setCorePoolSize(threads);
    taskExecutor.setMaxPoolSize(threads);
    taskExecutor.setThreadNamePrefix("ud_query");
    taskExecutor.initialize();
    return taskExecutor;
  }

  @Bean
  public ThreadPoolTaskExecutor metricQueryExecutor(
      @Value("${task.metric_query.threads}") int threads) {
    ThreadPoolTaskExecutor taskExecutor = new ThreadPoolTaskExecutor();
    taskExecutor.setCorePoolSize(threads);
    taskExecutor.setMaxPoolSize(threads);
    taskExecutor.setThreadNamePrefix("metric_query");
    taskExecutor.initialize();
    return taskExecutor;
  }

  @Bean
  public ThreadPoolTaskExecutor anomalyDetectionExecutor(
      @Value("${task.anomaly_detection.threads}") int threads) {
    ThreadPoolTaskExecutor taskExecutor = new ThreadPoolTaskExecutor();
    taskExecutor.setCorePoolSize(threads);
    taskExecutor.setMaxPoolSize(threads);
    taskExecutor.setThreadNamePrefix("metric_query");
    taskExecutor.initialize();
    return taskExecutor;
  }
}
