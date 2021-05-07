// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import play.mvc.Controller;
import play.mvc.With;

@With(TokenAuthenticator.class)
public abstract class AuthenticatedController extends AbstractPlatformController {}
