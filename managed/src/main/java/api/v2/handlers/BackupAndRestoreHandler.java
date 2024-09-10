// Copyright (c) YugaByte, Inc.

package api.v2.handlers;

import api.v2.mappers.GflagsMetadataMapper;
import api.v2.models.GflagMetadata;
import com.google.inject.Singleton;
import com.yugabyte.yw.forms.ybc.YbcGflags;
import java.util.List;
import play.mvc.Http;

@Singleton
public class BackupAndRestoreHandler {

  public List<GflagMetadata> listYbcGflagsMetadata(Http.Request request) {
    return GflagsMetadataMapper.INSTANCE.toGflagMetadataList(YbcGflags.ybcGflagsMetadata.values());
  }
}
