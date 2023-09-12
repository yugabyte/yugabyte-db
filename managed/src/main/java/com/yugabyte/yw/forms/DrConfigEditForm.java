package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import javax.validation.Valid;
import lombok.ToString;

@ApiModel(description = "drConfig edit form")
@ToString
public class DrConfigEditForm {

  @ApiModelProperty(value = "New Target Universe UUID")
  public UUID newTargetUniverseUUID;

  @Valid
  @ApiModelProperty("Parameters used to do Backup/restore")
  public XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams bootstrapBackupParams;
}
