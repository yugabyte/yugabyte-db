// Copyright (c) Yugabyte, Inc.

package controllers;

import play.mvc.*;
import views.html.*;

public class HomeController extends Controller {
    /**
     * Renders the Index Page
     *
     * @return
     */
    public Result index() {
        return ok(index.render("Yugabyte Middleware APIs."));
    }

}
