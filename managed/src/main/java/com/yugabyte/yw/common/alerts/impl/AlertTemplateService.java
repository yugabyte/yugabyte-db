/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts.impl;

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfiguration.TargetType;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.text.DecimalFormat;
import java.util.*;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import net.logstash.logback.encoder.org.apache.commons.lang3.StringUtils;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.Constructor;
import play.Environment;

@Singleton
@Slf4j
public class AlertTemplateService {

  private static final String QUERY_THRESHOLD_PLACEHOLDER = "{{ query_threshold }}";
  private static final String QUERY_CONDITION_PLACEHOLDER = "{{ query_condition }}";
  private static final DecimalFormat THRESHOLD_FORMAT = new DecimalFormat("0.#");

  Map<AlertTemplate, AlertTemplateDescription> templateDescriptionMap;

  @Inject
  public AlertTemplateService(Environment environment) {
    Yaml yaml = new Yaml(new Constructor(AlertTemplateDescriptionMap.class, new LoaderOptions()));
    AlertTemplateDescriptionMap map =
        yaml.load(environment.resourceAsStream("alert/alert_templates.yml"));
    templateDescriptionMap = map.getTemplates();
  }

  public AlertTemplateDescription getTemplateDescription(AlertTemplate template) {
    return templateDescriptionMap.get(template);
  }

  @Data
  public static class DefaultThreshold {
    String paramName;
    Double threshold;
  }

  @Data
  public static class TestAlertSettings {
    List<Label> additionalLabels = new ArrayList<>();
    String customMessage;
    boolean generateValueFromThreshold = true;
    double customValue = 1D;
  }

  @Data
  public static class Label {
    private String name;
    private String value;
  }

  @Data
  public static class AlertTemplateDescription {
    String name;
    String description;
    String queryTemplate;
    String summaryTemplate;
    int defaultDurationSec;
    boolean createForNewCustomer;
    boolean skipSourceLabels;
    Map<Severity, DefaultThreshold> defaultThresholdMap;
    TargetType targetType;
    Condition defaultThresholdCondition;
    Unit defaultThresholdUnit;
    double thresholdMinValue;
    double thresholdMaxValue = Double.MAX_VALUE;
    boolean thresholdReadOnly;
    boolean thresholdConditionReadOnly = true;
    String thresholdUnitName = "";
    TestAlertSettings testAlertSettings = new TestAlertSettings();
    Map<String, String> labels = new HashMap<>();
    Map<String, String> annotations = new HashMap<>();

    public String getQueryWithThreshold(
        AlertDefinition definition, AlertConfigurationThreshold threshold) {
      String query =
          queryTemplate.replaceAll("__customerUuid__", definition.getCustomerUUID().toString());
      String universeUuid = definition.getLabelValue(KnownAlertLabels.UNIVERSE_UUID);
      if (StringUtils.isNoneEmpty(universeUuid)) {
        query = query.replaceAll("__universeUuid__", universeUuid);
        if (query.contains("__mountPoints__")) {
          Universe universe = Universe.getOrBadRequest(UUID.fromString(universeUuid));
          query =
              query.replaceAll("__mountPoints__", MetricQueryHelper.getDataMountPoints(universe));
        }
        if (query.contains("__systemMountPoints__")) {
          Universe universe = Universe.getOrBadRequest(UUID.fromString(universeUuid));
          RuntimeConfGetter confGetter =
              StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
          query =
              query.replaceAll(
                  "__systemMountPoints__",
                  MetricQueryHelper.getOtherMountPoints(confGetter, universe));
        }
      }
      return replaceThresholdAndCondition(query, threshold);
    }

    public static String replaceThresholdAndCondition(
        String pattern, AlertConfigurationThreshold threshold) {
      return pattern
          .replace(QUERY_THRESHOLD_PLACEHOLDER, THRESHOLD_FORMAT.format(threshold.getThreshold()))
          .replace(QUERY_CONDITION_PLACEHOLDER, threshold.getCondition().getValue());
    }
  }

  @Data
  public static class AlertTemplateDescriptionMap {
    Map<AlertTemplate, AlertTemplateDescription> templates;
  }
}
