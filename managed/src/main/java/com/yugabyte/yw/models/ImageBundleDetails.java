package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.apache.commons.lang3.StringUtils;

@Data
@EqualsAndHashCode(
    callSuper = false,
    exclude = {"regions"})
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ImageBundleDetails {

  @Data
  @JsonInclude(JsonInclude.Include.NON_NULL)
  @EqualsAndHashCode(callSuper = false)
  public static class BundleInfo {
    @ApiModelProperty private String ybImage;

    @ApiModelProperty(
        value =
            "sshUserOverride for the bundle. <b style=\"color:#ff0000\">Deprecated since "
                + "YBA version 2.20.3.0.</b> Use imageBundles.details.sshUser instead.")
    @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.3.0")
    private String sshUserOverride;

    @ApiModelProperty(
        value =
            "sshPortOverride for the bundle. <b style=\"color:#ff0000\">Deprecated since "
                + "YBA version 2.20.3.0.</b> Use imageBundles.details.sshUser instead.")
    @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.3.0")
    private Integer sshPortOverride;
  }

  @ApiModelProperty(value = "Global YB image for the bundle")
  private String globalYbImage;

  @ApiModelProperty(value = "Architecture type for the image bundle")
  private Architecture arch;

  @ApiModelProperty(value = "Regions override for image bundle")
  private Map<String, BundleInfo> regions;

  @ApiModelProperty private String sshUser;
  @ApiModelProperty private Integer sshPort;
  @ApiModelProperty public boolean useIMDSv2 = false;

  public void setSshUser(String sshUser) {
    this.sshUser = sshUser;
    if (StringUtils.isEmpty(this.sshUser) && this.regions != null) {
      this.sshUser =
          this.regions.values().stream()
              .map(BundleInfo::getSshUserOverride)
              .filter(StringUtils::isNotEmpty)
              .findFirst()
              .orElse(null);
      this.regions.values().forEach(bi -> bi.setSshUserOverride(null));
    }
  }

  public void setSshPort(Integer sshPort) {
    this.sshPort = sshPort;
    if (this.sshPort == null && this.regions != null) {
      this.sshPort =
          this.regions.values().stream()
              .map(BundleInfo::getSshPortOverride)
              .findFirst()
              .orElse(null);
      this.regions.values().forEach(bi -> bi.setSshPortOverride(null));
    }
  }
}
