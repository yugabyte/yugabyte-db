package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.List;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;

public class V253__GenerateImageBundleTest extends FakeDBApplication {

  protected Customer defaultCustomer;

  protected static final String CERT_CONTENTS =
      "-----BEGIN CERTIFICATE-----\n"
          + "MIIDEjCCAfqgAwIBAgIUEdzNoxkMLrZCku6H1jQ4pUgPtpQwDQYJKoZIhvcNAQEL\n"
          + "BQAwLzERMA8GA1UECgwIWXVnYWJ5dGUxGjAYBgNVBAMMEUNBIGZvciBZdWdhYnl0\n"
          + "ZURCMB4XDTIwMTIyMzA3MjU1MVoXDTIxMDEyMjA3MjU1MVowLzERMA8GA1UECgwI\n"
          + "WXVnYWJ5dGUxGjAYBgNVBAMMEUNBIGZvciBZdWdhYnl0ZURCMIIBIjANBgkqhkiG\n"
          + "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuLPcCR1KpVSs3B2515xNAR8ntfhOM5JjLl6Y\n"
          + "WjqoyRQ4wiOg5fGQpvjsearpIntr5t6uMevpzkDMYY4U21KbIW8Vvg/kXiASKMmM\n"
          + "W4ToH3Q0NfgLUNb5zJ8df3J2JZ5CgGSpipL8VPWsuSZvrqL7V77n+ndjMTUBNf57\n"
          + "eW4VjzYq+YQhyloOlXtlfWT6WaAsoaVOlbU0GK4dE2rdeo78p2mS2wQZSBDXovpp\n"
          + "0TX4zhT6LsJaRBZe49GE4SMkxz74alK1ezRvFcrPiNKr5NOYYG9DUUqFHWg47Bmw\n"
          + "KbiZ5dLdyxgWRDbanwl2hOMfExiJhHE7pqgr8XcizCiYuUzlDwIDAQABoyYwJDAO\n"
          + "BgNVHQ8BAf8EBAMCAuQwEgYDVR0TAQH/BAgwBgEB/wIBATANBgkqhkiG9w0BAQsF\n"
          + "AAOCAQEAVI3NTJVNX4XTcVAxXXGumqCbKu9CCLhXxaJb+J8YgmMQ+s9lpmBuC1eB\n"
          + "38YFdSEG9dKXZukdQcvpgf4ryehtvpmk03s/zxNXC5237faQQqejPX5nm3o35E3I\n"
          + "ZQqN3h+mzccPaUvCaIlvYBclUAt4VrVt/W66kLFPsfUqNKVxm3B56VaZuQL1ZTwG\n"
          + "mrIYBoaVT/SmEeIX9PNjlTpprDN/oE25fOkOxwHyI9ydVFkMCpBNRv+NisQN9c+R\n"
          + "/SBXfs+07aqFgrGTte6/I4VZ/6vz2cWMwZU+TUg/u0fc0Y9RzOuJrZBV2qPAtiEP\n"
          + "YvtLjmJF//b3rsty6NFIonSVgq6Nqw==\n"
          + "-----END CERTIFICATE-----\n";

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    when(mockCloudQueryHelper.getDefaultImage(any(Region.class), any()))
        .thenReturn("test_image_id");
  }

  @Test
  public void generateImageBundleAWSProvider() {
    Provider defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    Region region = Region.create(defaultProvider, "us-west-1", "us-west-1", "us-west-1-image");

    V253__GenerateImageBundle.generateImageBundles();
    defaultProvider.refresh();
    List<ImageBundle> defaultImageBundles =
        ImageBundle.getDefaultForProvider(defaultProvider.getUuid());
    ImageBundle defaultImageBundle = defaultImageBundles.get(0);

    assertEquals(1, defaultProvider.getImageBundles().size());
    assertEquals(defaultImageBundle.getUuid(), defaultProvider.getImageBundles().get(0).getUuid());
    assertEquals(
        String.format("for_provider_%s", defaultProvider.getName()),
        defaultProvider.getImageBundles().get(0).getName());
    Map<String, ImageBundleDetails.BundleInfo> regionsBundleInfo =
        defaultProvider.getImageBundles().get(0).getDetails().getRegions();
    assertEquals("us-west-1-image", regionsBundleInfo.get(region.getCode()).getYbImage());
  }

  @Test
  public void generateImageBundleGCPProvider() {
    Provider defaultProvider = ModelFactory.gcpProvider(defaultCustomer);
    Region region = Region.create(defaultProvider, "us-west1", "us-west1", "test_image_id");

    V253__GenerateImageBundle.generateImageBundles();
    defaultProvider.refresh();

    assertEquals(1, defaultProvider.getImageBundles().size());
    assertEquals(
        String.format("for_provider_%s", defaultProvider.getName()),
        defaultProvider.getImageBundles().get(0).getName());
    assertEquals(
        "test_image_id", defaultProvider.getImageBundles().get(0).getDetails().getGlobalYbImage());
  }
}
