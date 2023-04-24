// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashMap;
import java.util.Map;
import lombok.NoArgsConstructor;

@ApiModel(description = "YB-Controller throttle parameters")
@NoArgsConstructor
public class YbcThrottleParameters {

  @ApiModelProperty(value = "Max concurrent uploads per node")
  @JsonAlias("max_concurrent_uploads")
  public int maxConcurrentUploads = 0;

  @ApiModelProperty(value = "Max objects per upload per node")
  @JsonAlias("per_upload_num_objects")
  public int perUploadNumObjects = 0;

  @ApiModelProperty(value = "Max concurrent downloads per node")
  @JsonAlias("max_concurrent_downloads")
  public int maxConcurrentDownloads = 0;

  @ApiModelProperty(value = "Max objects per download per node")
  @JsonAlias("per_download_num_objects")
  public int perDownloadNumObjects = 0;

  @ApiModelProperty(value = "Unset Throttle parameters in YB-Controller")
  public boolean resetDefaults = false;

  @JsonIgnore
  public Map<String, Integer> getThrottleFlagsMap() {
    Map<String, Integer> throttleFlagsMap = new HashMap<>();
    throttleFlagsMap.put(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS, maxConcurrentUploads);
    throttleFlagsMap.put(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS, maxConcurrentDownloads);
    throttleFlagsMap.put(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS, perUploadNumObjects);
    throttleFlagsMap.put(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS, perDownloadNumObjects);
    return throttleFlagsMap;
  }
}
