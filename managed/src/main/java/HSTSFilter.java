import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import org.apache.pekko.stream.Materializer;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

@Singleton
public class HSTSFilter extends Filter {
  private final long maxAge;
  private boolean hstsEnabled = false;
  private final String hstsHeaderKey = "Strict-Transport-Security";
  private final String hstsHeaderValue = "max-age=%s; includeSubDomains";

  @Inject
  public HSTSFilter(Materializer mat, Config config) {
    super(mat);
    this.maxAge = TimeUnit.DAYS.toSeconds(365); // Max age of one year
    if (config.hasPath("yb.security.headers.hsts_enabled")
        && config.getBoolean("yb.security.headers.hsts_enabled")) {
      this.hstsEnabled = true;
    }
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    if (hstsEnabled) {
      if (!rh.secure() && rh.method().equals("GET")) {
        // In case we start running http:// server as well along with https://
        // For HTTP GET requests, return a 301 status code to redirect to HTTPS
        Result result = Results.redirect("https://" + rh.host() + rh.uri());
        return CompletableFuture.completedFuture(result);
      } else {
        // For other requests (POST, etc.), set the HSTS header
        return next.apply(rh)
            .thenApply(
                result -> {
                  // Add the HSTS header to the result
                  return result.withHeader(hstsHeaderKey, String.format(hstsHeaderValue, maxAge));
                });
      }
    } else {
      return next.apply(rh);
    }
  }
}
