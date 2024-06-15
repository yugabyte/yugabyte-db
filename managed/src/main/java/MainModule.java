// Copyright (c) YugaByte, Inc.

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.AbstractModule;
import com.google.inject.Provides;
import com.google.inject.name.Names;
import com.nimbusds.oauth2.sdk.ParseException;
import com.nimbusds.oauth2.sdk.http.HTTPRequest;
import com.nimbusds.openid.connect.sdk.op.OIDCProviderMetadata;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.CloudModules;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.commissioner.AutomatedMasterFailover;
import com.yugabyte.yw.commissioner.BackupGarbageCollector;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.DefaultExecutorServiceProvider;
import com.yugabyte.yw.commissioner.ExecutorServiceProvider;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.PerfAdvisorNodeManager;
import com.yugabyte.yw.commissioner.PerfAdvisorScheduler;
import com.yugabyte.yw.commissioner.PitrConfigPoller;
import com.yugabyte.yw.commissioner.RefreshKmsService;
import com.yugabyte.yw.commissioner.SetUniverseKey;
import com.yugabyte.yw.commissioner.SupportBundleCleanup;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskGarbageCollector;
import com.yugabyte.yw.commissioner.XClusterSyncScheduler;
import com.yugabyte.yw.commissioner.YbcUpgrade;
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.AppInit;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.ExtraMigrationManager;
import com.yugabyte.yw.common.NativeKubernetesManager;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.PrometheusConfigHelper;
import com.yugabyte.yw.common.PrometheusConfigManager;
import com.yugabyte.yw.common.ReleaseContainerFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.ShellKubernetesManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.YBALifeCycle;
import com.yugabyte.yw.common.YamlWrapper;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertsGarbageCollector;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUniverseKeyCache;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil;
import com.yugabyte.yw.common.metrics.PlatformMetricsProcessor;
import com.yugabyte.yw.common.metrics.SwamperTargetsFileUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.common.operator.YBInformerFactory;
import com.yugabyte.yw.common.operator.YBReconcilerFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.rbac.PermissionUtil;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.common.rbac.RoleUtil;
import com.yugabyte.yw.common.services.LocalYBClientService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.common.ybflyway.YBFlywayInit;
import com.yugabyte.yw.controllers.MetricGrafanaController;
import com.yugabyte.yw.controllers.PlatformHttpActionAdapter;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.helpers.TaskTypesModule;
import com.yugabyte.yw.queries.QueryHelper;
import com.yugabyte.yw.scheduler.Scheduler;
import de.dentrassi.crypto.pem.PemKeyStoreProvider;
import io.prometheus.client.CollectorRegistry;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.security.Security;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.validator.routines.DomainValidator;
import org.bouncycastle.jce.provider.BouncyCastleProvider;
import org.pac4j.core.client.Clients;
import org.pac4j.core.http.url.DefaultUrlResolver;
import org.pac4j.oidc.client.OidcClient;
import org.pac4j.oidc.config.OidcConfiguration;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import org.yb.perf_advisor.module.PerfAdvisor;
import org.yb.perf_advisor.query.NodeManagerInterface;
import play.Environment;

/**
 * This class is a Guice module that tells Guice to bind different types
 *
 * <p>This 'MainModule' is registered with Play in the application.common.conf. This is named
 * `MainModule` to differentiate from the `app.Module` class generated by openapi.
 */
@Slf4j
public class MainModule extends AbstractModule {
  private final Config config;
  private final String[] TLD_OVERRIDE = {"local"};

  public MainModule(Environment environment, Config config) {
    this.config = config;
  }

