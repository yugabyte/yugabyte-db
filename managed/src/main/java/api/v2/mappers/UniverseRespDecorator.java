// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.Universe;
import java.text.ParseException;

public abstract class UniverseRespDecorator implements UniverseRespMapper {
  private final UniverseRespMapper delegate;

  public UniverseRespDecorator(UniverseRespMapper delegate) {
    this.delegate = delegate;
  }

  @Override
  public Universe toV2UniverseResp(com.yugabyte.yw.forms.UniverseResp v1UniverseResp) {
    Universe universeResp = delegate.toV2UniverseResp(v1UniverseResp);
    if (v1UniverseResp == null) {
      return universeResp;
    }

    universeResp.getSpec().setName(v1UniverseResp.name);
    universeResp.getInfo().setVersion(v1UniverseResp.version);
    try {
      universeResp.getInfo().setCreationDate(parseToOffsetDateTime(v1UniverseResp.creationDate));
    } catch (ParseException e) {
      throw new RuntimeException(e);
    }
    universeResp.getInfo().setDnsName(v1UniverseResp.dnsName);

    return universeResp;
  }
}
