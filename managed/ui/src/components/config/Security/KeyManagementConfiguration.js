// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Row, Col, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { Field, Formik } from 'formik';
import { toast } from 'react-toastify';
import {
  YBFormInput,
  YBButton,
  YBFormSelect,
  YBCheckBox,
  YBFormDropZone
} from '../../common/forms/fields';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { ListKeyManagementConfigurations } from './ListKeyManagementConfigurations';
import * as Yup from 'yup';

import {
  AWS_REGIONS,
  GCP_KMS_REGIONS,
  GCP_KMS_REGIONS_FLATTENED
} from '../PublicCloud/views/providerRegionsData';
import { readUploadedFile } from '../../../utils/UniverseUtils';
import { change } from 'redux-form';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { isRbacEnabled } from '../../../redesign/features/rbac/common/RbacUtils';
import { getYBAHost } from '../../configRedesign/providerRedesign/utils';
import { YBAHost } from '../../../redesign/helpers/constants';

const awsRegionList = AWS_REGIONS.map((region, index) => {
  return {
    value: region.destVpcRegion,
    label: region.destVpcRegion
  };
});

// TODO: (Daniel) - Replace this hard-coding with an API that returns a list of supported KMS Configurations
let kmsConfigTypes = [
  // Equinix KMS support is deprecated from 2.12.1
  // { value: 'SMARTKEY', label: 'Equinix SmartKey' },
  { value: 'AWS', label: 'AWS KMS' },
  { value: 'HASHICORP', label: 'Hashicorp Vault' },
  { value: 'GCP', label: 'GCP KMS' },
  { value: 'AZU', label: 'Azure KMS' }
];

//GCP KMS
export const PROTECTION_LEVELS = [
  { label: 'HSM (Hardware)', value: 'HSM' },
  { label: 'Software', value: 'SOFTWARE' }
];

const DEFAULT_GCP_LOCATION = GCP_KMS_REGIONS[0].options[0];
const DEFAULT_PROTECTION = PROTECTION_LEVELS[0];

//Azu KMS
const AZU_PROTECTION_ALGOS = [{ label: 'RSA', value: 'RSA' }];
const KEY_SIZES = [
  { label: '2048', value: 2048 },
  { label: '3072', value: 3072 },
  { label: '4096', value: 4096 }
];
const DEFAULT_AZU_PROTECTION_ALGO = AZU_PROTECTION_ALGOS[0];
const DEFAULT_KEY_SIZE = KEY_SIZES[0];

//Form Data
const DEFAULT_FORM_DATA = {
  kmsProvider: kmsConfigTypes[0],
  PROTECTION_LEVEL: DEFAULT_PROTECTION,
  LOCATION_ID: DEFAULT_GCP_LOCATION,
  AZU_KEY_ALGORITHM: DEFAULT_AZU_PROTECTION_ALGO,
  AZU_KEY_SIZE: DEFAULT_KEY_SIZE
};

//HCP KMS
const HcpAuthType = {
  Token: 'TOKEN',
  AppRole: 'APPROLE'
};

export const HCP_AUTHENTICATION_TYPE = [
  { label: 'Token', value: HcpAuthType.Token },
  { label: 'AppRole', value: HcpAuthType.AppRole }
];

const DEFAULT_HCP_AUTHENTICATION_TYPE = HCP_AUTHENTICATION_TYPE[0];

class KeyManagementConfiguration extends Component {
  state = {
    listView: false,
    enabledIAMProfile: false,
    enabledMI: false,
    hcpAuthType: DEFAULT_HCP_AUTHENTICATION_TYPE,
    useCmkPolicy: false,
    mode: 'NEW',
    formData: DEFAULT_FORM_DATA
  };

  isEditMode = () => {
    const { mode } = this.state;
    return mode === 'EDIT';
  };

  isTokenMode = () => {
    const { hcpAuthType } = this.state;
    return hcpAuthType.value === HcpAuthType.Token;
  }

  isAppRoleMode = () => {
    const { hcpAuthType } = this.state;
    return hcpAuthType.value === HcpAuthType.AppRole;
  }

  updateFormField = (field, value) => {
    this.props.dispatch(change('kmsProviderConfigForm', field, value));
  };

  componentDidMount() {
    this.props.fetchKMSConfigList().then((response) => {
      if (isRbacEnabled() || response.payload?.data?.length) {
        this.setState({ listView: true });
      }
    });
    this.props.fetchHostInfo();
    this._ismounted = true;
  }

  componentWillUnmount() {
    this._ismounted = false;
  }