  @Override
  public void configure() {
    bind(StaticInjectorHolder.class).asEagerSingleton();
    bind(Long.class)
        .annotatedWith(Names.named("AppStartupTimeMs"))
        .toInstance(System.currentTimeMillis());
    bind(YBALifeCycle.class).asEagerSingleton();
    if (!config.getBoolean("play.evolutions.enabled")) {
      // We want to init flyway only when evolutions are not enabled
      bind(YBFlywayInit.class).asEagerSingleton();
    } else {
      log.info("Using Evolutions. Not using flyway migrations.");
    }
    install(new TaskTypesModule());

    Security.addProvider(new PemKeyStoreProvider());
    Security.addProvider(new BouncyCastleProvider());
    TLSConfig.modifyTLSDisabledAlgorithms(config);
    bind(RuntimeConfigFactory.class).to(SettableRuntimeConfigFactory.class).asEagerSingleton();
    install(new CustomerConfKeys());
    install(new ProviderConfKeys());
    install(new GlobalConfKeys());
    install(new UniverseConfKeys());
    bind(RuntimeConfigCache.class).asEagerSingleton();

    install(new CloudModules());
    CollectorRegistry.defaultRegistry.clear();
    try {
      DomainValidator.updateTLDOverride(DomainValidator.ArrayType.LOCAL_PLUS, TLD_OVERRIDE);
    } catch (Exception domainValidatorException) {
      log.info("Skipping Initialization of domain validator for dev env's");
    }

    // Bind Application Initializer
    bind(AppInit.class).asEagerSingleton();
    bind(ConfigHelper.class).asEagerSingleton();
    // Set LocalClientService as the implementation for YBClientService
    bind(YBClientService.class).to(LocalYBClientService.class);
    bind(YsqlQueryExecutor.class).asEagerSingleton();
    bind(YcqlQueryExecutor.class).asEagerSingleton();
    bind(PlaySessionStore.class).to(PlayCacheSessionStore.class);
    bind(ExecutorServiceProvider.class).to(DefaultExecutorServiceProvider.class);
    bind(NodeManagerInterface.class).to(PerfAdvisorNodeManager.class);

    bind(PerfAdvisor.class).asEagerSingleton();
    bind(SwamperHelper.class).asEagerSingleton();
    bind(NodeManager.class).asEagerSingleton();
    bind(MetricQueryHelper.class).asEagerSingleton();
    bind(QueryHelper.class).asEagerSingleton();
    bind(ShellProcessHandler.class).asEagerSingleton();
    bind(NetworkManager.class).asEagerSingleton();
    bind(AccessManager.class).asEagerSingleton();
    bind(ReleaseManager.class).asEagerSingleton();
    bind(TemplateManager.class).asEagerSingleton();
    bind(ExtraMigrationManager.class).asEagerSingleton();
    bind(AWSInitializer.class).asEagerSingleton();
    bind(CallHome.class).asEagerSingleton();
    bind(Scheduler.class).asEagerSingleton();
    bind(HealthChecker.class).asEagerSingleton();
    bind(TaskGarbageCollector.class).asEagerSingleton();
    bind(PitrConfigPoller.class).asEagerSingleton();
    bind(AutomatedMasterFailover.class).asEagerSingleton();
    bind(BackupGarbageCollector.class).asEagerSingleton();
    bind(SupportBundleCleanup.class).asEagerSingleton();
    bind(EncryptionAtRestManager.class).asEagerSingleton();
    bind(EncryptionAtRestUniverseKeyCache.class).asEagerSingleton();
    bind(SetUniverseKey.class).asEagerSingleton();
    bind(RefreshKmsService.class).asEagerSingleton();
    bind(CustomerTaskManager.class).asEagerSingleton();
    bind(YamlWrapper.class).asEagerSingleton();
    bind(AlertManager.class).asEagerSingleton();
    bind(QueryAlerts.class).asEagerSingleton();
    bind(PlatformMetricsProcessor.class).asEagerSingleton();
    bind(AlertsGarbageCollector.class).asEagerSingleton();
    bind(AlertConfigurationWriter.class).asEagerSingleton();
    bind(SwamperTargetsFileUpdater.class).asEagerSingleton();
    bind(PlatformReplicationManager.class).asEagerSingleton();
    bind(PlatformReplicationHelper.class).asEagerSingleton();
    bind(GFlagsValidation.class).asEagerSingleton();
    bind(XClusterUniverseService.class).asEagerSingleton();
    bind(TaskExecutor.class).asEagerSingleton();
    bind(ShellKubernetesManager.class).asEagerSingleton();
    bind(NativeKubernetesManager.class).asEagerSingleton();
    bind(SupportBundleUtil.class).asEagerSingleton();
    bind(MetricGrafanaController.class).asEagerSingleton();
    bind(PlatformScheduler.class).asEagerSingleton();
    bind(AccessKeyRotationUtil.class).asEagerSingleton();
    bind(GcpEARServiceUtil.class).asEagerSingleton();
    bind(YbcUpgrade.class).asEagerSingleton();
    bind(XClusterSyncScheduler.class).asEagerSingleton();
    bind(PerfAdvisorScheduler.class).asEagerSingleton();
    bind(PermissionUtil.class).asEagerSingleton();
    bind(RoleUtil.class).asEagerSingleton();
    bind(RoleBindingUtil.class).asEagerSingleton();
    bind(PrometheusConfigManager.class).asEagerSingleton();
    bind(OperatorUtils.class).asEagerSingleton();
    bind(PrometheusConfigHelper.class).asEagerSingleton();
    bind(YbClientConfigFactory.class).asEagerSingleton();
    bind(OperatorStatusUpdaterFactory.class).asEagerSingleton();
    bind(YBInformerFactory.class).asEagerSingleton();
    bind(YBReconcilerFactory.class).asEagerSingleton();
    bind(ReleasesUtils.class).asEagerSingleton();
    bind(ReleaseContainerFactory.class).asEagerSingleton();

    requestStaticInjection(CertificateInfo.class);
    requestStaticInjection(HealthCheck.class);
    requestStaticInjection(AppConfigHelper.class);
  }

