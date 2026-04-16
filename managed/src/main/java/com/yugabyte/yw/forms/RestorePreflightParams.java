package com.yugabyte.yw.forms;

import com.yugabyte.yw.forms.backuprestore.RestoreItemsValidationParams;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import play.data.validation.Constraints;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(value = "Parameters for Restore preflight checks")
public class RestorePreflightParams extends RestoreItemsValidationParams {
  @ApiModelProperty(value = "Target universe UUID", required = true)
  @Constraints.Required
  private UUID universeUUID;

  @Override
  public void validateParams(UUID customerUUID) {
    super.validateParams(customerUUID);
    Universe.getOrBadRequest(universeUUID);
  }
}
