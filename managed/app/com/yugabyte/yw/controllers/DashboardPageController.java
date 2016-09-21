// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.views.html.*;
import play.mvc.Controller;
import play.mvc.Result;

public class DashboardPageController extends Controller {
  public Result index() {
    return ok(dashboard.render());
  }

}