  //recursively monitor task status
  onTaskFailure = (mode) => {
    const message = `Failed to ${mode === 'EDIT' ? 'update' : 'add'} configuration`;
    toast.error(message, { autoClose: 2500 });
  };

  onTaskSuccess = (mode) => {
    const message = `Successfully ${mode === 'EDIT' ? 'updated' : 'added'} the configuration`;
    toast.success(message, { autoClose: 2500 });
    this.props.fetchKMSConfigList();
  };

  monitorTaskStatus = (taskUUID, mode) => {
    this._ismounted &&
      this.props.getCurrentTaskData(taskUUID).then((res) => {
        if (res.error) this.onTaskFailure(mode);
        else {
          const status = res.payload?.data?.status;
          if (status === 'Failure') this.onTaskFailure(mode);
          else if (status === 'Success') this.onTaskSuccess(mode);
          else setTimeout(() => this.monitorTaskStatus(taskUUID, mode), 5000); //recursively check task status
        }
      });
  };

  onEditSubmit = (values) => {
    const { updateKMSConfig } = this.props;
    const { mode, formData } = this.state;
    const { kmsProvider } = values;

    if (kmsProvider) {
      const data = {};

      const isFieldModified = (fieldname) => {
        return values[fieldname] && formData[fieldname] !== values[fieldname];
      };

      const updateConfig = (data) => {
        updateKMSConfig(values.configUUID, data).then((res) => {
          if (res) {
            this.setState({ listView: true, mode: 'NEW', hcpAuthType: DEFAULT_HCP_AUTHENTICATION_TYPE, formData: DEFAULT_FORM_DATA }, () => {
              this.monitorTaskStatus(res.payload.data.taskUUID, mode);
            });
          }
        });
      };

      switch (kmsProvider.value) {
        case 'AWS':
          if (values.AWS_KMS_ENDPOINT) data['AWS_KMS_ENDPOINT'] = values.AWS_KMS_ENDPOINT;

          if (!this.state.enabledIAMProfile) {
            if (isFieldModified('AWS_ACCESS_KEY_ID'))
              data['AWS_ACCESS_KEY_ID'] = values.AWS_ACCESS_KEY_ID;
            if (isFieldModified('AWS_SECRET_ACCESS_KEY'))
              data['AWS_SECRET_ACCESS_KEY'] = values.AWS_SECRET_ACCESS_KEY;
          }

          if (values.cmkPolicyContent) {
            readUploadedFile(values.cmkPolicyContent).then((text) => {
              data['cmk_policy'] = text;
              updateConfig(data);
            });
            return;
          } else if (values.cmk_id) {
            data['cmk_id'] = values.cmk_id;
          }
          break;
        case 'HASHICORP':
          data['HC_VAULT_ADDRESS'] = values.HC_VAULT_ADDRESS;
          if (this.isTokenMode()) {
            if (isFieldModified('HC_VAULT_TOKEN')) data['HC_VAULT_TOKEN'] = values.HC_VAULT_TOKEN;
          } else if (this.isAppRoleMode()) {
            if (isFieldModified('HC_VAULT_ROLE_ID')) data['HC_VAULT_ROLE_ID'] = values.HC_VAULT_ROLE_ID;
            if (isFieldModified('HC_VAULT_SECRET_ID')) data['HC_VAULT_SECRET_ID'] = values.HC_VAULT_SECRET_ID;
            if (isFieldModified('HC_VAULT_AUTH_NAMESPACE')) data['HC_VAULT_AUTH_NAMESPACE'] = values.HC_VAULT_AUTH_NAMESPACE;
          }
          break;
        default:
        case 'SMARTKEY':
          data['base_url'] = values.base_url || 'api.amer.smartkey.io';
          if (isFieldModified('api_key')) data['api_key'] = values.api_key;
          break;
        case 'GCP':
          if (values.GCP_CONFIG) {
            readUploadedFile(values.GCP_CONFIG).then((creds) => {
              try {
                data['GCP_CONFIG'] = { ...JSON.parse(creds) };
                updateConfig(data);
              } catch (e) {
                toast.error('Invalid Config File', { autoClose: 2500 });
              }
            });

            return;
          }
          break;
        case 'AZU':
          if (isFieldModified('CLIENT_ID')) data['CLIENT_ID'] = values.CLIENT_ID;

          if (!this.state.enabledMI) {
            if (isFieldModified('CLIENT_SECRET'))
              data['CLIENT_SECRET'] = values.CLIENT_SECRET;
          }

          if (isFieldModified('TENANT_ID')) data['TENANT_ID'] = values.TENANT_ID;

          break;
      }
      updateConfig(data);
    }
  };

