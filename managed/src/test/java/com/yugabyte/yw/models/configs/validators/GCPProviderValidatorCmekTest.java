// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.api.gax.rpc.ApiExceptionFactory;
import com.google.api.gax.rpc.StatusCode;
import com.google.api.services.compute.model.AttachedDisk;
import com.google.api.services.compute.model.CustomerEncryptionKey;
import com.google.api.services.compute.model.InstanceProperties;
import com.google.api.services.compute.model.InstanceTemplate;
import com.google.cloud.kms.v1.CryptoKey;
import com.google.cloud.kms.v1.CryptoKey.CryptoKeyPurpose;
import com.google.cloud.kms.v1.CryptoKeyVersion;
import com.google.cloud.kms.v1.CryptoKeyVersion.CryptoKeyVersionState;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.cloud.gcp.GCPProjectApiClient;
import com.yugabyte.yw.cloud.gcp.GCPProjectApiClientFactory;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import org.junit.Before;
import org.junit.Test;

/**
 * Covers the CMEK checks that acceptance criterion 3 depends on: a key that Compute Engine cannot
 * use for disk encryption must fail provider validation with a message that says how to fix it.
 * Boot and data disks carry their own keys, and none of this reads the instance template unless
 * {@code yb.gcp.read_instance_template} is on.
 */
public class GCPProviderValidatorCmekTest {

  private static final String TEMPLATE = "yba-cmek-tpl";
  private static final String JSON_PATH = "data.regions[0].details.cloudInfo.gcp.instanceTemplate";
  private static final String PROJECT_KEY_PREFIX = "projects/yugabyte/locations/";
  private static final String GLOBAL_KEY =
      PROJECT_KEY_PREFIX + "global/keyRings/satvikkeyring/cryptoKeys/satvik-test";
  private static final String WEST_KEY =
      PROJECT_KEY_PREFIX + "us-west1/keyRings/satvikkeyring/cryptoKeys/satvik-test";
  private static final String CENTRAL_KEY =
      PROJECT_KEY_PREFIX + "us-central1/keyRings/satvikkeyring/cryptoKeys/satvik-test";

  private GCPProviderValidator validator;
  private GCPProjectApiClient apiClient;
  private RuntimeConfGetter runtimeConfGetter;
  private SetMultimap<String, String> errors;

  @Before
  public void setUp() {
    runtimeConfGetter = mock(RuntimeConfGetter.class);
    validator =
        new GCPProviderValidator(
            mock(BeanValidator.class),
            runtimeConfGetter,
            mock(GCPCloudImpl.class),
            mock(GCPProjectApiClientFactory.class));
    apiClient = mock(GCPProjectApiClient.class);
    errors = HashMultimap.create();
  }

