// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.Universe;

public abstract class UniverseRespDecorator implements UniverseRespMapper {
  private final UniverseRespMapper delegate;

  public UniverseRespDecorator(UniverseRespMapper delegate) {
    this.delegate = delegate;
  }

  @Override
  public Universe toV2Universe(com.yugabyte.yw.forms.UniverseResp v1UniverseResp) {
    // The delegate will create a V2 Universe object from the nested UniverseDefinitionTaskParams
    Universe universe = delegate.toV2Universe(v1UniverseResp);
    if (v1UniverseResp == null) {
      return universe;
    }
    // Now fill the V2 Universe object with the top-level properties from the V1 UniverseResp
    universe.getSpec().setName(v1UniverseResp.name);
    delegate.fillV2UniverseInfoFromV1UniverseResp(v1UniverseResp, universe.getInfo());

    return universe;
  }
}
