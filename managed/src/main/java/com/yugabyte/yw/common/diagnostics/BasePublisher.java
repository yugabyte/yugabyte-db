package com.yugabyte.yw.common.diagnostics;

import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.util.HashMap;
import java.util.Map;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import kamon.Kamon;
import kamon.metric.Metric;
import lombok.extern.slf4j.Slf4j;

/**
 * Base publisher class that provides common functionality for publishing content to various
 * destinations. Eliminates code duplication between ThreadDumpPublisher and SupportBundlePublisher.
 */
@Slf4j
@Singleton
public abstract class BasePublisher<T> {
  public static final String DESTINATION_TAG = "destination";

  protected final RuntimeConfigFactory runtimeConfigFactory;
  protected final Map<String, Destination<T>> destinations = new HashMap<>();
  protected final Metric.Counter publishFailureCounter;

  protected BasePublisher(RuntimeConfigFactory runtimeConfigFactory, String metricPrefix) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.publishFailureCounter =
        Kamon.counter(
            metricPrefix + "_publish_failed", "Counter of failed attempts to publish content");
  }

  /**
   * Initialize destinations. This method should be called after the constructor to avoid
   * this-escape warnings. Subclasses should call this in their constructors or provide a separate
   * initialization method.
   */
  protected final void initializeDestinations() {
    createDestinations();
  }

  /**
   * Create destinations. Subclasses should implement this to add their specific destinations. This
   * method is called after the constructor is complete to avoid this-escape warnings.
   */
  protected abstract void createDestinations();

  /**
   * Publish content to all enabled destinations with a specific blob path.
   *
   * @param content The content to publish
   * @param blobPath The blob path for the upload
   * @return true if at least one destination succeeded, false otherwise
   */
  protected boolean publishToEnabledDestinations(T content, String blobPath) {
    boolean anySuccess = false;
    Map<String, Destination<T>> enabledDestinations =
        destinations.entrySet().stream()
            .filter(d -> d.getValue().enabled())
            .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));

    for (Map.Entry<String, Destination<T>> destinationEntry : enabledDestinations.entrySet()) {
      String name = destinationEntry.getKey();

      try {
        if (destinationEntry.getValue().publish(content, blobPath)) {
          anySuccess = true;
          log.info(
              "Successfully published content to destination '{}' at path '{}'", name, blobPath);
        } else {
          publishFailureCounter.withTag(DESTINATION_TAG, name).increment(1);
          log.error("Failed to publish content to destination '{}' at path '{}'", name, blobPath);
        }
      } catch (Exception e) {
        log.error("Failed to publish content to destination '{}' at path '{}'", name, blobPath, e);
        publishFailureCounter.withTag(DESTINATION_TAG, name).increment(1);
      }
    }

    return anySuccess;
  }

  /**
   * Generic destination interface for publishing content. Publishers generate paths and pass them
   * to destinations for upload.
   */
  public interface Destination<T> {
    boolean publish(T content, String blobPath);

    boolean enabled();
  }
}
