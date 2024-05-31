// Copyright (c) YugaByte, Inc.

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static play.mvc.Results.ok;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigValueFactory;
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

public class RequestHeaderFilterTest {
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
  public void testEnabled() {
    String header = "subtask-abort-position";
    Config config =
        ConfigFactory.empty()
            .withValue(
                "yb.internal.headers." + header + ".enabled", ConfigValueFactory.fromAnyRef(true));
    Filter f = new RequestHeaderFilter(materializer, config);
    String value = "v1";

    Function<Http.RequestHeader, CompletionStage<Result>> next =
        (rh) -> {
          assertEquals(value, MDC.get(header));
          return CompletableFuture.completedFuture(ok("ok"));
        };

    Http.RequestHeader rh = new Http.RequestBuilder().build();
    rh.getHeaders().addHeader("subtask-abort-position", value);
    f.apply(next, rh);
  }

  @Test
  public void testDisabled() {
    String header = "subtask-abort-position";
    Config config =
        ConfigFactory.empty()
            .withValue(
                "yb.internal.headers." + header + ".enabled", ConfigValueFactory.fromAnyRef(false));
    Filter f = new RequestHeaderFilter(materializer, config);

    Function<Http.RequestHeader, CompletionStage<Result>> next =
        (rh) -> {
          assertNull(MDC.get(header));
          return CompletableFuture.completedFuture(ok("ok"));
        };

    Http.RequestHeader rh = new Http.RequestBuilder().build();
    rh.getHeaders().addHeader(header, "v1");
    f.apply(next, rh);
  }

  @Test
  public void testNotPresent() {
    String header = "subtask-abort-position";
    Config config = ConfigFactory.empty();
    Filter f = new RequestHeaderFilter(materializer, config);

    Function<Http.RequestHeader, CompletionStage<Result>> next =
        (rh) -> {
          assertNull(MDC.get(header));
          return CompletableFuture.completedFuture(ok("ok"));
        };

    Http.RequestHeader rh = new Http.RequestBuilder().build();
    rh.getHeaders().addHeader(header, "v1");
    f.apply(next, rh);
  }
}
