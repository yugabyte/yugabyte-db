/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers

import javax.inject.{Inject, Provider, Singleton}
import play.api.http.HttpConfiguration
import play.api.routing.Router.Routes
import play.api.routing.sird.{GET, _}
import play.api.routing.{Router, SimpleRouter}

private class SwaggerGenRouter @Inject() (controller: controllers.ApiHelpController) extends SimpleRouter {
  override def routes: Routes = {
    case GET(p"/dynamic_swagger.json") => controller.getResources
    case GET(p"/docs/dynamic_swagger.json") => controller.getResources
  }
}

class AppRouter @Inject()(swaggerGenRouter1: SwaggerGenRouter, prodRouter: router.Routes) extends SimpleRouter {
  // Composes both routers with spaRouter having precedence.
  override def routes: Routes = swaggerGenRouter1.routes.orElse(prodRouter.routes)
}

@Singleton
class SwaggerGenRoutesProvider @Inject()(val swaggerGenRouter: AppRouter, val httpConfig: HttpConfiguration) extends Provider[Router] {
  override def get: Router = swaggerGenRouter.withPrefix (httpConfig.context)
}
