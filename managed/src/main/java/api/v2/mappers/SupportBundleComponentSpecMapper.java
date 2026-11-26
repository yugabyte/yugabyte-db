// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import com.yugabyte.yw.models.helpers.BundleDetails;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

/**
 * Maps the per-component collection specs between {@link BundleDetails} and the generated v2 API
 * models. Both sides use identical property names, so no overrides are needed.
 */
@Mapper(config = CentralConfig.class)
public interface SupportBundleComponentSpecMapper {

  SupportBundleComponentSpecMapper INSTANCE =
      Mappers.getMapper(SupportBundleComponentSpecMapper.class);

  api.v2.models.FilesComponentSpec toApi(BundleDetails.FilesComponentSpec source);

  BundleDetails.FilesComponentSpec toV1(api.v2.models.FilesComponentSpec source);

  api.v2.models.BashComponentSpec toApi(BundleDetails.BashComponentSpec source);

  BundleDetails.BashComponentSpec toV1(api.v2.models.BashComponentSpec source);

  api.v2.models.YSQLComponentSpec toApi(BundleDetails.YSQLComponentSpec source);

  BundleDetails.YSQLComponentSpec toV1(api.v2.models.YSQLComponentSpec source);

  api.v2.models.YCQLComponentSpec toApi(BundleDetails.YCQLComponentSpec source);

  BundleDetails.YCQLComponentSpec toV1(api.v2.models.YCQLComponentSpec source);

  api.v2.models.YbAdminComponentSpec toApi(BundleDetails.YbAdminComponentSpec source);

  BundleDetails.YbAdminComponentSpec toV1(api.v2.models.YbAdminComponentSpec source);

  api.v2.models.YbaComponentSpec toApi(BundleDetails.YbaComponentSpec source);

  BundleDetails.YbaComponentSpec toV1(api.v2.models.YbaComponentSpec source);
}
