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

import static play.mvc.Http.Status.BAD_REQUEST;

import com.cronutils.utils.StringUtils;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.forms.AlertTemplateSystemVariable;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertChannelTemplates;
import com.yugabyte.yw.models.AlertTemplateVariable;
import io.ebean.annotation.Transactional;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.Environment;

@Singleton
@Slf4j
public class AlertChannelTemplateService {

  private static final Set<String> JSON_OBJECT_VARIABLE_PATTERNS =
      Arrays.stream(AlertTemplateSystemVariable.values())
          .filter(AlertTemplateSystemVariable::isJsonObject)
          .map(variable -> "\\{\\{\\s*" + variable.getName() + "\\s*\\}\\}")
          .collect(Collectors.toSet());

  private final BeanValidator beanValidator;

  private final Environment environment;

  private final Map<ChannelType, String> defaultTitleTemplates;
  private final Map<ChannelType, String> defaultTextTemplates;

  @Inject
  public AlertChannelTemplateService(BeanValidator beanValidator, Environment environment) {
    this.beanValidator = beanValidator;
    this.environment = environment;
    defaultTitleTemplates = readTemplates("title");
    defaultTextTemplates = readTemplates("text");
  }

  public AlertChannelTemplates get(UUID customerUUID, ChannelType channelType) {
    return AlertChannelTemplates.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("type", channelType)
        .findOne();
  }

  public AlertChannelTemplates getOrBadRequest(UUID customerUUID, ChannelType channelType) {
    AlertChannelTemplates templates = get(customerUUID, channelType);
    if (templates == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No templates defined for channel type " + channelType.name());
    }
    return templates;
  }

  public List<AlertChannelTemplates> list(UUID customerUUID) {
    return AlertChannelTemplates.createQuery().eq("customerUUID", customerUUID).findList();
  }

  public AlertChannelTemplatesExt getWithDefaults(UUID customerUUID, ChannelType channelType) {
    AlertChannelTemplates templates = get(customerUUID, channelType);
    return appendDefaults(templates, customerUUID, channelType);
  }

  public List<AlertChannelTemplatesExt> listWithDefaults(UUID customerUUID) {
    Map<ChannelType, AlertChannelTemplates> templates =
        list(customerUUID).stream()
            .collect(Collectors.toMap(AlertChannelTemplates::getType, Function.identity()));
    return Arrays.stream(ChannelType.values())
        .map(type -> appendDefaults(templates.get(type), customerUUID, type))
        .collect(Collectors.toList());
  }

  @Transactional
  public AlertChannelTemplates save(AlertChannelTemplates templates) {
    validate(templates);

    AlertChannelTemplates before = get(templates.getCustomerUUID(), templates.getType());
    if (before == null) {
      templates.save();
    } else {
      templates.update();
    }
    return templates;
  }

  @Transactional
  public void delete(UUID customerUUID, ChannelType channelType) {
    AlertChannelTemplates templates = getOrBadRequest(customerUUID, channelType);

    templates.delete();
    log.info(
        "Deleted template for alert channel type {}, customer {}",
        channelType.name(),
        customerUUID);
  }

  public void validate(AlertChannelTemplates templates) {
    beanValidator.validate(templates);

    AlertChannelParams.validateTemplateString(
        beanValidator, "titleTemplate", templates.getTitleTemplate());
    AlertChannelParams.validateTemplateString(
        beanValidator, "textTemplate", templates.getTextTemplate());

    Set<String> variables =
        AlertTemplateVariable.list(templates.getCustomerUUID()).stream()
            .map(AlertTemplateVariable::getName)
            .collect(Collectors.toSet());

    if (!templates.getType().isHasTitle() && templates.getTitleTemplate() != null) {
      beanValidator
          .error()
          .forField(
              "titleTemplate",
              "title template not supported for channel type " + templates.getType().name())
          .throwError();
    }

    templates
        .getCustomVariablesSet()
        .forEach(
            templateVariable -> {
              if (!variables.contains(templateVariable)) {
                beanValidator
                    .error()
                    .forField("", "variable '" + templateVariable + "' does not exist")
                    .throwError();
              }
            });
    if (templates.getType() == ChannelType.WebHook) {
      if (!StringUtils.isEmpty(templates.getTextTemplate())) {
        String jsonizedTemplate = templates.getTextTemplate();
        for (String jsonVariablePattern : JSON_OBJECT_VARIABLE_PATTERNS) {
          jsonizedTemplate = jsonizedTemplate.replaceAll(jsonVariablePattern, "{}");
        }
        beanValidator.validateValidJson(
            "textTemplate", jsonizedTemplate, "Only JSON body is supported for WebHook template");
      }
    }
  }

  public AlertChannelTemplatesExt appendDefaults(
      AlertChannelTemplates templates, UUID customerUUID, ChannelType channelType) {
    if (templates == null) {
      templates = new AlertChannelTemplates().setCustomerUUID(customerUUID).setType(channelType);
    }
    return new AlertChannelTemplatesExt()
        .setChannelTemplates(templates)
        .setDefaultTitleTemplate(defaultTitleTemplates.get(channelType))
        .setDefaultTextTemplate(defaultTextTemplates.get(channelType));
  }

  private Map<ChannelType, String> readTemplates(String templateType) {
    Map<ChannelType, String> result = new HashMap<>();
    for (ChannelType channelType : ChannelType.values()) {
      if (templateType.equals("title") && !channelType.isHasTitle()) {
        continue;
      }
      String resourceName = "alert/" + channelType.name() + "_" + templateType + ".template";
      String template = FileUtils.readResource(resourceName, environment);
      result.put(channelType, template);
    }
    return result;
  }
}
