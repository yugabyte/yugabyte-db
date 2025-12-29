package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Transient;
import java.util.UUID;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import play.data.validation.Constraints;

@ApiModel(description = "Update PITR config parameters")
@NoArgsConstructor
public class UpdatePitrConfigParams extends UniverseTaskParams {

  @JsonIgnore @Getter @Setter private UUID universeUUID;

  @JsonIgnore public UUID customerUUID;

  @JsonIgnore public UUID pitrConfigUUID;

  @ApiModelProperty(value = "Retention period of a snapshot")
  @Constraints.Required
  public long retentionPeriodInSeconds;

  @ApiModelProperty(value = "Time interval between snapshots")
  @Constraints.Required
  public long intervalInSeconds = 86400L;

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  @Transient
  private KubernetesResourceDetails kubernetesResourceDetails;
}
