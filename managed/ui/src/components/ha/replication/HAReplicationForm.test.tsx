import React from 'react';
import { toast } from 'react-toastify';
import userEvent from '@testing-library/user-event';
import { screen } from '@testing-library/react';

import { render, fireEvent, waitFor } from '../../../test-utils';
import { FREQUENCY_MULTIPLIER, HAInstanceTypes, HAReplicationForm } from './HAReplicationForm';
import { HAConfig, HAReplicationSchedule } from '../../../redesign/helpers/dtos';
import { api } from '../../../redesign/helpers/api';
import { mockRuntimeConfigs } from './mockUtils';

jest.mock('../../../redesign/helpers/api');

const mockConfig = {
  cluster_key: 'fake-key',
  instances: [
    {
      uuid: 'instance-id-1',
      address: 'http://fake.address',
      is_leader: true,
      is_local: true
    }
  ]
} as HAConfig;
const mockSchedule: HAReplicationSchedule = {
  frequency_milliseconds: 5 * FREQUENCY_MULTIPLIER,
  is_running: false // intentionally set enable replication toggle fo "off" to test all edge cases
};

const setup = (hasPeerCerts: boolean, config?: HAConfig, schedule?: HAReplicationSchedule) => {
  const backToView = jest.fn();
  const fetchRunTimeConfigs = jest.fn();
  const setRunTimeConfig = jest.fn();
  const mockRuntimeConfigWithPeerCerts = {
    type: 'GLOBAL',
    uuid: '00000000-0000-0000-0000-000000000000',
    mutableScope: true,
    configEntries: [
      {
        inherited: false,
        key: 'yb.ha.ws',
        value:
          '{"ahc":{"disableUrlEncoding":false,"idleConnectionInPoolTimeout":"1 minute","keepAlive":true,"maxConnectionLifetime":null,"maxConnectionsPerHost":-1,"maxConnectionsTotal":-1,"maxNumberOfRedirects":5,"maxRequestRetry":5},"cache":{"cacheManagerResource":null,"cacheManagerURI":null,"cachingProviderName":"","enabled":false,"heuristics":{"enabled":false},"name":"play-ws-cache"},"compressionEnabled":false,"followRedirects":true,"ssl":{"checkRevocation":null,"debug":{"all":false,"certpath":false,"data":false,"defaultctx":false,"handshake":false,"keygen":false,"keymanager":false,"ocsp":false,"packet":false,"plaintext":false,"pluggability":false,"record":false,"session":false,"sessioncache":false,"ssl":false,"sslctx":false,"trustmanager":false,"verbose":false},"default":false,"disabledKeyAlgorithms":["RSA keySize < 2048","DSA keySize < 2048","EC keySize < 224"],"disabledSignatureAlgorithms":["MD2","MD4","MD5"],"enabledCipherSuites":[],"enabledProtocols":["TLSv1.2","TLSv1.1","TLSv1"],"hostnameVerifierClass":null,"keyManager":{"algorithm":null,"prototype":{"stores":{"data":null,"password":null,"path":null,"type":null}},"stores":[]},"loose":{"acceptAnyCertificate":false,"allowLegacyHelloMessages":null,"allowUnsafeRenegotiation":null,"allowWeakCiphers":false,"allowWeakProtocols":false,"disableHostnameVerification":false,"disableSNI":false},"protocol":"TLSv1.2","revocationLists":[],"sslParameters":{"clientAuth":"default","protocols":[]},"trustManager":{"algorithm":null,"prototype":{"stores":{"data":null,"path":null,"type":null}},"stores":[{"data":"-----BEGIN CERTIFICATE-----\\nMIIOHDCCDQSgAwIBAgIRAO5kLPg5l5zJEkJWJqfEbQowDQYJKoZIhvcNAQELBQAw\\nRjELMAkGA1UEBhMCVVMxIjAgBgNVBAoTGUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBM\\nTEMxEzARBgNVBAMTCkdUUyBDQSAxQzMwHhcNMjIxMTAyMTM0MzA5WhcNMjMwMTI1\\nMTM0MzA4WjAXMRUwEwYDVQQDDAwqLmdvb2dsZS5jb20wWTATBgcqhkjOPQIBBggq\\nhkjOPQMBBwNCAATnpaH4wd+cGIQdMkL+05urIhqgiFWxpXuisENOgKFc/tdEponF\\n9PgXdr8WWQlIjRxlJ5Z7QxbPQlikvUeQFkVKo4IL/TCCC/kwDgYDVR0PAQH/BAQD\\nAgeAMBMGA1UdJQQMMAoGCCsGAQUFBwMBMAwGA1UdEwEB/wQCMAAwHQYDVR0OBBYE\\nFE4XeU6urCodRXAa/1YYmlrAAkbWMB8GA1UdIwQYMBaAFIp0f6+Fze6VzT2c0OJG\\nFPNxNR0nMGoGCCsGAQUFBwEBBF4wXDAnBggrBgEFBQcwAYYbaHR0cDovL29jc3Au\\ncGtpLmdvb2cvZ3RzMWMzMDEGCCsGAQUFBzAChiVodHRwOi8vcGtpLmdvb2cvcmVw\\nby9jZXJ0cy9ndHMxYzMuZGVyMIIJrQYDVR0RBIIJpDCCCaCCDCouZ29vZ2xlLmNv\\nbYIWKi5hcHBlbmdpbmUuZ29vZ2xlLmNvbYIJKi5iZG4uZGV2ghUqLm9yaWdpbi10\\nZXN0LmJkbi5kZXaCEiouY2xvdWQuZ29vZ2xlLmNvbYIYKi5jcm93ZHNvdXJjZS5n\\nb29nbGUuY29tghgqLmRhdGFjb21wdXRlLmdvb2dsZS5jb22CCyouZ29vZ2xlLmNh\\nggsqLmdvb2dsZS5jbIIOKi5nb29nbGUuY28uaW6CDiouZ29vZ2xlLmNvLmpwgg4q\\nLmdvb2dsZS5jby51a4IPKi5nb29nbGUuY29tLmFygg8qLmdvb2dsZS5jb20uYXWC\\nDyouZ29vZ2xlLmNvbS5icoIPKi5nb29nbGUuY29tLmNvgg8qLmdvb2dsZS5jb20u\\nbXiCDyouZ29vZ2xlLmNvbS50coIPKi5nb29nbGUuY29tLnZuggsqLmdvb2dsZS5k\\nZYILKi5nb29nbGUuZXOCCyouZ29vZ2xlLmZyggsqLmdvb2dsZS5odYILKi5nb29n\\nbGUuaXSCCyouZ29vZ2xlLm5sggsqLmdvb2dsZS5wbIILKi5nb29nbGUucHSCEiou\\nZ29vZ2xlYWRhcGlzLmNvbYIPKi5nb29nbGVhcGlzLmNughEqLmdvb2dsZXZpZGVv\\nLmNvbYIMKi5nc3RhdGljLmNughAqLmdzdGF0aWMtY24uY29tgg9nb29nbGVjbmFw\\ncHMuY26CESouZ29vZ2xlY25hcHBzLmNughFnb29nbGVhcHBzLWNuLmNvbYITKi5n\\nb29nbGVhcHBzLWNuLmNvbYIMZ2tlY25hcHBzLmNugg4qLmdrZWNuYXBwcy5jboIS\\nZ29vZ2xlZG93bmxvYWRzLmNughQqLmdvb2dsZWRvd25sb2Fkcy5jboIQcmVjYXB0\\nY2hhLm5ldC5jboISKi5yZWNhcHRjaGEubmV0LmNughByZWNhcHRjaGEtY24ubmV0\\nghIqLnJlY2FwdGNoYS1jbi5uZXSCC3dpZGV2aW5lLmNugg0qLndpZGV2aW5lLmNu\\nghFhbXBwcm9qZWN0Lm9yZy5jboITKi5hbXBwcm9qZWN0Lm9yZy5jboIRYW1wcHJv\\namVjdC5uZXQuY26CEyouYW1wcHJvamVjdC5uZXQuY26CF2dvb2dsZS1hbmFseXRp\\nY3MtY24uY29tghkqLmdvb2dsZS1hbmFseXRpY3MtY24uY29tghdnb29nbGVhZHNl\\ncnZpY2VzLWNuLmNvbYIZKi5nb29nbGVhZHNlcnZpY2VzLWNuLmNvbYIRZ29vZ2xl\\ndmFkcy1jbi5jb22CEyouZ29vZ2xldmFkcy1jbi5jb22CEWdvb2dsZWFwaXMtY24u\\nY29tghMqLmdvb2dsZWFwaXMtY24uY29tghVnb29nbGVvcHRpbWl6ZS1jbi5jb22C\\nFyouZ29vZ2xlb3B0aW1pemUtY24uY29tghJkb3VibGVjbGljay1jbi5uZXSCFCou\\nZG91YmxlY2xpY2stY24ubmV0ghgqLmZscy5kb3VibGVjbGljay1jbi5uZXSCFiou\\nZy5kb3VibGVjbGljay1jbi5uZXSCDmRvdWJsZWNsaWNrLmNughAqLmRvdWJsZWNs\\naWNrLmNughQqLmZscy5kb3VibGVjbGljay5jboISKi5nLmRvdWJsZWNsaWNrLmNu\\nghFkYXJ0c2VhcmNoLWNuLm5ldIITKi5kYXJ0c2VhcmNoLWNuLm5ldIIdZ29vZ2xl\\ndHJhdmVsYWRzZXJ2aWNlcy1jbi5jb22CHyouZ29vZ2xldHJhdmVsYWRzZXJ2aWNl\\ncy1jbi5jb22CGGdvb2dsZXRhZ3NlcnZpY2VzLWNuLmNvbYIaKi5nb29nbGV0YWdz\\nZXJ2aWNlcy1jbi5jb22CF2dvb2dsZXRhZ21hbmFnZXItY24uY29tghkqLmdvb2ds\\nZXRhZ21hbmFnZXItY24uY29tghhnb29nbGVzeW5kaWNhdGlvbi1jbi5jb22CGiou\\nZ29vZ2xlc3luZGljYXRpb24tY24uY29tgiQqLnNhZmVmcmFtZS5nb29nbGVzeW5k\\naWNhdGlvbi1jbi5jb22CFmFwcC1tZWFzdXJlbWVudC1jbi5jb22CGCouYXBwLW1l\\nYXN1cmVtZW50LWNuLmNvbYILZ3Z0MS1jbi5jb22CDSouZ3Z0MS1jbi5jb22CC2d2\\ndDItY24uY29tgg0qLmd2dDItY24uY29tggsybWRuLWNuLm5ldIINKi4ybWRuLWNu\\nLm5ldIIUZ29vZ2xlZmxpZ2h0cy1jbi5uZXSCFiouZ29vZ2xlZmxpZ2h0cy1jbi5u\\nZXSCDGFkbW9iLWNuLmNvbYIOKi5hZG1vYi1jbi5jb22CFGdvb2dsZXNhbmRib3gt\\nY24uY29tghYqLmdvb2dsZXNhbmRib3gtY24uY29tgg0qLmdzdGF0aWMuY29tghQq\\nLm1ldHJpYy5nc3RhdGljLmNvbYIKKi5ndnQxLmNvbYIRKi5nY3BjZG4uZ3Z0MS5j\\nb22CCiouZ3Z0Mi5jb22CDiouZ2NwLmd2dDIuY29tghAqLnVybC5nb29nbGUuY29t\\nghYqLnlvdXR1YmUtbm9jb29raWUuY29tggsqLnl0aW1nLmNvbYILYW5kcm9pZC5j\\nb22CDSouYW5kcm9pZC5jb22CEyouZmxhc2guYW5kcm9pZC5jb22CBGcuY26CBiou\\nZy5jboIEZy5jb4IGKi5nLmNvggZnb28uZ2yCCnd3dy5nb28uZ2yCFGdvb2dsZS1h\\nbmFseXRpY3MuY29tghYqLmdvb2dsZS1hbmFseXRpY3MuY29tggpnb29nbGUuY29t\\nghJnb29nbGVjb21tZXJjZS5jb22CFCouZ29vZ2xlY29tbWVyY2UuY29tgghnZ3Bo\\ndC5jboIKKi5nZ3BodC5jboIKdXJjaGluLmNvbYIMKi51cmNoaW4uY29tggh5b3V0\\ndS5iZYILeW91dHViZS5jb22CDSoueW91dHViZS5jb22CFHlvdXR1YmVlZHVjYXRp\\nb24uY29tghYqLnlvdXR1YmVlZHVjYXRpb24uY29tgg95b3V0dWJla2lkcy5jb22C\\nESoueW91dHViZWtpZHMuY29tggV5dC5iZYIHKi55dC5iZYIaYW5kcm9pZC5jbGll\\nbnRzLmdvb2dsZS5jb22CG2RldmVsb3Blci5hbmRyb2lkLmdvb2dsZS5jboIcZGV2\\nZWxvcGVycy5hbmRyb2lkLmdvb2dsZS5jboIYc291cmNlLmFuZHJvaWQuZ29vZ2xl\\nLmNuMCEGA1UdIAQaMBgwCAYGZ4EMAQIBMAwGCisGAQQB1nkCBQMwPAYDVR0fBDUw\\nMzAxoC+gLYYraHR0cDovL2NybHMucGtpLmdvb2cvZ3RzMWMzL1FxRnhiaTlNNDhj\\nLmNybDCCAQQGCisGAQQB1nkCBAIEgfUEgfIA8AB2AOg+0No+9QY1MudXKLyJa8kD\\n08vREWvs62nhd31tBr1uAAABhDjL2IwAAAQDAEcwRQIgQQ1PvUC5phcRASIsCKB8\\nZHkxPAAx7FtQIWpAVUpIN9ECIQC128MH0F8IWJttknlqARlThgqYuyo2JRwBAVQu\\nhLzFLwB2AHoyjFTYty22IOo44FIe6YQWcDIThU070ivBOlejUutSAAABhDjL2NgA\\nAAQDAEcwRQIhAJwJMHFE6fP1mqw9fxpJkgKOXgzmcStNhLxwax5AUTSPAiBP5yxG\\nQar6D/k8RFHezOJjOtTfQQX3zxvRAP5nDWZHSjANBgkqhkiG9w0BAQsFAAOCAQEA\\n4S8KgjhxeMVDJY+lXTAb3oIICSjcJN/3KSjUIy24cdd+mVz1c0h9BSSA+dDy//kp\\nVrAILEgJddKsnF0sjco5rjlrL8cyn5VWGO4stENfK4cQW6mb+6pYYNTfpWglIu6g\\nqeH+BvPjhgqC3qhFJ/ZuHc6gl+lsA4HCSJKw8ecKfM2Re2MJPF/xeJTdON++39DM\\nksdidhWcB67A1ynzZ9zXYj51Q9ISXO6VlHSnugxCMpS9zmdkaasHFWvvFsNuFxYY\\n833e/p+f5bHRXkmdogOldIChmLjvlvcIVEhKtkSv7Rduc3PnO9HJj7Q4t0gZ1oMn\\nBuBl4CVkECWt6k0Xq5yorw==\\n-----END CERTIFICATE-----","type":"PEM"}]}},"timeout":{"connection":"2 minutes","idle":"2 minutes","request":"2 minutes"},"useProxyProperties":true,"useragent":null}'
      }
    ]
  };
  const mockRuntimeConfigPromise = {
    data: hasPeerCerts ? mockRuntimeConfigWithPeerCerts : mockRuntimeConfigs,
    error: null,
    promiseState: 'SUCCESS'
  };

  const component = render(
    <HAReplicationForm
      config={config}
      schedule={schedule}
      backToViewMode={backToView}
      runtimeConfigs={mockRuntimeConfigPromise}
      fetchRuntimeConfigs={fetchRunTimeConfigs}
      setRuntimeConfig={setRunTimeConfig}
    />
  );

  const form = component.getByRole('form');
  const formFields = {
    instanceType: form.querySelector<HTMLInputElement>('input[name="instanceType"]:checked')!,
    instanceAddress: form.querySelector<HTMLInputElement>('input[name="instanceAddress"]')!,
    clusterKey: form.querySelector<HTMLInputElement>('input[name="clusterKey"]')!,
    replicationFrequency: form.querySelector<HTMLInputElement>(
      'input[name="replicationFrequency"]'
    )!,
    replicationEnabled: form.querySelector<HTMLInputElement>('input[name="replicationEnabled"]')!
  };
  const formValues = {
    instanceType: formFields.instanceType.value,
    instanceAddress: formFields.instanceAddress.value,
    clusterKey: formFields.clusterKey.value,
    replicationFrequency: formFields.replicationFrequency.value,
    replicationEnabled: formFields.replicationEnabled.checked
  };

  return { component, formFields, formValues, backToView };
};

