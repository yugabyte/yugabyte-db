// Copyright (c) YugabyteDB, Inc.

package controllers;

import javax.inject.Inject;
import play.api.mvc.AnyContent;
import play.mvc.Controller;
import play.mvc.Result;

public class UIController extends Controller {
  @Inject Assets assets;

  public play.api.mvc.Action<AnyContent> index() {
    return assets.at("/public", "index.html", false);
  }

  public play.api.mvc.Action<AnyContent> assetOrDefault(String resource) {
    if (resource.startsWith("static") || resource.contains(".css") || resource.contains(".ico")) {
      // Route any static files through the assets path.
      return assets.at("/public", resource, false);
    } else {
      return index();
    }
  }

  // UI Controller wouldn't serve the API calls.
  public Result unknown(String resource) {
    return notFound(String.format("%s not found", resource));
  }
}
