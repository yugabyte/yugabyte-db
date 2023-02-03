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

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertChannelTemplates;
import io.ebean.annotation.Transactional;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class AlertChannelTemplateService {

  private final BeanValidator beanValidator;

  @Inject
  public AlertChannelTemplateService(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
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

  private void validate(AlertChannelTemplates templates) {
    beanValidator.validate(templates);
  }
}
