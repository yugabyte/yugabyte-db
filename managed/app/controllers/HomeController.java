/**
 * Created by ram on 5/31/16.
 */
/**
 * Copyright (c) YugaByte, Inc.
 *
 * Created by ram on 5/31/16.
 */

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
