package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SupportBundleTaskParamsV2 extends AbstractTaskParams {

  public SupportBundleV2 supportBundle;

  public SupportBundleFormDataV2 bundleData;

  public UUID scopeUUID;

  public Customer customer;

  public Universe universe;

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;

  public SupportBundleTaskParamsV2() {}

  public SupportBundleTaskParamsV2(
      SupportBundleV2 supportBundle,
      SupportBundleFormDataV2 bundleData,
      Customer customer,
      Universe universe) {
    this.supportBundle = supportBundle;
    this.bundleData = bundleData;
    this.scopeUUID = supportBundle.getScopeUUID();
    this.customer = customer;
    this.universe = universe;
  }

  /** Params for YBA-only support bundles that do not require a universe. */
  public static SupportBundleTaskParamsV2 forYbaOnly(
      SupportBundleV2 supportBundle, SupportBundleFormDataV2 bundleData, Customer customer) {
    return new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, null);
  }
}
