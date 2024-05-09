package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleasesUtils.ExtractedMetadata;
import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import java.util.HashMap;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@Slf4j
@RunWith(MockitoJUnitRunner.class)
public class ReleasesUtilsTest extends FakeDBApplication {
  @InjectMocks ReleasesUtils releasesUtils;

  @Mock ConfigHelper configHelper;

  @Test
  public void testVersionMetadataFromUrl() {
    URL url = getMockUrl("good_version_metadata.tgz");
    when(configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion))
        .thenReturn(getVersionMap("2024.1.0.0-b23"));
    ExtractedMetadata em = releasesUtils.versionMetadataFromURL(url);
    assertEquals(Architecture.x86_64, em.architecture);
    assertEquals("2.0.0.0-b1", em.minimumYbaVersion);
  }

  @Test
  public void testMinimumYbaVersionFails() {
    URL url = getMockUrl("min_yba_version_fail.tgz");
    when(configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion))
        .thenReturn(getVersionMap("2.1.0.0-b1"));
    assertThrows(PlatformServiceException.class, () -> releasesUtils.versionMetadataFromURL(url));
  }

  @Test
  public void testNoMinimumYbaVersionDefined() {
    URL url = getMockUrl("no_min_yba_metadata.tgz");
    ExtractedMetadata em = releasesUtils.versionMetadataFromURL(url);
    assertEquals(Architecture.aarch64, em.architecture);
    assertEquals("2.21.1.0-b180", em.version);
  }

  private URL getMockUrl(String filename) {
    try {
      final URLConnection mockConnection = mock(URLConnection.class);
      when(mockConnection.getInputStream())
          .thenReturn(this.getClass().getResourceAsStream(filename));

      final URLStreamHandler handler =
          new URLStreamHandler() {
            @Override
            protected URLConnection openConnection(final URL arg0) throws IOException {
              return mockConnection;
            }
          };
      final URL url = new URL("http://foo.bar", "foo.bar", 80, "", handler);
      return url;
    } catch (IOException e) {
      throw new RuntimeException("failed to create a mock url", e);
    }
  }

  private Map<String, Object> getVersionMap(String version) {
    Map<String, Object> map = new HashMap<String, Object>();
    map.put("version", version);
    return map;
  }
}
