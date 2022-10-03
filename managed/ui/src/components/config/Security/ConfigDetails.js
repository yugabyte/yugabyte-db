import React from 'react';
import moment from 'moment';
import { YBModal } from '../../common/forms/fields';

export const ConfigDetails = ({ data, visible, onHide }) => {
  const {
    credentials,
    metadata: { name, provider }
  } = data;

  //get details by provider
  const getForAWS = () => {
    const {
      AWS_ACCESS_KEY_ID,
      AWS_SECRET_ACCESS_KEY,
      AWS_REGION,
      cmk_id,
      AWS_KMS_ENDPOINT
    } = credentials;
    const data = [
      {
        label: 'Access Key ID',
        value: AWS_ACCESS_KEY_ID
      },
      {
        label: 'Secret Key',
        value: AWS_SECRET_ACCESS_KEY
      },
      {
        label: 'Region',
        value: AWS_REGION
      },
      {
        label: 'Customer Master Key ID',
        value: cmk_id
      },
      {
        label: 'AWS KMS Endpoint',
        value: AWS_KMS_ENDPOINT
      }
    ];
    return data;
  };

  const getForSmartKey = () => {
    const { base_url, api_key } = credentials;
    const data = [
      {
        label: 'API Url',
        value: base_url
      },
      {
        label: 'Secret Key',
        value: api_key
      }
    ];
    return data;
  };

  const getForHashicorp = () => {
    const {
      HC_VAULT_ADDRESS,
      HC_VAULT_TOKEN,
      HC_VAULT_ENGINE,
      HC_VAULT_MOUNT_PATH,
      HC_VAULT_TTL,
      HC_VAULT_TTL_EXPIRY
    } = credentials;
    const data = [
      {
        label: 'Vault Address',
        value: HC_VAULT_ADDRESS
      },
      {
        label: 'Secret Token',
        value: HC_VAULT_TOKEN
      },
      {
        label: 'Secret Engine',
        value: HC_VAULT_ENGINE
      },
      {
        label: 'Mount Path',
        value: HC_VAULT_MOUNT_PATH
      },
      {
        label: 'Expiry',
        value:
          HC_VAULT_TTL && HC_VAULT_TTL_EXPIRY
            ? moment(HC_VAULT_TTL_EXPIRY).format('DD MMMM YYYY')
            : 'Wont Expire'
      }
    ];
    return data;
  };

  const getForGCP = () => {
    const {
      CRYPTO_KEY_ID,
      KEY_RING_ID,
      LOCATION_ID,
      GCP_CONFIG: { client_email }
    } = credentials;
    const data = [
      {
        label: 'Service Account Email',
        value: client_email
      },
      {
        label: 'Location',
        value: LOCATION_ID
      },
      {
        label: 'Key Ring Name',
        value: KEY_RING_ID
      },
      {
        label: 'Crypto Key Name',
        value: CRYPTO_KEY_ID
      }
    ];
    return data;
  };

  const getForAzure = () => {
    const {
      CLIENT_ID,
      CLIENT_SECRET,
      TENANT_ID,
      AZU_VAULT_URL,
      AZU_KEY_NAME,
      AZU_KEY_ALGORITHM,
      AZU_KEY_SIZE
    } = credentials;
    const data = [
      {
        label: 'Client ID',
        value: CLIENT_ID
      },
      {
        label: 'Client Secret',
        value: CLIENT_SECRET
      },
      {
        label: 'Tenant ID',
        value: TENANT_ID
      },
      {
        label: 'Key Vault URL',
        value: AZU_VAULT_URL
      },
      {
        label: 'Key Name',
        value: AZU_KEY_NAME
      },
      {
        label: 'Key Algorithm',
        value: AZU_KEY_ALGORITHM
      },
      {
        label: 'Key Size (bits)',
        value: AZU_KEY_SIZE
      }
    ];
    return data;
  };

  const getDetails = () => {
    if (provider === 'AWS') return getForAWS();
    if (provider === 'SMARTKEY') return getForSmartKey();
    if (provider === 'GCP') return getForGCP();
    if (provider === 'AZU') return getForAzure();

    return getForHashicorp();
  };

  const BASIC_DETAILS = [
    {
      label: 'Name',
      value: name
    },
    {
      label: 'Provider',
      value: provider
    }
  ];
  const MORE_DETAILS = getDetails();

  return (
    <div className="cert-details-modal">
      <YBModal
        title={'Configuration Details'}
        visible={visible}
        onHide={onHide}
        submitLabel={'Close'}
        onFormSubmit={onHide}
      >
        <ul className="cert-details-modal__list">
          {[...BASIC_DETAILS, ...MORE_DETAILS]
            .filter(({ value }) => !!value)
            .map(({ label, value }) => (
              <li key={label}>
                <label>{label}</label>
                <div>{value}</div>
              </li>
            ))}
        </ul>
      </YBModal>
    </div>
  );
};
