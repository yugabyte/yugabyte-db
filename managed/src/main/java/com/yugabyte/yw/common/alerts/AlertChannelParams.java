// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.yugabyte.yw.common.BeanValidator;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.apache.commons.lang3.StringUtils;

@Data
@EqualsAndHashCode
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, include = As.PROPERTY, property = "channelType")
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

  private static final Pattern PLACEHOLDER_PATTERN = Pattern.compile("\\{\\{([^{}]*)\\}\\}");

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

    Matcher m = PLACEHOLDER_PATTERN.matcher(template);
    while (m.find()) {
      String placeholderValue = m.group(1).trim();
      if (!placeholderValue.startsWith(AlertTemplateSubstitutor.LABELS_PREFIX)
          && !placeholderValue.startsWith(AlertTemplateSubstitutor.ANNOTATIONS_PREFIX)) {
        validator
            .error()
            .forField(
                fieldName,
                "Template placeholder params should start with '$labels.'"
                    + " or '$annotations.' prefix.")
            .throwError();
      }
    }
  }
}
