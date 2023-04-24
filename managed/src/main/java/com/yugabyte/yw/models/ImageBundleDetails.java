package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ImageBundleDetails {

  @Data
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class BundleInfo {
    @ApiModelProperty private String ybImage;
    @ApiModelProperty private String sshUserOverride;
    @ApiModelProperty private Integer sshPortOverride;
  }

  @ApiModelProperty(value = "Global YB image for the bundle")
  private String globalYbImage;

  @ApiModelProperty(value = "Architecture type for the image bundle")
  private Architecture arch;

  @ApiModelProperty(value = "Regions override for image bundle")
  private Map<String, BundleInfo> regions;
}
