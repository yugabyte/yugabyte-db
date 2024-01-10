import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import akka.stream.Materializer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.regex.Pattern;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

@Singleton
public class HAApiFilter extends Filter {

  public static final Logger LOG = LoggerFactory.getLogger(HAApiFilter.class);
  private final String HA_ENDPOINT_REGEX = "/api/.*(/?)settings/ha/.*";
  private final Pattern HA_ENDPOINT_PATTERN = Pattern.compile(HA_ENDPOINT_REGEX);

  @Inject
  public HAApiFilter(Materializer mat) {
    super(mat);
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    try {
      if (HighAvailabilityConfig.isFollower()) {
        // Only allow read access for HA follower
        if (!rh.method().equals("GET")) {
          // Also allow any HA, login, or read only specific APIs to succeed
          if (!HA_ENDPOINT_PATTERN.matcher(rh.path()).matches()
              && TokenAuthenticator.READ_POST_ENDPOINTS.stream()
                  .noneMatch(path -> rh.path().endsWith(path))) {
            Result result = Results.status(503, "API not available for follower YBA");
            return CompletableFuture.completedFuture(result);
          }
        }
      }
      return next.apply(rh);
    } catch (Exception e) {
      LOG.error("Error retrieving HA config", e);

      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Error retrieving HA config");
    }
  }
}