describe('HA replication configuration form', () => {
  it('should render form with values matching INITIAL_VALUES when no data provided', () => {
    const { component, formValues } = setup(true);
    expect(formValues).toEqual({
      instanceType: HAInstanceTypes.Active,
      instanceAddress: 'http://localhost',
      clusterKey: '',
      replicationFrequency: '1',
      replicationEnabled: true
    });
    expect(component.getByRole('button', { name: /create/i })).toBeDisabled();
  });

  it('should render form with values provided in config and schedule mocks', () => {
    const { component, formValues } = setup(true, mockConfig, mockSchedule);
    expect(formValues).toEqual({
      instanceType: mockConfig.instances[0].is_leader
        ? HAInstanceTypes.Active
        : HAInstanceTypes.Standby,
      instanceAddress: mockConfig.instances[0].address,
      clusterKey: mockConfig.cluster_key,
      replicationFrequency: String(mockSchedule.frequency_milliseconds / FREQUENCY_MULTIPLIER),
      replicationEnabled: mockSchedule.is_running
    });
    // although form is valid - the submit button should be initially disabled as form is pristine
    expect(component.getByRole('button', { name: /save/i })).toBeDisabled();
  });

  it('should show error toast on incorrect config', () => {
    const config = { instances: [{}] } as HAConfig;
    const toastError = jest.fn();
    jest.spyOn(toast, 'error').mockImplementation(toastError);
    setup(true, config, {} as HAReplicationSchedule);
    expect(toastError).toBeCalled();
  });

  it('should change the view on switching from active to standy mode', () => {
    const { component } = setup(true);

    // check active mode view
    expect(component.queryByRole('alert')).not.toBeInTheDocument();
    expect(component.getByTestId('ha-replication-config-form-schedule-section')).toBeVisible();

    // switch to standby mode
    userEvent.click(component.getByText(/standby/i));

    // check standby mode view
    expect(component.getByRole('alert')).toBeInTheDocument();
    expect(component.queryByRole('button', { name: /generate key/i })).not.toBeInTheDocument();
    expect(component.getByTestId('ha-replication-config-form-schedule-section')).not.toBeVisible();
  });

  it('should disable all form fields in edit mode when replication toggle is off', () => {
    const { component } = setup(true, mockConfig, mockSchedule);
    component.getAllByRole('radio').forEach((element) => expect(element).toBeDisabled());
    component.getAllByRole('textbox').forEach((element) => expect(element).toBeDisabled());
  });

  it('should not show any validation messages initially', () => {
    const { component } = setup(true);
    component
      .queryAllByTestId('yb-label-validation-error')
      .forEach((element) => expect(element).not.toBeInTheDocument());
  });

  // formik validation is async, therefore use async/await
  it('should validate address field', async () => {
    const { component, formFields } = setup(true);

    userEvent.clear(formFields.instanceAddress);
    fireEvent.blur(formFields.instanceAddress);
    expect(await component.findByText(/required field/i)).toBeInTheDocument();

    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, 'lorem ipsum');
    expect(await component.findByText(/should be a valid url/i)).toBeInTheDocument();

    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, 'http://valid.url');
    expect(await component.findByText(/should be a valid url/i)).not.toBeInTheDocument();
  });

  it('should validate cluster key field', async () => {
    const { component, formFields } = setup(true);

    // that fields is editable in standby mode only
    userEvent.click(component.getByText(/standby/i));

    fireEvent.blur(formFields.clusterKey);
    expect(await component.findByText(/required field/i)).toBeInTheDocument();

    userEvent.type(formFields.clusterKey, 'some-fake-key');
    expect(await component.findByText(/required field/i)).not.toBeInTheDocument();
  });

  it('should validate replication frequency field', async () => {
    const { component, formFields } = setup(true);

    // check for required value validation
    userEvent.clear(formFields.replicationFrequency);
    fireEvent.blur(formFields.replicationFrequency);
    expect(await component.findByText(/required field/i)).toBeInTheDocument();

    // disable frequency input and make sure previously visible error message is gone
    userEvent.click(formFields.replicationEnabled);
    expect(formFields.replicationFrequency).toBeDisabled();
    expect(await component.findByText(/required field/i)).not.toBeInTheDocument();

    // enable frequency input back and assure it's enabled
    userEvent.click(formFields.replicationEnabled);
    expect(formFields.replicationFrequency).toBeEnabled();

    // check for min value validation
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, '-1');
    expect(await component.findByText(/minimum value is 1/i)).toBeInTheDocument();

    // check if it filters non-number input and validation message is gone
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, 'qwerty 123');
    expect(formFields.replicationFrequency).toHaveValue(123);
    expect(await component.findByTestId('yb-label-validation-error')).not.toBeInTheDocument();
  });

  it('should disable submit button on validation failure', async () => {
    const { component, formFields } = setup(true, mockConfig, mockSchedule);

    // enable frequency field, type smth and check that submit button become enabled
    userEvent.click(formFields.replicationEnabled);
    userEvent.type(formFields.replicationFrequency, '10');
    fireEvent.blur(formFields.replicationFrequency);
    expect(component.getByRole('button', { name: /save/i })).toBeEnabled();

    // force validation error and check if submit button become disabled
    userEvent.clear(formFields.replicationFrequency);
    fireEvent.blur(formFields.replicationFrequency);
    expect(await component.findByRole('button', { name: /save/i })).toBeDisabled();
  });

  it('should check active config creation happy path', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'http://fake-address',
      clusterKey: 'fake-key',
      replicationFrequency: '30'
    };

    (api.generateHAKey as jest.Mock).mockResolvedValue({ cluster_key: fakeValues.clusterKey });
    (api.createHAConfig as jest.Mock).mockResolvedValue({ uuid: fakeValues.configId });
    (api.createHAInstance as jest.Mock).mockResolvedValue({});
    (api.startHABackupSchedule as jest.Mock).mockResolvedValue({});

    const { component, formFields, backToView } = setup(true);

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // generate cluster key and check form value
    userEvent.click(component.queryByRole('button', { name: /generate key/i })!);
    await waitFor(() => expect(api.generateHAKey).toBeCalled());
    expect(formFields.clusterKey).toHaveValue(fakeValues.clusterKey);

    // set replication frequency
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, fakeValues.replicationFrequency);

    // click the submit button
    expect(component.getByRole('button', { name: /create/i })).toBeEnabled();
    userEvent.click(component.getByRole('button', { name: /create/i }));

    await waitFor(() => {
      expect(api.createHAConfig).toBeCalledWith(fakeValues.clusterKey);
      expect(api.createHAInstance).toBeCalledWith(
        fakeValues.configId,
        fakeValues.instanceAddress,
        true,
        true
      );
      expect(api.startHABackupSchedule).toBeCalledWith(
        fakeValues.configId,
        Number(fakeValues.replicationFrequency) * FREQUENCY_MULTIPLIER
      );
    });

    expect(backToView).toBeCalled();
  });

  it('should check standby config creation happy path', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'http://fake-address',
      clusterKey: 'fake-key'
    };

    (api.createHAConfig as jest.Mock).mockResolvedValue({ uuid: fakeValues.configId });
    (api.createHAInstance as jest.Mock).mockResolvedValue({});

    const { component, formFields, backToView } = setup(true);

    // select standby mode
    userEvent.click(component.getByText(/standby/i));

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // enter cluster key
    userEvent.type(formFields.clusterKey, fakeValues.clusterKey);

    // click the submit button
    expect(component.getByRole('button', { name: /create/i })).toBeEnabled();
    userEvent.click(component.getByRole('button', { name: /create/i }));

    await waitFor(() => {
      expect(api.createHAConfig).toBeCalledWith(fakeValues.clusterKey);
      expect(api.createHAInstance).toBeCalledWith(
        fakeValues.configId,
        fakeValues.instanceAddress,
        false,
        true
      );
    });

    expect(backToView).toBeCalled();
  });
  it('should disable the submit button for active config if peer certs do not exist', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'http://fake-address',
      clusterKey: 'fake-key',
      replicationFrequency: '30'
    };
    (api.generateHAKey as jest.Mock).mockResolvedValue({ cluster_key: fakeValues.clusterKey });

    const { component, formFields } = setup(false);

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // generate cluster key and check form value
    userEvent.click(component.queryByRole('button', { name: /generate key/i })!);
    await waitFor(() => expect(api.generateHAKey).toBeCalled());
    expect(formFields.clusterKey).toHaveValue(fakeValues.clusterKey);

    // set replication frequency
    userEvent.clear(formFields.replicationFrequency);
    userEvent.type(formFields.replicationFrequency, fakeValues.replicationFrequency);

    // Verify the submit button is disabled (since no peer certs were added).
    expect(component.getByRole('button', { name: /create/i })).toBeDisabled();
  });
  it('should disable the submit button for standby config if peer certs do not exist', async () => {
    const fakeValues = {
      configId: 'fake-config-id',
      instanceAddress: 'http://fake-address',
      clusterKey: 'fake-key'
    };
    const { component, formFields } = setup(false);

    // select standby mode
    userEvent.click(component.getByText(/standby/i));

    // enter address
    userEvent.clear(formFields.instanceAddress);
    userEvent.type(formFields.instanceAddress, fakeValues.instanceAddress);

    // enter cluster key
    userEvent.type(formFields.clusterKey, fakeValues.clusterKey);

    // Verify the submit button is disabled (since no peer certs were added).
    expect(component.getByRole('button', { name: /create/i })).toBeDisabled();
  });
  it('should check enabling replication happy flow', async () => {
    (api.startHABackupSchedule as jest.Mock).mockResolvedValue({});

    const { component, formFields, backToView } = setup(true, mockConfig, mockSchedule);

    userEvent.click(formFields.replicationEnabled);
    expect(component.getByRole('button', { name: /save/i })).toBeEnabled();
    userEvent.click(component.getByRole('button', { name: /save/i }));
    await waitFor(() => {
      expect(api.startHABackupSchedule).toBeCalledWith(
        undefined,
        mockSchedule.frequency_milliseconds
      );
    });
    expect(backToView).toBeCalled();
  });

  it('should check disabling replication happy flow', async () => {
    (api.stopHABackupSchedule as jest.Mock).mockResolvedValue({});

    const { component, formFields, backToView } = setup(true, mockConfig, mockSchedule);

    // make dummy changes to form to enable submit button with turned off replication toggle
    userEvent.click(formFields.replicationEnabled);
    userEvent.type(formFields.replicationFrequency, '1');
    userEvent.click(formFields.replicationEnabled);

    expect(component.getByRole('button', { name: /save/i })).toBeEnabled();

    userEvent.click(component.getByRole('button', { name: /save/i }));
    await waitFor(() => expect(api.stopHABackupSchedule).toBeCalledWith(undefined));
    expect(backToView).toBeCalled();
  });

  it('should show toast on api call failure', async () => {
    (api.startHABackupSchedule as jest.Mock).mockRejectedValue({});
    const toastError = jest.fn();
    jest.spyOn(toast, 'error').mockImplementation(toastError);
    const consoleError = jest.fn();
    jest.spyOn(console, 'error').mockImplementation(consoleError);

    const { component, formFields, backToView } = setup(true, mockConfig, mockSchedule);

    userEvent.click(formFields.replicationEnabled);
    userEvent.click(component.getByRole('button', { name: /save/i }));
    await waitFor(() => {
      expect(toastError).toBeCalled();
      expect(consoleError).toBeCalled();
    });
    expect(backToView).not.toBeCalled();
  });
});