  onSubmit = (values) => {
    const { setKMSConfig } = this.props;
    const { mode } = this.state;
    const { kmsProvider, name } = values;

    if (kmsProvider) {
      const data = { name };

      const createConfig = (data) => {
        setKMSConfig(kmsProvider.value, data).then((res) => {
          if (res) {
            this.setState({ listView: true, hcpAuthType: DEFAULT_HCP_AUTHENTICATION_TYPE }, () => {
              this.monitorTaskStatus(res.payload.data.taskUUID, mode);
            });
          }
        });
      };

      switch (kmsProvider.value) {
        case 'AWS':
          if (values.AWS_KMS_ENDPOINT) data['AWS_KMS_ENDPOINT'] = values.AWS_KMS_ENDPOINT;

          if (!this.state.enabledIAMProfile) {
            data['AWS_ACCESS_KEY_ID'] = values.AWS_ACCESS_KEY_ID;
            data['AWS_SECRET_ACCESS_KEY'] = values.AWS_SECRET_ACCESS_KEY;
          }
          data['AWS_REGION'] = values.region.value;

          if (values.cmkPolicyContent) {
            readUploadedFile(values.cmkPolicyContent).then((text) => {
              data['cmk_policy'] = text;
              createConfig(data);
            });
            return;
          } else if (values.cmk_id) {
            data['cmk_id'] = values.cmk_id;
          }
          break;
        case 'HASHICORP':
          data['HC_VAULT_ADDRESS'] = values.HC_VAULT_ADDRESS;
          if (this.isTokenMode()) {
            data['HC_VAULT_TOKEN'] = values.HC_VAULT_TOKEN;
          } else if (this.isAppRoleMode()) {
            data['HC_VAULT_ROLE_ID'] = values.HC_VAULT_ROLE_ID;
            data['HC_VAULT_SECRET_ID'] = values.HC_VAULT_SECRET_ID;
            data['HC_VAULT_AUTH_NAMESPACE'] = values.HC_VAULT_AUTH_NAMESPACE;
          }
          data['HC_VAULT_KEY_NAME'] = values.HC_VAULT_KEY_NAME
            ? values.HC_VAULT_KEY_NAME
            : 'key_yugabyte';
          data['HC_VAULT_MOUNT_PATH'] = values.HC_VAULT_MOUNT_PATH
            ? values.HC_VAULT_MOUNT_PATH
            : 'transit/';
          data['HC_VAULT_ENGINE'] = 'transit';
          break;
        default:
        case 'SMARTKEY':
          data['base_url'] = values.base_url || 'api.amer.smartkey.io';
          data['api_key'] = values.api_key;
          break;
        case 'GCP':
          if (values.GCP_CONFIG) {
            readUploadedFile(values.GCP_CONFIG).then((creds) => {
              try {
                data['GCP_CONFIG'] = { ...JSON.parse(creds) };
                data['LOCATION_ID'] = values.LOCATION_ID.value;
                data['PROTECTION_LEVEL'] = values.PROTECTION_LEVEL.value;
                data['KEY_RING_ID'] = values.KEY_RING_ID;
                data['CRYPTO_KEY_ID'] = values.CRYPTO_KEY_ID;

                if (values.GCP_KMS_ENDPOINT) data['GCP_KMS_ENDPOINT'] = values.GCP_KMS_ENDPOINT;

                createConfig(data);
              } catch (e) {
                toast.error('Invalid Config File', { autoClose: 2500 });
              }
            });
            return;
          }

          break;
        case 'AZU':
          data['CLIENT_ID'] = values.CLIENT_ID;
          if (!this.state.enabledMI) {
            data['CLIENT_SECRET'] = values.CLIENT_SECRET;
          }
          data['TENANT_ID'] = values.TENANT_ID;
          data['AZU_VAULT_URL'] = values.AZU_VAULT_URL;
          data['AZU_KEY_NAME'] = values.AZU_KEY_NAME;
          data['AZU_KEY_ALGORITHM'] = values.AZU_KEY_ALGORITHM.value;
          data['AZU_KEY_SIZE'] = Number(values.AZU_KEY_SIZE.value);

          break;
      }

      createConfig(data);
    }
  };

