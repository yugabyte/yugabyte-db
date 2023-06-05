package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.util.HashMap;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;

public class V231__ProviderDetailsPersistTest extends FakeDBApplication {

  private Provider gcpProvider;
  private Provider awsProvider;
  private Provider azureProvider;

  @Before
  public void setup() {
    Customer defaultCustomer = ModelFactory.testCustomer();

    Map<String, String> awsConfig =
        new HashMap<String, String>() {
          {
            put("AWS_ACCESS_KEY_ID", "awsAccessKeyID");
            put("AWS_SECRET_ACCESS_KEY", "awsAccessKeySecret");
          }
        };
    awsProvider = ModelFactory.awsProvider(defaultCustomer);
    awsProvider.setConfigMap(awsConfig);
    awsProvider.save();

    Map<String, String> azuConfig =
        new HashMap<String, String>() {
          {
            put("AZURE_CLIENT_ID", "azuClientId");
            put("AZURE_CLIENT_SECRET", "azuClientSecret");
            put("AZURE_SUBSCRIPTION_ID", "azuSubscriptionId");
            put("AZURE_TENANT_ID", "azuTenantId");
          }
        };
    azureProvider = ModelFactory.azuProvider(defaultCustomer);
    azureProvider.setConfigMap(azuConfig);
    azureProvider.save();

    Map<String, String> gcpConfig =
        new HashMap<String, String>() {
          {
            put("client_email", "Client Email");
            put("project_id", "GCP Project ID");
            put("auth_provider_x509_cert_url", "Cert URL");
            put("private_key", "GCP Provider Private key");
            put("private_key_id", "GCP Private Key ID");
            put("CUSTOM_GCE_NETWORK", "GCP Custom network");
            put("GOOGLE_APPLICATION_CREDENTIALS", "credential file path");
          }
        };
    gcpProvider = ModelFactory.gcpProvider(defaultCustomer);
    gcpProvider.setConfig(gcpConfig);
    gcpProvider.save();
  }

  @Test
  public void migrateConfigToDetails() {
    V231__ProviderDetailsPersist.migrateConfigToDetails();
    gcpProvider.refresh();
    awsProvider.refresh();
    azureProvider.refresh();

    // GCP Provider Check
    assertNotNull(gcpProvider.getDetails());
    GCPCloudInfo gcpCloudInfo = gcpProvider.getDetails().getCloudInfo().gcp;
    assertNotNull(gcpCloudInfo);
    assertEquals("GCP Project ID", gcpCloudInfo.getGceProject());
    assertEquals("GCP Custom network", gcpCloudInfo.getDestVpcId());
    assertNotNull(gcpCloudInfo.getGceApplicationCredentials());

    // AWS Provider Check
    assertNotNull(awsProvider.getDetails());
    AWSCloudInfo awsCloudInfo = awsProvider.getDetails().getCloudInfo().aws;
    assertNotNull(awsCloudInfo);
    assertEquals("awsAccessKeyID", awsCloudInfo.awsAccessKeyID);
    assertEquals("awsAccessKeySecret", awsCloudInfo.awsAccessKeySecret);

    // Azure Provider Check
    assertNotNull(azureProvider.getDetails());
    AzureCloudInfo azureCloudInfo = azureProvider.getDetails().getCloudInfo().azu;
    assertNotNull(azureCloudInfo);
    assertEquals("azuClientId", azureCloudInfo.azuClientId);
    assertEquals("azuClientSecret", azureCloudInfo.azuClientSecret);
    assertEquals("azuSubscriptionId", azureCloudInfo.azuSubscriptionId);
    assertEquals("azuTenantId", azureCloudInfo.azuTenantId);
  }
}