  @Provides
  protected OidcClient<OidcConfiguration> provideOidcClient(
      RuntimeConfigFactory runtimeConfigFactory, CustomCAStoreManager customCAStoreManager) {
    com.typesafe.config.Config config = runtimeConfigFactory.globalRuntimeConf();
    String securityType = config.getString("yb.security.type");
    if (securityType.equals("OIDC")) {
      if (customCAStoreManager.isEnabled()) {
        KeyStore ybaAndJavaKeyStore = customCAStoreManager.getYbaAndJavaKeyStore();
        try {
          TrustManagerFactory trustFactory =
              TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
          trustFactory.init(ybaAndJavaKeyStore);
          TrustManager[] ybaJavaTrustManagers = trustFactory.getTrustManagers();
          SecureRandom secureRandom = new SecureRandom();
          SSLContext sslContext = SSLContext.getInstance("TLS");
          sslContext.init(null, ybaJavaTrustManagers, secureRandom);
          HttpsURLConnection.setDefaultSSLSocketFactory(sslContext.getSocketFactory());
          HTTPRequest.setDefaultSSLSocketFactory(sslContext.getSocketFactory());
        } catch (Exception e) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Error occurred when building SSL context" + e.getMessage());
        }
      }
      OidcConfiguration oidcConfiguration = new OidcConfiguration();
      oidcConfiguration.setClientId(config.getString("yb.security.clientID"));
      oidcConfiguration.setSecret(config.getString("yb.security.secret"));
      oidcConfiguration.setScope(config.getString("yb.security.oidcScope"));
      setProviderMetadata(config, oidcConfiguration);
      oidcConfiguration.setMaxClockSkew(3600);
      oidcConfiguration.setResponseType("code");
      return new OidcClient<>(oidcConfiguration);
    } else {
      log.warn("Client with empty OIDC configuration because yb.security.type={}", securityType);
      // todo: fail fast instead of relying on log?
      return new OidcClient<>();
    }
  }

  private void setProviderMetadata(Config config, OidcConfiguration oidcConfiguration) {
    String providerMetadata = config.getString("yb.security.oidcProviderMetadata");
    if (providerMetadata.isEmpty()) {
      String discoveryURI = config.getString("yb.security.discoveryURI");
      if (discoveryURI.isEmpty()) {
        log.error("OIDC setup error: Both discoveryURL and provider metadata is empty");
        // TODO(sbapat) throw. Though rest of the method is written to fail silently so do not
        //  want to change that in this diff.
      } else {
        oidcConfiguration.setDiscoveryURI(discoveryURI);
      }
    } else {
      try {
        oidcConfiguration.setProviderMetadata(OIDCProviderMetadata.parse(providerMetadata));
      } catch (ParseException e) {
        log.error("Provider metadata invalid", e);
      }
    }
  }

  @Provides
  protected org.pac4j.core.config.Config providePac4jConfig(
      OidcClient<OidcConfiguration> oidcClient) {
    final Clients clients = new Clients("/api/v1/callback", oidcClient);
    clients.setUrlResolver(new DefaultUrlResolver(true));
    final org.pac4j.core.config.Config config = new org.pac4j.core.config.Config(clients);
    config.setHttpActionAdapter(new PlatformHttpActionAdapter());
    return config;
  }
}
