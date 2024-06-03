// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.forms.AlertTemplateSystemVariable;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.Arrays;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.apache.commons.lang3.StringUtils;

@Data
@EqualsAndHashCode
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, property = "channelType")
@JsonSubTypes({
  @JsonSubTypes.Type(value = AlertChannelEmailParams.class, name = "Email"),
  @JsonSubTypes.Type(value = AlertChannelSlackParams.class, name = "Slack"),
  @JsonSubTypes.Type(value = AlertChannelPagerDutyParams.class, name = "PagerDuty"),
  @JsonSubTypes.Type(value = AlertChannelWebHookParams.class, name = "WebHook")
})
@ApiModel(
    subTypes = {
      AlertChannelEmailParams.class,
      AlertChannelSlackParams.class,
      AlertChannelPagerDutyParams.class,
      AlertChannelWebHookParams.class
    },
    discriminator = "channelType",
    description = "Supertype for channel params for different channel types.")
public class AlertChannelParams {

  public static final Pattern PLACEHOLDER_PATTERN = Pattern.compile("\\{\\{([^{}]*)\\}\\}");
  public static final Pattern PLACEHOLDER_VALUE_PATTERN = Pattern.compile("^\\w+$");
  public static final String SYSTEM_VARIABLE_PREFIX = "yugabyte_";

  public static final Set<String> SYSTEM_VARIABLE_NAMES =
      Arrays.stream(AlertTemplateSystemVariable.values())
          .map(AlertTemplateSystemVariable::getName)
          .collect(Collectors.toSet());

  @ApiModelProperty(value = "Channel type", accessMode = AccessMode.READ_WRITE)
  private ChannelType channelType;

  // Specifies template string for the notification title.
  @ApiModelProperty(value = "Notification title template", accessMode = READ_WRITE)
  private String titleTemplate;

  // Specifies template string for the notification text/body/description.
  @ApiModelProperty(value = "Notification text template", accessMode = READ_WRITE)
  private String textTemplate;

  public void validate(BeanValidator validator) {
    validateTemplate(validator, titleTemplate, "titleTemplate");
    validateTemplate(validator, textTemplate, "textTemplate");
  }

  private static void validateTemplate(BeanValidator validator, String template, String fieldName) {
    if (StringUtils.isEmpty(template)) {
      return;
    }
    validateTemplateString(validator, template, fieldName);
  }

  public static void validateTemplateString(
      BeanValidator validator, String fieldName, String template) {
    if (StringUtils.isEmpty(template)) {
      return;
    }
    Matcher placeholderMatcher = PLACEHOLDER_PATTERN.matcher(template);
    while (placeholderMatcher.find()) {
      String placeholderValue = placeholderMatcher.group(1).trim();
      Matcher valueMatcher = PLACEHOLDER_VALUE_PATTERN.matcher(placeholderValue);
      if (!valueMatcher.matches()) {
        validator
            .error()
            .forField(
                fieldName,
                "Template placeholder params should contain only alphanumeric"
                    + " characters and underscores (_)")
            .throwError();
      } else if (placeholderValue.startsWith(SYSTEM_VARIABLE_PREFIX)) {
        if (!SYSTEM_VARIABLE_NAMES.contains(placeholderValue)) {
          validator
              .error()
              .forField(fieldName, "Only system variables may have 'yugabyte_' prefix");
        }
      }
    }
  }
}
