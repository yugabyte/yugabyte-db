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

  const getDetails = () => {
    if (provider === 'AWS') return getForAWS();
    if (provider === 'SMARTKEY') return getForSmartKey();

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
