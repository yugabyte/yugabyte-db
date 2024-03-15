import static org.junit.Assert.*;
import static play.mvc.Results.ok;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.common.logging.LogUtil;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import org.apache.pekko.actor.ActorSystem;
import org.apache.pekko.stream.ActorMaterializer;
import org.apache.pekko.stream.ActorMaterializerSettings;
import org.apache.pekko.stream.Materializer;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.MDC;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;

public class RequestLoggingFilterTest {
  private static final String header = "X-REQUEST-ID";

  private ActorSystem actorSystem;
  private Materializer materializer;

  @Before
  public void setup() {
    actorSystem = ActorSystem.create();
    materializer =
        ActorMaterializer.apply(ActorMaterializerSettings.apply(actorSystem), "test", actorSystem);
  }

  @After
  public void teardown() {
    actorSystem.terminate();
  }

  @Test
  public void testForCloud() {
    Config config =
        ConfigFactory.empty()
            .withValue("yb.cloud.enabled", ConfigValueFactory.fromAnyRef(true))
            .withValue("yb.cloud.requestIdHeader", ConfigValueFactory.fromAnyRef(header));
    Filter f = new RequestLoggingFilter(materializer, config);
    String reqId = "reqId";

    Function<Http.RequestHeader, CompletionStage<Result>> next =
        (rh) -> {
          assertEquals(reqId, MDC.get("request-id"));
          return CompletableFuture.completedFuture(ok("ok"));
        };

    Http.RequestHeader rh = new Http.RequestBuilder().build();
    rh.getHeaders().addHeader(header, reqId);
    f.apply(next, rh);
  }

  @Test
  public void testWithCloudDisabled() {
    Config config =
        ConfigFactory.empty()
            .withValue("yb.cloud.enabled", ConfigValueFactory.fromAnyRef(false))
            .withValue("yb.cloud.requestIdHeader", ConfigValueFactory.fromAnyRef(header));
    Filter f = new RequestLoggingFilter(materializer, config);
    Function<Http.RequestHeader, CompletionStage<Result>> next =
        (rh) -> {
          assertNotNull(MDC.get(LogUtil.CORRELATION_ID));
          return CompletableFuture.completedFuture(ok("ok"));
        };

    Http.RequestHeader rh = new Http.RequestBuilder().build();
    f.apply(next, rh);
  }
}
