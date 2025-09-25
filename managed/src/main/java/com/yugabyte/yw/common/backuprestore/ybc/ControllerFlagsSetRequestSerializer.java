// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.backuprestore.ybc;

import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.databind.JsonSerializer;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializerProvider;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.io.IOException;
import org.yb.ybc.ControllerFlagsSetRequest;

public class ControllerFlagsSetRequestSerializer extends JsonSerializer<ControllerFlagsSetRequest> {

  @Override
  public void serialize(
      ControllerFlagsSetRequest value, JsonGenerator gen, SerializerProvider serializers)
      throws IOException {
    ObjectMapper mapper = new ObjectMapper();
    ObjectNode flags = mapper.createObjectNode();

    ObjectNode throttleFlags = mapper.createObjectNode();
    throttleFlags.put(
        "max_concurrent_uploads", value.getFlags().getThrottleParams().getMaxConcurrentUploads());
    throttleFlags.put(
        "max_concurrent_downloads",
        value.getFlags().getThrottleParams().getMaxConcurrentDownloads());
    throttleFlags.put(
        "per_upload_num_objects", value.getFlags().getThrottleParams().getPerUploadNumObjects());
    throttleFlags.put(
        "per_download_num_objects",
        value.getFlags().getThrottleParams().getPerDownloadNumObjects());
    flags.set("throttle_flags", throttleFlags);

    ObjectNode diskFlags = mapper.createObjectNode();
    diskFlags.put(
        "disk_read_bytes_per_sec", value.getFlags().getDiskFlags().getDiskReadBytesPerSec());
    diskFlags.put(
        "disk_write_bytes_per_sec", value.getFlags().getDiskFlags().getDiskWriteBytesPerSec());
    flags.set("disk_flags", diskFlags);

    flags.put("reset_default_controller_flags", value.getResetDefaultControllerFlags());

    gen.writeTree(flags);
  }
}
