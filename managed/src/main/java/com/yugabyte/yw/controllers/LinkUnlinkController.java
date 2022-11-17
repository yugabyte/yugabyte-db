// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.UniverseSpec;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.UUID;
import play.api.libs.Files.TemporaryFile;
import play.mvc.Http;
import play.mvc.Result;

public class LinkUnlinkController extends AbstractPlatformController {

  @Inject private Config config;

  @Inject private ConfigHelper configHelper;

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  public Result exportUniverse(UUID customerUUID, UUID universeUUID) throws IOException {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    List<InstanceType> instanceTypes =
        InstanceType.findByProvider(
            provider,
            config,
            configHelper,
            runtimeConfigFactory
                .forProvider(provider)
                .getBoolean("yb.internal.allow_unsupported_instances"));

    List<AccessKey> accessKeys = AccessKey.getAll(provider.uuid);

    List<PriceComponent> priceComponents = PriceComponent.findByProvider(provider);

    String storagePath = runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");

    UniverseSpec universeSpec =
        UniverseSpec.builder()
            .universe(universe)
            .universeConfig(universe.getConfig())
            .provider(provider)
            .instanceTypes(instanceTypes)
            .accessKeys(accessKeys)
            .priceComponents(priceComponents)
            .oldStoragePath(storagePath)
            .build();

    InputStream is = universeSpec.exportSpec();

    response().setHeader("Content-Disposition", "attachment; filename=universeSpec.tar.gz");
    return ok(is).as("application/gzip");
  }

  public Result importUniverse(UUID customerUUID, UUID universeUUID) throws IOException {
    Http.MultipartFormData<TemporaryFile> body = request().body().asMultipartFormData();
    Http.MultipartFormData.FilePart<TemporaryFile> tempSpecFile = body.getFile("spec");

    if (tempSpecFile == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Failed to get uploaded spec file");
    }

    String storagePath = runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    File tempFile = (File) tempSpecFile.getFile();
    UniverseSpec universeSpec = UniverseSpec.importSpec(tempFile, storagePath);
    universeSpec.save(storagePath);
    return PlatformResults.withData(universeSpec);
  }
}
