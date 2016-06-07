// Copyright (c) Yugabyte, Inc.

package controllers;

import play.mvc.Controller;
import play.mvc.With;
import security.TokenAuthenticator;

@With(TokenAuthenticator.class)
public abstract class AuthenticatedController extends Controller {}
