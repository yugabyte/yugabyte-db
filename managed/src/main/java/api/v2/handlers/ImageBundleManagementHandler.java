// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static api.v2.handlers.HandlerPagingSupport.normalize;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.ImageBundleMapper;
import api.v2.models.ImageBundlePagedQuerySpec;
import api.v2.models.ImageBundlePagedResp;
import api.v2.utils.NormalizedPaginationSpec;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Provider;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;

public class ImageBundleManagementHandler {

  public ImageBundlePagedResp pageListImageBundles(
      UUID cUUID, UUID providerUUID, ImageBundlePagedQuerySpec spec) {
    NormalizedPaginationSpec normalized = normalize(spec);
    if (Provider.get(cUUID, providerUUID) == null) {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("Could not find provider %s", providerUUID));
    }

    String arch =
        spec.getFilter() != null ? StringUtils.trimToNull(spec.getFilter().getArch()) : null;
    if (arch != null) {
      try {
        Architecture.valueOf(arch);
      } catch (IllegalArgumentException e) {
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("Specify a valid arch type: %s", arch));
      }
    }

    return HandlerPagingSupport.pagedResponse(
        new ImageBundlePagedResp(),
        ImageBundle.getPagedList(providerUUID, arch, normalized),
        ImageBundleMapper.INSTANCE::toApi);
  }
}
