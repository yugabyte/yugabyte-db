package com.yugabyte.yw.controllers;

import static play.mvc.Results.forbidden;
import static play.mvc.Results.unauthorized;

import org.pac4j.core.context.HttpConstants;
import org.pac4j.core.exception.http.HttpAction;
import org.pac4j.play.PlayWebContext;
import org.pac4j.play.http.PlayHttpActionAdapter;
import play.mvc.Result;

// TODO(sbapat): investigate why this class is needed .. looks redundant as
//  base class provides all the functionality
public class PlatformHttpActionAdapter extends PlayHttpActionAdapter {

  @Override
  public Result adapt(final HttpAction action, PlayWebContext context) {
    if (action.getCode() == HttpConstants.UNAUTHORIZED) {
      return unauthorized();
    } else if (action.getCode() == HttpConstants.FORBIDDEN) {
      return forbidden();
    } else {
      return super.adapt(action, context);
    }
  }
}
