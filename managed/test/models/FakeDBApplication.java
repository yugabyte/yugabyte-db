// Copyright (c) Yugabyte, Inc.

package models;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;
import play.test.WithApplication;

import java.util.Map;

public class FakeDBApplication extends WithApplication {

	@Override
	protected Application provideApplication() {
		return new GuiceApplicationBuilder()
				.configure((Map) Helpers.inMemoryDatabase())
				.build();
	}
}
