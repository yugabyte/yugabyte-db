// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.controllers;

import play.mvc.Controller;
import play.mvc.With;

@With(TokenAuthenticator.class)
public abstract class AuthenticatedController extends Controller {}
