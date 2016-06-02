import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Mode;
import play.Application;
import play.api.mvc.RequestHeader;
import play.inject.guice.GuiceApplicationBuilder;
import play.mvc.Http;
import play.test.WithApplication;
import services.YBClientService;
import static org.mockito.Mockito.*;
import static play.inject.Bindings.bind;

import java.util.Collections;

/**
 * Created by ram on 6/1/16.
 */
public  abstract class ApplicationTestBase extends WithApplication {
    private static final Logger LOGGER = LoggerFactory.getLogger(ApplicationTestBase.class);

    @Override
    protected Application provideApplication()
    {
        return new GuiceApplicationBuilder()
                .overrides(bind(YBClientService.class).to(MockYBClientService.class))
                .in(Mode.TEST)
                .build();
    }

    @Override
    public void startPlay()
    {
        super.startPlay();
        // mock or otherwise provide a context
        Http.Context.current.set(new Http.Context(1L,
                mock(RequestHeader.class),
                mock(Http.Request.class),
                Collections.<String, String>emptyMap(),
                Collections.<String, String>emptyMap(),
                Collections.<String, Object>emptyMap()));
    }

    public Http.Context context()
    {
        return Http.Context.current.get();
    }
}
