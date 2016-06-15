// Copyright (c) YugaByte, Inc.

import com.avaje.ebean.Ebean;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import models.cloud.Provider;
import play.Application;
import play.Environment;
import play.Logger;
import play.libs.Yaml;

import java.util.ArrayList;
import java.util.List;


/**
 * We will use this singleton to do actions specific to the app environment, like
 * db seed etc.
 */
@Singleton
public class AppInit {

	@Inject
	public AppInit(Environment environment, Application application) {
		Logger.info("Yugaware Application has started");
		if (!environment.isTest()) {
			// Check if we have provider data, if not, we need to see the database
			if (Provider.find.where().findRowCount() == 0) {
				Logger.debug("Seed the Yugaware DB");
				List<?> all = (ArrayList<?>) Yaml.load(
						application.resourceAsStream("db_seed.yml"),
						application.classloader()
				);
				Ebean.saveAll(all);
			}
		}
	}
}
