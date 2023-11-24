import akka.stream.Materializer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.stream.Collectors;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;

@Singleton
public class CustomHTTPHeader extends Filter {
  private List<String> customHeaders;

  @Inject
  public CustomHTTPHeader(Materializer mat, Config config) {
    super(mat);
    if (config.hasPath("yb.security.headers.custom_headers")) {
      this.customHeaders =
          config.getStringList("yb.security.headers.custom_headers").stream()
              .collect(Collectors.toList());
    }
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    if (customHeaders != null && customHeaders.size() > 0) {
      return next.apply(rh)
          .thenApply(
              result -> {
                Result finalResult = result; // Initialize with the original result
                // Iterate through the list of headers and add them to the response
                for (String header : customHeaders) {
                  String[] parts = header.split(":", 2); // Split each header into key and value
                  if (parts.length == 2) {
                    String key = parts[0].trim();
                    String value = parts[1].trim();
                    finalResult =
                        finalResult.withHeader(key, value); // Add the header to the result
                  }
                }

                return finalResult;
              });
    } else {
      return next.apply(rh);
    }
  }
}
