/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.templates.PlaceholderSubstitutor;
import com.yugabyte.yw.common.templates.PlaceholderSubstitutorIF;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.RequiredArgsConstructor;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;

public class AlertRuleTemplateSubstitutor implements PlaceholderSubstitutorIF {

  private static final String DEFINITION_NAME = "definition_name";
  private static final String DEFINITION_EXPR = "definition_expr";
  private static final String DURATION = "duration";
  private static final String LABELS = "labels";
  private static final String ANNOTATIONS = "annotations";
  private static final String LABEL_PREFIX = "          ";
  public static final String AFFECTED_NODE_NAMES = "affected_node_names";
  public static final String AFFECTED_NODE_ADDRESSES = "affected_node_addresses";
  public static final String AFFECTED_NODE_IDENTIFIERS = "affected_node_identifiers";
  public static final String AFFECTED_VOLUMES = "affected_volumes";

  private final Map<String, String> labels;

  private final Map<String, String> annotations;

  private final PlaceholderSubstitutor placeholderSubstitutor;

  public AlertRuleTemplateSubstitutor(
      RuntimeConfGetter confGetter,
      AlertTemplateDescription templateDescription,
      AlertConfiguration configuration,
      AlertDefinition definition,
      AlertConfiguration.Severity severity,
      AlertTemplateSettings templateSettings) {
    Map<String, String> definitionLabels =
        getLabels(templateDescription, configuration, templateSettings, definition, severity);

    if (configuration.getTemplate() == AlertTemplate.NODE_DISK_USAGE) {
      UUID universeUuid = UUID.fromString(definition.getLabelValue(KnownAlertLabels.UNIVERSE_UUID));
      Optional<Universe> universe = Universe.maybeGet(universeUuid);
      universe.ifPresent(
          value ->
              definitionLabels.put("mount_points", MetricQueryHelper.getDataMountPoints(value)));
    }
    if (configuration.getTemplate() == AlertTemplate.NODE_SYSTEM_DISK_USAGE) {
      UUID universeUuid = UUID.fromString(definition.getLabelValue(KnownAlertLabels.UNIVERSE_UUID));
      Optional<Universe> universe = Universe.maybeGet(universeUuid);
      universe.ifPresent(
          value ->
              definitionLabels.put(
                  "system_mount_points", MetricQueryHelper.getOtherMountPoints(confGetter, value)));
    }

    AlertConfigurationLabelProvider labelProvider =
        new AlertConfigurationLabelProvider(definition.getUuid(), definitionLabels);
    AlertTemplateSubstitutor<AlertConfigurationLabelProvider> substitutor =
        new AlertTemplateSubstitutor<>(labelProvider);

    labels =
        getLabels(
            templateDescription,
            configuration,
            templateSettings,
            definition,
            severity,
            substitutor);

    String affectedNodeNamesLabel = labels.get(AFFECTED_NODE_NAMES);
    if (StringUtils.isNoneEmpty(affectedNodeNamesLabel)) {
      // We want similar annotations with node addresses and onprem node ids.
      labels.put(
          AFFECTED_NODE_ADDRESSES,
          affectedNodeNamesLabel.replaceAll(
              KnownAlertLabels.NODE_NAME.labelName(), KnownAlertLabels.NODE_ADDRESS.labelName()));
      labels.put(
          AFFECTED_NODE_IDENTIFIERS,
          affectedNodeNamesLabel.replaceAll(
              KnownAlertLabels.NODE_NAME.labelName(),
              KnownAlertLabels.NODE_IDENTIFIER.labelName()));
    }

    // We want to substitute final labels in annotations.
    labelProvider = new AlertConfigurationLabelProvider(definition.getUuid(), labels);
    AlertTemplateSubstitutor<AlertConfigurationLabelProvider> finalSubstitutor =
        new AlertTemplateSubstitutor<>(labelProvider);
    annotations =
        templateDescription.getAnnotations().entrySet().stream()
            .collect(
                Collectors.toMap(
                    Map.Entry::getKey, entry -> finalSubstitutor.replace(entry.getValue())));
    placeholderSubstitutor =
        new PlaceholderSubstitutor(
            key -> {
              switch (key) {
                case DEFINITION_NAME:
                  return configuration.getName();
                case DEFINITION_EXPR:
                  return templateDescription.getQueryWithThreshold(
                      definition, configuration.getThresholds().get(severity));
                case DURATION:
                  return configuration.getDurationSec() + "s";
                case LABELS:
                  return fillKeyValuePlaceholder(labels);
                case ANNOTATIONS:
                  return fillKeyValuePlaceholder(annotations);
                default:
                  throw new IllegalArgumentException(
                      "Unexpected placeholder " + key + " in rule template file");
              }
            });
  }

  @Override
  public String replace(String templateStr) {
    return placeholderSubstitutor.replace(templateStr);
  }

  @RequiredArgsConstructor
  private static class AlertConfigurationLabelProvider implements AlertLabelsProvider {
    private final UUID uuid;
    private final Map<String, String> labels;

    @Override
    public String getLabelValue(String name) {
      return labels.get(name);
    }

    @Override
    public String getAnnotationValue(String name) {
      // We don't need annotations
      return null;
    }

    @Override
    public UUID getUuid() {
      return uuid;
    }
  }

  private static Map<String, String> getLabels(
      AlertTemplateDescription templateDescription,
      AlertConfiguration configuration,
      AlertTemplateSettings templateSettings,
      AlertDefinition definition,
      AlertConfiguration.Severity severity) {
    return definition
        .getEffectiveLabels(templateDescription, configuration, templateSettings, severity)
        .stream()
        .collect(Collectors.toMap(AlertDefinitionLabel::getName, AlertDefinitionLabel::getValue));
  }

  private static Map<String, String> getLabels(
      AlertTemplateDescription templateDescription,
      AlertConfiguration configuration,
      AlertTemplateSettings templateSettings,
      AlertDefinition definition,
      AlertConfiguration.Severity severity,
      AlertTemplateSubstitutor<AlertConfigurationLabelProvider> substitutor) {
    Map<String, String> labels =
        new HashMap<>(
            getLabels(templateDescription, configuration, templateSettings, definition, severity));
    if (MapUtils.isNotEmpty(templateDescription.getLabels())) {
      Map<String, String> templateLabels =
          templateDescription.getLabels().entrySet().stream()
              .collect(
                  Collectors.toMap(
                      Map.Entry::getKey,
                      entry ->
                          AlertTemplateDescription.replaceThresholdAndCondition(
                              entry.getValue(), configuration.getThresholds().get(severity))));
      labels.putAll(
          templateLabels.entrySet().stream()
              .collect(
                  Collectors.toMap(
                      Map.Entry::getKey, entry -> substitutor.replace(entry.getValue()))));
    }
    return labels;
  }

  private static String fillKeyValuePlaceholder(Map<String, String> map) {
    return map.entrySet().stream()
        .map(label -> LABEL_PREFIX + label.getKey() + ": |-\n" + label.getValue())
        .map(annotation -> annotation.replaceAll("\n", "\n" + LABEL_PREFIX + "  "))
        .collect(Collectors.joining("\n"));
  }
}
