// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Row, Col } from 'react-bootstrap';
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

import { regionsData } from '../PublicCloud/views/providerRegionsData';
import { readUploadedFile } from '../../../utils/UniverseUtils';
import { change } from 'redux-form';
import YBInfoTip from '../../common/descriptors/YBInfoTip';

const awsRegionList = regionsData.map((region, index) => {
  return {
    value: region.destVpcRegion,
    label: region.destVpcRegion
  };
});

// TODO: (Daniel) - Replace this hard-coding with an API that returns a list of supported KMS Configurations
let kmsConfigTypes = [
  { value: 'SMARTKEY', label: 'Equinix SmartKey' },
  { value: 'AWS', label: 'AWS KMS' },
  { value: 'HASHICORP', label: 'Hashicorp Vault' }
];

class KeyManagementConfiguration extends Component {
  state = {
    listView: false,
    enabledIAMProfile: false,
    useCmkPolicy: false,
    mode: 'NEW',
    formData: {}
  };

  isEditMode = () => {
    const { mode } = this.state;
    return mode === 'EDIT';
  };

  updateFormField = (field, value) => {
    this.props.dispatch(change('kmsProviderConfigForm', field, value));
  };

  componentDidMount() {
    this.props.fetchKMSConfigList().then((response) => {
      if (response.payload?.data?.length) {
        this.setState({ listView: true });
      }
    });
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
            this.setState({ listView: true, mode: 'NEW', formData: {} }, () => {
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
          if (isFieldModified('HC_VAULT_TOKEN')) data['HC_VAULT_TOKEN'] = values.HC_VAULT_TOKEN;
          break;
        default:
        case 'SMARTKEY':
          data['base_url'] = values.base_url || 'api.amer.smartkey.io';
          if (isFieldModified('api_key')) data['api_key'] = values.api_key;
          break;
      }
      updateConfig(data);
    }
  };

  onSubmit = (values) => {
    const { setKMSConfig } = this.props;
    const { mode } = this.state;
    const { kmsProvider } = values;

    if (kmsProvider) {
      const data = { name: values.name };

      const createConfig = (data) => {
        setKMSConfig(kmsProvider.value, data).then((res) => {
          if (res) {
            this.setState({ listView: true }, () => {
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
          data['HC_VAULT_TOKEN'] = values.HC_VAULT_TOKEN;
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

  getAWSForm = () => {
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
            <div className="form-item-custom-label">AWS KMS Endpoint</div>
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
            <YBInfoTip title="AWS KMS Endpoint" content="Enter your AWS KMS Endpoint." />
          </Col>
        </Row>
        <Row>
          <div className={'bottom-form-field'}>
            <Field
              component={YBFormDropZone}
              name={'cmkPolicyContent'}
              title={'Upload CMK Policy'}
              className="upload-file-button"
            />
          </div>
        </Row>
      </Fragment>
    );
  };

  getHCVaultForm = () => {
    const isEdit = this.isEditMode();
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
            />
          </Col>
          <Col lg={1} className="config-zone-tooltip">
            <YBInfoTip
              title="Vault Address"
              content="Vault Address must be a valid URL with port number, Ex:- http://0.0.0.0:0000"
            />
          </Col>
        </Row>
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
            <div className="form-item-custom-label">Mount Path</div>
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
              title="Mount Path"
              content="Enter the mount path. If mount path is not specified, path will be auto set to 'transit/'"
            />
          </Col>
        </Row>
      </Fragment>
    );
  };

  displayFormContent = (provider) => {
    if (!provider) {
      return this.getSmartKeyForm();
    }
    switch (provider.value) {
      case 'SMARTKEY':
        return this.getSmartKeyForm();
      case 'AWS':
        return this.getAWSForm();
      case 'HASHICORP':
        return this.getHCVaultForm();
      default:
        return this.getSmartKeyForm();
    }
  };

  openCreateConfigForm = () => {
    this.setState({ listView: false });
  };

  handleEdit = ({ credentials, metadata }) => {
    const formData = { ...credentials, ...metadata };
    const { provider } = metadata;
    const { AWS_REGION } = credentials;
    if (provider) formData.kmsProvider = kmsConfigTypes.find((config) => config.value === provider);
    if (AWS_REGION) formData.region = awsRegionList.find((region) => region.value === AWS_REGION);

    this.setState({
      listView: false,
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
    this.setState({ listView: true, mode: 'NEW', formData: {} });
  };

  render() {
    const { configList, featureFlags, currentUserInfo } = this.props;
    const { listView, enabledIAMProfile, formData } = this.state;
    const isAdmin = ['Admin', 'SuperAdmin'].includes(currentUserInfo.role);
    const isHCVaultEnabled = featureFlags.test.enableHCVault || featureFlags.released.enableHCVault;
    if (!isHCVaultEnabled)
      kmsConfigTypes = kmsConfigTypes.filter((config) => config.value !== 'HASHICORP');

    const isEdit = this.isEditMode();

    if (getPromiseState(configList).isInit() || getPromiseState(configList).isLoading()) {
      return <YBLoadingCircleIcon />;
    }
    if (listView) {
      return (
        <ListKeyManagementConfigurations
          configs={configList}
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
      base_url: Yup.string(),
      api_key: Yup.mixed().when('kmsProvider', {
        is: (provider) => provider?.value === 'SMARTKEY',
        then: Yup.mixed().required('API key is Required')
      }),

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

      HC_VAULT_ADDRESS: Yup.mixed().when('kmsProvider', {
        is: (provider) => provider?.value === 'HASHICORP',
        then: Yup.string()
          .matches(/^(?:http(s)?:\/\/)?[\w.-]+(?:[\w-]+)+:\d+/, {
            message: 'Vault Address must be a valid URL with port number'
          })
          .required('Vault Address is Required')
      }),

      HC_VAULT_TOKEN: Yup.mixed().when('kmsProvider', {
        is: (provider) => provider?.value === 'HASHICORP',
        then: Yup.mixed().required('Secret Token is Required')
      }),

      cmkPolicyContent: Yup.string(),
      cmk_id: Yup.string()
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
                    {this.displayFormContent(values.kmsProvider)}
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
export default KeyManagementConfiguration;
