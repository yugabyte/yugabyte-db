import com.yugabyte.operator.OperatorConfig;
import java.util.Collections;
import java.util.List;
import javax.inject.Inject;
import org.apache.pekko.stream.Materializer;
import play.libs.streams.Accumulator;
import play.mvc.EssentialAction;
import play.mvc.EssentialFilter;
import play.mvc.Results;

public class BlockAllRequestsFilter extends EssentialFilter {
  public static final boolean ENABLED = OperatorConfig.getOssMode();
  private static final List<String> ALLOWED_PATHS = Collections.singletonList("/api/register");

  private final Materializer mat;

  @Inject
  public BlockAllRequestsFilter(Materializer mat) {
    this.mat = mat;
  }

  @Override
  public EssentialAction apply(EssentialAction next) {
    return EssentialAction.of(
        request -> {
          if (ENABLED && isPathBlocked(request.path())) {
            return Accumulator.done(Results.forbidden("Access denied"));
          } else {
            return next.apply(request);
          }
        });
  }

  private boolean isPathBlocked(String path) {
    for (String allowedPath : ALLOWED_PATHS) {
      if (path.endsWith(allowedPath)) {
        return false;
      }
    }
    return true;
  }
}
