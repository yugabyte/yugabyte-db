// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.yugabyte.yw.models.AlertChannel.ChannelType;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@Entity
@ApiModel(description = "Alert channel templates")
public class AlertChannelTemplates extends Model {

  private static final Finder<UUID, AlertChannelTemplates> find =
      new Finder<UUID, AlertChannelTemplates>(AlertChannelTemplates.class) {};

  @Id
  @NotNull
  @ApiModelProperty(value = "Channel type", accessMode = READ_WRITE)
  private ChannelType type;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @ApiModelProperty(value = "Notification title template", accessMode = READ_WRITE)
  private String titleTemplate;

  @ApiModelProperty(value = "Notification text template", accessMode = READ_WRITE)
  @NotNull
  private String textTemplate;

  public static ExpressionList<AlertChannelTemplates> createQuery() {
    return find.query().where();
  }
}
