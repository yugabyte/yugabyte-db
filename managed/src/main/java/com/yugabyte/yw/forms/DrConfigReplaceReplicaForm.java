package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import javax.validation.Valid;
import lombok.ToString;
import play.data.validation.Constraints.Required;

@ApiModel(description = "drConfig edit form")
@ToString
public class DrConfigReplaceReplicaForm {

  @ApiModelProperty(value = "The current primary universe UUID")
  @Required
  public UUID primaryUniverseUuid;

  @ApiModelProperty(value = "New dr replica universe UUID")
  @Required
  public UUID drReplicaUniverseUuid;

  @Valid
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public XClusterConfigRestartFormData.RestartBootstrapParams bootstrapParams;
}
