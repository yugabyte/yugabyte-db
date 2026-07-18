package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SupportBundleTaskParams extends AbstractTaskParams {

  public SupportBundle supportBundle;

  public SupportBundleFormData bundleData;

  public UUID scopeUUID;

  public Customer customer;

  public Universe universe;

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;

  public SupportBundleTaskParams(
      SupportBundle supportBundle,
      SupportBundleFormData bundleData,
      Customer customer,
      Universe universe) {
    this.supportBundle = supportBundle;
    this.bundleData = bundleData;
    this.scopeUUID = supportBundle.getScopeUUID();
    this.customer = customer;
    this.universe = universe;
  }
}
