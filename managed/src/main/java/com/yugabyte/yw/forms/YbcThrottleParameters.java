// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.common.YbaApi;
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
  public long maxConcurrentUploads = 0;

  @ApiModelProperty(value = "Max objects per upload per node")
  @JsonAlias("per_upload_num_objects")
  public long perUploadNumObjects = 0;

  @ApiModelProperty(value = "Max concurrent downloads per node")
  @JsonAlias("max_concurrent_downloads")
  public long maxConcurrentDownloads = 0;

  @ApiModelProperty(value = "Max objects per download per node")
  @JsonAlias("per_download_num_objects")
  public long perDownloadNumObjects = 0;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Disk read bytes per second to throttle"
              + " disk usage during backups")
  @JsonAlias("disk_read_bytes_per_sec")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2025.2.0.0")
  public long diskReadBytesPerSecond = -1;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Disk write bytes per second to"
              + " throttle disk usage during restore")
  @JsonAlias("disk_write_bytes_per_sec")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2025.2.0.0")
  public long diskWriteBytesPerSecond = -1;

  @ApiModelProperty(value = "Unset Throttle parameters in YB-Controller")
  public boolean resetDefaults = false;

  @JsonIgnore
  public Map<String, Long> getThrottleFlagsMap() {
    Map<String, Long> throttleFlagsMap = new HashMap<>();
    throttleFlagsMap.put(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS, maxConcurrentUploads);
    throttleFlagsMap.put(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS, maxConcurrentDownloads);
    throttleFlagsMap.put(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS, perUploadNumObjects);
    throttleFlagsMap.put(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS, perDownloadNumObjects);
    throttleFlagsMap.put(GFlagsUtil.YBC_DISK_READ_BYTES_PER_SECOND, diskReadBytesPerSecond);
    throttleFlagsMap.put(GFlagsUtil.YBC_DISK_WRITE_BYTES_PER_SECOND, diskWriteBytesPerSecond);
    return throttleFlagsMap;
  }
}