  private void setTemplateReadEnabled(boolean enabled) {
    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.readGcpInstanceTemplate))
        .thenReturn(enabled);
  }

  private static AttachedDisk disk(boolean boot, String kmsKeyName) {
    AttachedDisk disk = new AttachedDisk().setBoot(boot);
    if (kmsKeyName != null) {
      disk.setDiskEncryptionKey(new CustomerEncryptionKey().setKmsKeyName(kmsKeyName));
    }
    return disk;
  }

  private static InstanceTemplate template(String bootDiskKey, String... dataDiskKeys) {
    List<AttachedDisk> disks = new ArrayList<>();
    disks.add(disk(true, bootDiskKey));
    for (String dataDiskKey : dataDiskKeys) {
      disks.add(disk(false, dataDiskKey));
    }
    return new InstanceTemplate().setProperties(new InstanceProperties().setDisks(disks));
  }

  private static CryptoKey usableKey() {
    return CryptoKey.newBuilder()
        .setPurpose(CryptoKeyPurpose.ENCRYPT_DECRYPT)
        .setPrimary(CryptoKeyVersion.newBuilder().setState(CryptoKeyVersionState.ENABLED).build())
        .build();
  }

  private void validate(String bootDiskKey, String... dataDiskKeys) {
    validator.validateTemplateCmekKeys(
        TEMPLATE, template(bootDiskKey, dataDiskKeys), apiClient, errors, JSON_PATH);
  }

  private String soleError() {
    assertEquals("expected exactly one validation error, got " + errors, 1, errors.size());
    Collection<String> messages = errors.get(JSON_PATH);
    assertEquals("error was not attached to the instanceTemplate json path", 1, messages.size());
    return messages.iterator().next();
  }

  @Test
  public void templateWithoutCmekIsSkippedEntirely() {
    validate(null);

    assertTrue("a template without a key must not produce errors", errors.isEmpty());
    verify(apiClient, never()).getCryptoKey(anyString());
  }

  @Test
  public void globalKeyIsAcceptedForAnyRegion() {
    when(apiClient.getCryptoKey(GLOBAL_KEY)).thenReturn(usableKey());

    validate(GLOBAL_KEY);

    assertTrue("a valid global key must validate cleanly, got " + errors, errors.isEmpty());
  }

  @Test
  public void sameRegionKeyIsAccepted() {
    when(apiClient.getCryptoKey(WEST_KEY)).thenReturn(usableKey());

    validate(WEST_KEY);

    assertTrue("a valid same-region key must validate cleanly, got " + errors, errors.isEmpty());
  }

  @Test
  public void keyInAnotherRegionIsValidatedByGcp() {
    when(apiClient.getCryptoKey(CENTRAL_KEY)).thenReturn(usableKey());

    validate(CENTRAL_KEY);

    assertTrue("key location compatibility is delegated to GCP, got " + errors, errors.isEmpty());
  }

  @Test
  public void versionedKeyNameIsAccepted() {
    String versionedKey = GLOBAL_KEY + "/cryptoKeyVersions/1";
    when(apiClient.getCryptoKey(versionedKey)).thenReturn(usableKey());

    validate(versionedKey);

    assertTrue(
        "a versioned key name must not be seen as malformed, got " + errors, errors.isEmpty());
  }

  @Test
  public void bootAndDataDiskKeysAreBothValidated() {
    when(apiClient.getCryptoKey(GLOBAL_KEY)).thenReturn(usableKey());
    when(apiClient.getCryptoKey(WEST_KEY)).thenReturn(usableKey());

    validate(GLOBAL_KEY, WEST_KEY, WEST_KEY);

    assertTrue("distinct boot and data keys are both allowed, got " + errors, errors.isEmpty());
    verify(apiClient).getCryptoKey(GLOBAL_KEY);
    verify(apiClient).getCryptoKey(WEST_KEY);
  }

  @Test
  public void dataDiskOnlyKeyIsValidated() {
    when(apiClient.getCryptoKey(WEST_KEY)).thenThrow(apiException(StatusCode.Code.NOT_FOUND));

    validate(null, WEST_KEY);

    String error = soleError();
    assertTrue("should name the data disk key: " + error, error.contains(WEST_KEY));
    assertTrue("should say not found: " + error, error.contains("was not found"));
  }

  @Test
  public void keySharedByBootAndDataDisksIsFetchedOnce() {
    when(apiClient.getCryptoKey(GLOBAL_KEY)).thenReturn(usableKey());

    validate(GLOBAL_KEY, GLOBAL_KEY);

    assertTrue("a shared key must validate cleanly, got " + errors, errors.isEmpty());
    verify(apiClient, times(1)).getCryptoKey(GLOBAL_KEY);
  }

  @Test
  public void firstDataDiskKeyIsUsedWhenTheyDiffer() {
    when(apiClient.getCryptoKey(WEST_KEY)).thenReturn(usableKey());

    validate(null, WEST_KEY, CENTRAL_KEY);

    assertTrue("only the first data disk key applies, got " + errors, errors.isEmpty());
    verify(apiClient, never()).getCryptoKey(CENTRAL_KEY);
  }

  @Test
  public void malformedKeyIsRejected() {
    validate("keyRings/satvikkeyring/cryptoKeys/satvik-test");

    String error = soleError();
    assertTrue("should call out the malformed key: " + error, error.contains("malformed"));
    assertTrue("should show the expected form: " + error, error.contains("cryptoKeys/<key>"));
    verify(apiClient, never()).getCryptoKey(anyString());
  }

  @Test
  public void missingKeyIsReportedAsNotFound() {
    when(apiClient.getCryptoKey(GLOBAL_KEY)).thenThrow(apiException(StatusCode.Code.NOT_FOUND));

    validate(GLOBAL_KEY);

    String error = soleError();
    assertTrue("should say not found: " + error, error.contains("was not found"));
    assertTrue("should name the key: " + error, error.contains(GLOBAL_KEY));
    assertTrue("should name the template: " + error, error.contains(TEMPLATE));
  }

  @Test
  public void wrongPurposeKeyIsRejected() {
    when(apiClient.getCryptoKey(GLOBAL_KEY))
        .thenReturn(CryptoKey.newBuilder().setPurpose(CryptoKeyPurpose.ASYMMETRIC_SIGN).build());

    validate(GLOBAL_KEY);

    String error = soleError();
    assertTrue("should name the actual purpose: " + error, error.contains("ASYMMETRIC_SIGN"));
    assertTrue("should name the required purpose: " + error, error.contains("ENCRYPT_DECRYPT"));
  }

  @Test
  public void disabledPrimaryVersionIsRejected() {
    when(apiClient.getCryptoKey(GLOBAL_KEY))
        .thenReturn(
            CryptoKey.newBuilder()
                .setPurpose(CryptoKeyPurpose.ENCRYPT_DECRYPT)
                .setPrimary(
                    CryptoKeyVersion.newBuilder().setState(CryptoKeyVersionState.DISABLED).build())
                .build());

    validate(GLOBAL_KEY);

    String error = soleError();
    assertTrue("should name the state: " + error, error.contains("DISABLED"));
    assertTrue("should say ENABLED is required: " + error, error.contains("ENABLED"));
  }

  @Test
  public void unreadableKeyDoesNotBlockProviderValidation() {
    // cryptoKeys.get is not required to attach the key to a disk, so a permission denial must
    // not fail provider validation.
    when(apiClient.getCryptoKey(GLOBAL_KEY))
        .thenThrow(apiException(StatusCode.Code.PERMISSION_DENIED));

    validate(GLOBAL_KEY);

    assertTrue(
        "missing cryptoKeys.get must not fail validation, got " + errors, errors.isEmpty());
  }

  @Test
  public void templateIsOnlyCheckedForExistenceWhenTheReadIsDisabled() {
    // Reading the template needs compute.instanceTemplates.get; providers that only use it as an
    // instance source do not have it, so with the config off validation must stay on list.
    setTemplateReadEnabled(false);
    when(apiClient.checkInstanceTemplateExists(TEMPLATE)).thenReturn(false);

    validator.validateInstanceTempl(TEMPLATE, apiClient, errors, JSON_PATH);

    String error = soleError();
    assertTrue("should say the template is not found: " + error, error.contains("is not found"));
    verify(apiClient, never()).getInstanceTemplate(anyString());
    verify(apiClient, never()).getCryptoKey(anyString());
  }

  @Test
  public void templateIsReadWhenTheReadIsEnabled() {
    setTemplateReadEnabled(true);
    when(apiClient.getInstanceTemplate(TEMPLATE)).thenReturn(template(GLOBAL_KEY));
    when(apiClient.getCryptoKey(GLOBAL_KEY)).thenReturn(usableKey());

    validator.validateInstanceTempl(TEMPLATE, apiClient, errors, JSON_PATH);

    assertTrue("a valid template must validate cleanly, got " + errors, errors.isEmpty());
    verify(apiClient).getCryptoKey(GLOBAL_KEY);
    verify(apiClient, never()).checkInstanceTemplateExists(anyString());
  }

  private static RuntimeException apiException(StatusCode.Code code) {
    return ApiExceptionFactory.createException(
        new RuntimeException("simulated " + code),
        new StatusCode() {
          @Override
          public Code getCode() {
            return code;
          }

          @Override
          public Object getTransportCode() {
            return null;
          }
        },
        false);
  }
}