  getSmartKeyForm = () => {
    return (
      <Fragment>
        <Row className="config-provider-row" key={'url-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">API Url</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'base_url'}
              component={YBFormInput}
              placeholder={'api.amer.smartkey.io'}
              className={'kube-provider-input-field'}
            />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'private-key-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Secret API Key</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'api_key'}
              component={YBFormInput}
              className={'kube-provider-input-field'}
            />
          </Col>
        </Row>
      </Fragment>
    );
  };

  getAWSForm = (values) => {
    const { hostInfo } = this.props;
    const isEdit = this.isEditMode();
    return (
      <Fragment>
        <Row className="config-provider-row" key={'iam-enable-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Use IAM Profile</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'enableIAMProfile'}
              component={YBCheckBox}
              disabled={hostInfo === undefined || getYBAHost(hostInfo) !== YBAHost.AWS}
              checkState={this.state.enabledIAMProfile ? true : false}
              input={{
                onChange: () => this.setState({ enabledIAMProfile: !this.state.enabledIAMProfile })
              }}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Use IAM Profile"
              content="Select to use an IAM profile attached to an EC2 instance running the platform."
            />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'access-key-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Access Key Id</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'AWS_ACCESS_KEY_ID'}
              component={YBFormInput}
              disabled={this.state.enabledIAMProfile}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip title="Access Key Id" content="Enter your AWS access key ID." />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'secret-key-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Secret Key Id</div>
          </Col>
          <Col lg={7}>
            <Field
              name="AWS_SECRET_ACCESS_KEY"
              component={YBFormInput}
              disabled={this.state.enabledIAMProfile}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip title="Secret Key Id" content="Enter your AWS access key secret." />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'region-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Region</div>
          </Col>
          <Col lg={7}>
            <Field
              name="region"
              component={YBFormSelect}
              options={awsRegionList}
              className={'kube-provider-input-field'}
              isDisabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Region"
              content="Select the AWS region where the customer master key is located."
            />
          </Col>
        </Row>
        <Row className="cmk-id-row" key={'cmk-id-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Customer Master Key ID</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'cmk_id'}
              component={YBFormInput}
              placeholder={'CMK ID'}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Customer Master Key Id"
              content="Enter the identifier for the customer master key. If an identifier is not entered, a CMK ID will be auto-generated."
            />
          </Col>
        </Row>
        <Row className="kms-endpoint-row" key={'kms-endpoint-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">AWS KMS Endpoint (Optional)</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'AWS_KMS_ENDPOINT'}
              component={YBFormInput}
              placeholder={'AWS KMS Endpoint'}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip title="AWS KMS Endpoint (Optional)" content="Enter your AWS KMS Endpoint." />
          </Col>
        </Row>
        {!isEdit && (
          <Row>
            <div className={'bottom-form-field'}>
              <OverlayTrigger
                placement="top"
                overlay={
                  !!values.cmk_id ? (
                    <Tooltip className="high-index">
                      Custom policy file is not needed when Customer Master Key ID is specified.
                    </Tooltip>
                  ) : (
                    <></>
                  )
                }
              >
                <div>
                  <Field
                    component={YBFormDropZone}
                    name={'cmkPolicyContent'}
                    title={'Upload CMK Policy'}
                    disabled={!!values.cmk_id}
                  />
                </div>
              </OverlayTrigger>
            </div>
          </Row>
        )}
      </Fragment>
    );
  };

  getHCVaultForm = () => {
    const isEdit = this.isEditMode();
    const isToken = this.isTokenMode();
    const isAppRole = this.isAppRoleMode();
    return (
      <Fragment>
        <Row className="config-provider-row" key={'v-url-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Vault Address</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'HC_VAULT_ADDRESS'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Vault Address"
              content="Vault Address must be a valid URL with port number, Ex:- http://0.0.0.0:0000"
            />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'v-auth-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Authentication Type</div>
          </Col>
          <Col lg={7}>
            <Field
              name="hcpAuthType"
              component={YBFormSelect}
              options={HCP_AUTHENTICATION_TYPE}
              className={'kube-provider-input-field'}
              defaultValue={DEFAULT_HCP_AUTHENTICATION_TYPE}
              onChange={({ form, field }, option) => {
                this.setState({ hcpAuthType: option });
              }}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip title="Authentication Type" content="The authentication type used to connect to the Hahicorp Vault." />
          </Col>
        </Row>
        {isToken && (
          <Row className="config-provider-row" key={'v-token-field'}>
            <Col lg={3}>
              <div className="form-item-custom-label">Secret Token</div>
            </Col>
            <Col lg={7}>
              <Field
                name={'HC_VAULT_TOKEN'}
                component={YBFormInput}
                className={'kube-provider-input-field'}
              />
            </Col>
          </Row>
        )}
        {isAppRole && (
          <Fragment>
            <Row className="config-provider-row" key={'v-role-id-field'}>
              <Col lg={3}>
                <div className="form-item-custom-label">Role ID</div>
              </Col>
              <Col lg={7}>
                <Field
                  name={'HC_VAULT_ROLE_ID'}
                  component={YBFormInput}
                  className={'kube-provider-input-field'}
                />
              </Col>
              <Col lg={1} className="config-zone-tooltip">
                <YBInfoTip
                  title="Vault Role ID"
                  content="AppRole Role ID Credentials for the provided Auth Namespace"
                />
              </Col>
            </Row>
            <Row className="config-provider-row" key={'v-secret-id-field'}>
              <Col lg={3}>
                <div className="form-item-custom-label">Secret ID</div>
              </Col>
              <Col lg={7}>
                <Field
                  name={'HC_VAULT_SECRET_ID'}
                  component={YBFormInput}
                  className={'kube-provider-input-field'}
                />
              </Col>
              <Col lg={1} className="config-zone-tooltip">
                <YBInfoTip
                  title="Vault Secret ID"
                  content="AppRole Secret ID Credentials for the provided Auth Namespace"
                />
              </Col>
            </Row>
            <Row className="config-provider-row" key={'v-auth-namespace-field'}>
              <Col lg={3}>
                <div className="form-item-custom-label">Auth Namespace (Optional)</div>
              </Col>
              <Col lg={7}>
                <Field
                  name={'HC_VAULT_AUTH_NAMESPACE'}
                  component={YBFormInput}
                  className={'kube-provider-input-field'}
                />
              </Col>
            </Row>
          </Fragment>
        )}
        <Row className="config-provider-row" key={'v-key-name-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Key Name (Optional)</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'HC_VAULT_KEY_NAME'}
              component={YBFormInput}
              placeholder={'key_yugabyte'}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Key Name (Optional)"
              content="Enter the key name. If key name is not specified, it will be auto set to 'key_yugabyte'"
            />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'v-secret-engine-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Secret Engine</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'v_secret_engine'}
              value="transit"
              disabled={true}
              component={YBFormInput}
              className={'kube-provider-input-field'}
            />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'v-mount-path-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Mount Path (Optional)</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'HC_VAULT_MOUNT_PATH'}
              placeholder={'transit/'}
              component={YBFormInput}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Mount Path (Optional)"
              content="Enter the mount path. If mount path is not specified, path will be auto set to 'transit/'"
            />
          </Col>
        </Row>
      </Fragment>
    );
  };

  getGCPForm = () => {
    const isEdit = this.isEditMode();

    return (
      <>
        <Row className="config-provider-row" key={'gcp-creds-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Service Account Credentials</div>
          </Col>
          <Col lg={7}>
            <Field
              component={YBFormDropZone}
              name={'GCP_CONFIG'}
              title={'Upload GCP Credentials (json)'}
              acceptedFiles={['.txt']}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Service Account Credentials"
              content="Service Account Credentials file ( json ), will be used for authentication."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'gcp-loc-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Location</div>
          </Col>
          <Col lg={7}>
            <Field
              name="LOCATION_ID"
              component={YBFormSelect}
              options={GCP_KMS_REGIONS}
              className={'kube-provider-input-field'}
              isDisabled={isEdit}
              value={DEFAULT_GCP_LOCATION}
              defaultValue={DEFAULT_GCP_LOCATION}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Location"
              content="The geographical region where the Cloud KMS resource is stored and accessed."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'gcp-key-ring-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Key Ring Name</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'KEY_RING_ID'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Key Ring Name"
              content="Name of the key ring. If key ring with same name already exists then it will be used, else a new one will be created automatically."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'gcp-crypto-key-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Crypto Key Name</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'CRYPTO_KEY_ID'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Crypto Key Name"
              content="Name of the cryptographic key that will be used for encrypting and decrypting universe key. If crypto key with same name already exists then it will be used, else a new one will be created automatically."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'gcp-protection-level-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Protection Level</div>
          </Col>
          <Col lg={7}>
            <Field
              name="PROTECTION_LEVEL"
              component={YBFormSelect}
              options={PROTECTION_LEVELS}
              className={'kube-provider-input-field'}
              isDisabled={isEdit}
              value={DEFAULT_PROTECTION}
              defaultValue={DEFAULT_PROTECTION}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Protection Level"
              content="Protection level determines how cryptographic operations are performed."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'gcp-endpoint-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">KMS Endpoint (Optional)</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'GCP_KMS_ENDPOINT'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="KMS Endpoint (Optional)"
              content="If GCP KMS has custom endpoint. Must be a valid URL."
            />
          </Col>
        </Row>
      </>
    );
  };

  getAzuForm = () => {
    const { hostInfo } = this.props;
    const isEdit = this.isEditMode();

    return (
      <>
        <Row className="config-provider-row" key={'mi-enable-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Use Managed Identity</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'enableMI'}
              component={YBCheckBox}
              disabled={hostInfo === undefined || getYBAHost(hostInfo) !== YBAHost.AZU}
              checkState={this.state.enabledMI}
              input={{
                onChange: (e) => this.setState({ enabledMI: e.target.checked })
              }}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Use Managed Identity"
              content="Select to use a managed identity attached to an Azu VM instance running the platform."
            />
          </Col>
        </Row>
        <Row className="config-provider-row" key={'azu-client-id-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Client ID</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'CLIENT_ID'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Client ID"
              content="This is the unique identifier of an application registered in the  Azure Active Directory instance."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'azu-client-secret-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Client Secret</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'CLIENT_SECRET'}
              component={YBFormInput}
              disabled={this.state.enabledMI}
              placeholder={''}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Client Secret"
              content="This is the secret key of an application registered in the  Azure Active Directory instance."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'azu-tenant-id-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Tenant ID</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'TENANT_ID'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Tenant ID"
              content="This is the unique identifier of the Azure Active Directory instance."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'azu-key-vault-url-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Key Vault URL</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'AZU_VAULT_URL'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip title="Key Vault URL" content="The key vault URI in the Azure portal." />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'azu-key-name-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Key Name</div>
          </Col>
          <Col lg={7}>
            <Field
              name={'AZU_KEY_NAME'}
              component={YBFormInput}
              placeholder={''}
              className={'kube-provider-input-field'}
              disabled={isEdit}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Key Name"
              content="Name of the master key in the Key Vault. If master key with same name already exists then it will be used, else a new one will be created automatically."
            />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'azu-algo-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Key Algorithm</div>
          </Col>
          <Col lg={7}>
            <Field
              name="AZU_KEY_ALGORITHM"
              component={YBFormSelect}
              options={AZU_PROTECTION_ALGOS}
              className={'kube-provider-input-field'}
              isDisabled={true}
              value={DEFAULT_AZU_PROTECTION_ALGO}
              defaultValue={DEFAULT_AZU_PROTECTION_ALGO}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip title="Key Algorithm" content="The key algorithm used for the master key." />
          </Col>
        </Row>

        <Row className="config-provider-row" key={'azu-key-size-field'}>
          <Col lg={3}>
            <div className="form-item-custom-label">Key Size (bits)</div>
          </Col>
          <Col lg={7}>
            <Field
              name="AZU_KEY_SIZE"
              component={YBFormSelect}
              options={KEY_SIZES}
              className={'kube-provider-input-field'}
              isDisabled={isEdit}
              value={DEFAULT_KEY_SIZE}
              defaultValue={DEFAULT_KEY_SIZE}
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip title="Key Size" content="The size of the master key." />
          </Col>
        </Row>
      </>
    );
  };

  displayFormContent = (provider, values) => {
    if (!provider) {
      return this.getAWSForm(values);
    }
    switch (provider.value) {
      case 'SMARTKEY':
        return this.getSmartKeyForm();
      case 'AWS':
        return this.getAWSForm(values);
      case 'HASHICORP':
        return this.getHCVaultForm();
      case 'GCP':
        return this.getGCPForm();
      case 'AZU':
        return this.getAzuForm();
      default:
        return this.getAWSForm(values);
    }
  };

  openCreateConfigForm = () => {
    this.setState({ listView: false });
  };

  handleEdit = ({ credentials, metadata }) => {
    const formData = { ...credentials, ...metadata };
    const { provider } = metadata;
    const {
      AWS_REGION,
      PROTECTION_LEVEL,
      LOCATION_ID,
      AZU_KEY_ALGORITHM,
      AZU_KEY_SIZE
    } = credentials;
    if (provider) formData.kmsProvider = kmsConfigTypes.find((config) => config.value === provider);
    if (AWS_REGION) formData.region = awsRegionList.find((region) => region.value === AWS_REGION);
    if (PROTECTION_LEVEL)
      formData.PROTECTION_LEVEL = PROTECTION_LEVELS.find(
        (protection) => protection.value === PROTECTION_LEVEL
      );
    if (LOCATION_ID)
      formData.LOCATION_ID = GCP_KMS_REGIONS_FLATTENED.find(
        (region) => region.value === LOCATION_ID
      );

    if (AZU_KEY_ALGORITHM)
      formData.AZU_KEY_ALGORITHM = AZU_PROTECTION_ALGOS.find(
        (algo) => algo.value === AZU_KEY_ALGORITHM
      );

    if (AZU_KEY_SIZE)
      formData.AZU_KEY_SIZE = KEY_SIZES.find((keysize) => keysize.value === AZU_KEY_SIZE);

    this.setState({
      listView: false,
      hcpAuthType: DEFAULT_HCP_AUTHENTICATION_TYPE,
      mode: 'EDIT',
      formData
    });
  };

  deleteAuthConfig = (configUUID) => {
    const { configList, deleteKMSConfig, fetchKMSConfigList } = this.props;
    deleteKMSConfig(configUUID).then(() => {
      if (configList.data.length < 1) this.setState({ listView: false });
      else fetchKMSConfigList();
    });
  };

  /**
   * Shows list view on click of cancel button by turning the listView flag ON.
   */
  showListView = () => {
    this.setState({ listView: true, mode: 'NEW', hcpAuthType: DEFAULT_HCP_AUTHENTICATION_TYPE, formData: DEFAULT_FORM_DATA });
  };

  isValidUrl = (url) => {
    try {
      new URL(url);
    } catch (e) {
      return false;
    }
    return true;
  };

  render() {
    const { configList, featureFlags, currentUserInfo } = this.props;
    const { listView, enabledIAMProfile, formData, enabledMI } = this.state;
    const isAdmin = ['Admin', 'SuperAdmin'].includes(currentUserInfo.role);
    const isEdit = this.isEditMode();
    const isToken = this.isTokenMode();
    const isAppRole = this.isAppRoleMode();

    if (getPromiseState(configList).isInit() || getPromiseState(configList).isLoading()) {
      return <YBLoadingCircleIcon />;
    } else {
      //feature flagging
      const isHCVaultEnabled =
        featureFlags.test.enableHCVault || featureFlags.released.enableHCVault;
      const isGcpKMSEnabled = featureFlags.test.enableGcpKMS || featureFlags.released.enableGcpKMS;
      const isAzuKMSEnabled = featureFlags.test.enableAzuKMS || featureFlags.released.enableAzuKMS;

      let configs = configList.data;
      if (isHCVaultEnabled || isGcpKMSEnabled || isAzuKMSEnabled) {
        kmsConfigTypes = kmsConfigTypes.filter((config) => {
          return (
            !['HASHICORP', 'GCP', 'AZU'].includes(config.value) ||
            (config.value === 'HASHICORP' && isHCVaultEnabled) ||
            (config.value === 'GCP' && isGcpKMSEnabled) ||
            (config.value === 'AZU' && isAzuKMSEnabled)
          );
        });
        configs = configs
          ? configs.filter((config) => {
            return (
              !['HASHICORP', 'GCP', 'AZU'].includes(config.metadata.provider) ||
              (config.metadata.provider === 'HASHICORP' && isHCVaultEnabled) ||
              (config.metadata.provider === 'GCP' && isGcpKMSEnabled) ||
              (config.metadata.provider === 'AZU' && isAzuKMSEnabled)
            );
          })
          : [];
      }
      //feature flagging

      if (listView) {
        return (
          <ListKeyManagementConfigurations
            configs={configs}
            onCreate={this.openCreateConfigForm}
            onDelete={this.deleteAuthConfig}
            onEdit={this.handleEdit}
            isAdmin={isAdmin}
          />
        );
      }

      const validationSchema = Yup.object().shape({
        name: Yup.string().required('Name is Required'),
        kmsProvider: Yup.object().required('Provider name is Required'),
        //Smart Key
        api_key: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'SMARTKEY',
          then: Yup.mixed().required('API key is Required')
        }),
        base_url: Yup.string(),

        //Aws KMS
        AWS_ACCESS_KEY_ID: Yup.string().when('kmsProvider', {
          is: (provider) => provider?.value === 'AWS' && !enabledIAMProfile,
          then: Yup.string().required('Access Key ID is Required')
        }),

        AWS_SECRET_ACCESS_KEY: Yup.string().when('kmsProvider', {
          is: (provider) => provider?.value === 'AWS' && !enabledIAMProfile,
          then: Yup.string().required('Secret Key ID is Required')
        }),

        region: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AWS',
          then: Yup.mixed().required('AWS Region is Required')
        }),

        cmkPolicyContent: Yup.string(),
        cmk_id: Yup.string(),

        // HC Vault
        HC_VAULT_ADDRESS: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'HASHICORP',
          then: Yup.string()
            .matches(/^(?:http(s)?:\/\/)?[\w.-]+(?:[\w-]+)+:\d+/, {
              message: 'Vault Address must be a valid URL with port number'
            })
            .required('Vault Address is Required')
        }),

        HC_VAULT_TOKEN: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'HASHICORP' && isToken,
          then: Yup.mixed().required('Secret Token is Required')
        }),

        HC_VAULT_ROLE_ID: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'HASHICORP' && isAppRole,
          then: Yup.mixed().required('Role ID is Required')
        }),

        HC_VAULT_SECRET_ID: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'HASHICORP' && isAppRole,
          then: Yup.mixed().required('Secret ID is Required')
        }),

        //GCP KMS
        GCP_CONFIG: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'GCP',
          then: Yup.mixed().required('GCP Credentials are Required')
        }),
        LOCATION_ID: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'GCP',
          then: Yup.object().required('Location is Required')
        }),
        PROTECTION_LEVEL: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'GCP',
          then: Yup.object().required('Protection Level is Required')
        }),
        KEY_RING_ID: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'GCP',
          then: Yup.string().required('Key Ring Name is Required')
        }),
        CRYPTO_KEY_ID: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'GCP',
          then: Yup.string().required('Crypto Key Name is Required')
        }),
        GCP_KMS_ENDPOINT: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'GCP',
          then: Yup.string().test(
            'is-url-valid',
            'GCP KMS Custom Endpoint must be a valid URL',
            (value) => !value || this.isValidUrl(value) //not a required field
          )
        }),

        //Azu KMS
        CLIENT_ID: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AZU',
          then: Yup.mixed().required('Client ID is required')
        }),
        CLIENT_SECRET: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AZU' && !enabledMI,
          then: Yup.string().required('Client Secret is Required')
        }),
        TENANT_ID: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AZU',
          then: Yup.string().required('Tenant ID is Required')
        }),
        AZU_KEY_NAME: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AZU',
          then: Yup.string().required('Key Name is Required')
        }),
        AZU_KEY_ALGORITHM: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AZU',
          then: Yup.object().required('Key Algorithm is Required')
        }),
        AZU_KEY_SIZE: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AZU',
          then: Yup.object().required('Key Size is Required')
        }),
        AZU_VAULT_URL: Yup.mixed().when('kmsProvider', {
          is: (provider) => provider?.value === 'AZU',
          then: Yup.string()
            .required('Key Vault URL is Required')
            .test('is-url-valid', 'Key Vault URL must be a valid URL', (value) =>
              this.isValidUrl(value)
            )
        })
      });

      return (
        <div className="provider-config-container">
          <Formik
            initialValues={formData}
            validationSchema={validationSchema}
            onSubmit={(values) => {
              isEdit ? this.onEditSubmit(values) : this.onSubmit(values);
            }}
          >
            {({ handleSubmit, values, touched }) => {
              const isSaveDisabled = isEdit && !Object.keys(touched).length;
              return (
                <form onSubmit={handleSubmit}>
                  <Row>
                    <Col lg={8}>
                      <Row className="config-name-row" key={'name-field'}>
                        <Col lg={3}>
                          <div className="form-item-custom-label">Configuration Name</div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name={'name'}
                            component={YBFormInput}
                            placeholder={'Configuration Name'}
                            className={'kube-provider-input-field'}
                            disabled={isEdit}
                          />
                        </Col>
                        <Col lg={1} className="config-zone-tooltip">
                          <YBInfoTip
                            title="Confriguration Name"
                            content="The name of the KMS configuration (Required)."
                          />
                        </Col>
                      </Row>
                      <Row className="config-provider-row" key={'provider-field'}>
                        <Col lg={3}>
                          <div className="form-item-custom-label">KMS Provider</div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name="kmsProvider"
                            placeholder="Provider name"
                            component={YBFormSelect}
                            options={kmsConfigTypes}
                            className={'kube-provider-input-field'}
                            isDisabled={isEdit}
                          />
                        </Col>
                      </Row>
                      {this.displayFormContent(values.kmsProvider, values)}
                    </Col>
                  </Row>
                  <div className="form-action-button-container">
                    <YBButton
                      disabled={isSaveDisabled}
                      btnText="Save"
                      btnClass="btn btn-orange"
                      btnType="submit"
                    />
                    <YBButton btnText="Cancel" btnClass="btn" onClick={this.showListView} />
                  </div>
                </form>
              );
            }}
          </Formik>
        </div>
      );
    }
  }
}
export default KeyManagementConfiguration;
