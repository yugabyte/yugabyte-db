package com.yugabyte.yw.controllers;

import static play.mvc.Results.forbidden;
import static play.mvc.Results.unauthorized;

import org.pac4j.core.context.HttpConstants;
import org.pac4j.play.PlayWebContext;
import org.pac4j.play.http.DefaultHttpActionAdapter;
import play.mvc.Result;

// TODO(sbapat): investigate why this class is needed .. looks redundant as
//  base class provides all the functionality
public class PlatformHttpActionAdapter extends DefaultHttpActionAdapter {

  @Override
  public Result adapt(int code, PlayWebContext context) {
    if (code == HttpConstants.UNAUTHORIZED) {
      return unauthorized();
    } else if (code == HttpConstants.FORBIDDEN) {
      return forbidden();
    } else {
      return super.adapt(code, context);
    }
  }
}
