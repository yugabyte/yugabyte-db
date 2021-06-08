package com.yugabyte.yw.common.swagger;

import io.swagger.annotations.*;
import io.swagger.core.filter.AbstractSpecFilter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@SwaggerDefinition(
    info =
        @Info(
            title = "Yugabyte Platform API",
            description = "Yugabyte Platform API",
            version = "v1",
            contact = @Contact(name = "Yugabyte", url = "http://docs.yugabyte.com"),
            license =
                @License(
                    name = YWSwaggerSpecFilter.LICENSE_1_0_0_NAME,
                    url = YWSwaggerSpecFilter.POLYFORM_FREE_TRIAL_LICENSE_1_0_0_URL)),
    consumes = {"application/json"},
    produces = {"application/json"},
    schemes = {SwaggerDefinition.Scheme.HTTP, SwaggerDefinition.Scheme.HTTPS},
    externalDocs =
        @ExternalDocs(value = "About Yugabyte Platform", url = "https://docs.yugabyte.com"))
public class YWSwaggerSpecFilter extends AbstractSpecFilter {
  public static final Logger LOG = LoggerFactory.getLogger(YWSwaggerSpecFilter.class);

  static final String LICENSE_1_0_0_NAME = "Polyform Free Trial License 1.0.0";
  static final String POLYFORM_FREE_TRIAL_LICENSE_1_0_0_URL =
      "https://github.com/yugabyte/yugabyte-db/blob/master/licenses/"
          + "POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt";

  public YWSwaggerSpecFilter() {
    YWModelConverter.register();
  }
}
