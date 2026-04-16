import _ from 'lodash';
import * as Yup from 'yup';
import { useState } from 'react';
import { Field, Form, Formik } from 'formik';
import { Row, Col } from 'react-bootstrap';
import {
  YBButton,
  YBControlledNumericInput,
  YBFormInput,
  YBToggle
} from '../../../common/forms/fields';
import { AzureRegions } from './AzureRegions';
import YBInfoTip from '../../../common/descriptors/YBInfoTip';
import { FIELD_TYPE, NTPConfig, NTP_TYPES } from './NTPConfig';
import { ACCEPTABLE_CHARS } from '../../constants';

const initialValues = {
  providerName: '', // not a part of config payload
  networkSetup: 'existing_vpc', // not a part of config payload
  AZURE_CLIENT_ID: '',
  AZURE_CLIENT_SECRET: '',
  AZURE_TENANT_ID: '',
  AZURE_SUBSCRIPTION_ID: '',
  AZURE_RG: '',
  AZURE_NETWORK_SUBSCRIPTION_ID: '',
  AZURE_NETWORK_RG: '',
  ntp_option: NTP_TYPES.PROVIDER,
  ntpServers: [],
  setUpChrony: true,
  airGapInstall: false
};

const validationSchema = Yup.object().shape({
  providerName: Yup.string()
    .required('Provider Name is a required field')
    .matches(ACCEPTABLE_CHARS, 'Provider Name cannot contain special characters except - and _'),
  AZURE_CLIENT_ID: Yup.string().required('Azure Client ID is a required field'),
  AZURE_CLIENT_SECRET: Yup.string().required('Azure Client Secret is a required field'),
  AZURE_TENANT_ID: Yup.string().required('Azure Tenant ID is a required field'),
  AZURE_SUBSCRIPTION_ID: Yup.string().required('Azure Subscription ID is a required field'),
  AZURE_RG: Yup.string().required('Azure Resource Group is a required field'),
  AZURE_NETWORK_SUBSCRIPTION_ID: Yup.string(),
  AZURE_NETWORK_RG: Yup.string(),
  sshPort: Yup.number(),
  sshUser: Yup.string(),
  ntpServers: Yup.array().when('ntp_option', {
    is: NTP_TYPES.MANUAL,
    then: Yup.array().min(1, 'NTP servers cannot be empty')
  })
});

const convertFormDataToPayload = (formData) => {
  const perRegionMetadata = {};

  formData.forEach((regionItem) => {
    const azToSubnetIds = {};
    regionItem.azToSubnetIds.forEach((zoneItem) => {
      if (zoneItem.zone?.value) {
        azToSubnetIds[zoneItem.zone?.value] = zoneItem.subnet;
      }
    });

    perRegionMetadata[regionItem.region.value] = {
      vpcId: regionItem.vpcId,
      customSecurityGroupId: regionItem.customSecurityGroupId,
      customImageId: regionItem.customImageId,
      azToSubnetIds
    };
  });

  return { perRegionMetadata };
};

export const AzureProviderInitView = ({ createAzureProvider, isBack, onBack }) => {
  const [regionsFormData, setRegionsFormData] = useState([]);

  const createProviderConfig = (values) => {
    const config = _.omit(
      values,
      'providerName',
      'networkSetup',
      'sshPort',
      'sshUser',
      'ntpServers',
      'ntp_option',
      'setUpChrony',
      'airGapInstall'
    );
    const regions = convertFormDataToPayload(regionsFormData);
    if (values['sshPort']) {
      regions['sshPort'] = values['sshPort'];
    }
    if (values['sshUser']) {
      regions['sshUser'] = values['sshUser'];
    }
    regions['ntpServers'] = values['ntpServers'];
    regions['setUpChrony'] = values['setUpChrony'];
    regions['airGapInstall'] = values['airGapInstall'];
    createAzureProvider(values.providerName, config, regions);
  };

  return (
    <div className="provider-config-container">
      <Formik
        initialValues={initialValues}
        validationSchema={validationSchema}
        onSubmit={createProviderConfig}
      >
        {({ isValid, setFieldValue }) => (
          <Form>
            <Row>
              <Col lg={10}>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Provider Name</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="providerName"
                      placeholder="Provider Name"
                      component={YBFormInput}
                    />
                  </Col>
                </Row>


                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Subscription ID</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="AZURE_SUBSCRIPTION_ID"
                      placeholder="Subscription ID"
                      component={YBFormInput}
                    />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="The subscription ID is a unique ID that uniquely identifies your subscription to use Azure services."
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Resource Group</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="AZURE_RG" placeholder="Resource Group" component={YBFormInput} />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="Azure resource group includes those resources that you want to manage as a group."
                    />
                  </Col>
                </Row>

                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Network Subscription ID</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="AZURE_NETWORK_SUBSCRIPTION_ID"
                      placeholder="Network Subscription ID"
                      component={YBFormInput}
                    />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="The network subscription ID is a unique ID that uniquely identifies your subscription to use Azure network services."
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Network Resource Group</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="AZURE_NETWORK_RG" placeholder="Network Resource Group" component={YBFormInput} />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="Azure netowrk resource group includes those resources that you want to manage as a group."
                    />
                  </Col>
                </Row>


                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Tenant ID</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="AZURE_TENANT_ID" placeholder="Tenant ID" component={YBFormInput} />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="This is the unique identifier of the Azure Active Directory instance. "
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">SSH Port</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="sshPort" type="number">
                      {({ field, form: { setFieldValue } }) => (
                        <YBControlledNumericInput
                          name="sshPort"
                          input={{
                            placeholder: 'SSH Port'
                          }}
                          val={field.value}
                          onInputChanged={(valAsNum) => setFieldValue('sshPort', valAsNum)}
                        />
                      )}
                    </Field>
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="SSH Port"
                      content="Which port should YugaWare open and connect to?"
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">SSH User</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="sshUser" placeholder="SSH User" component={YBFormInput} />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip title="SSH User" content="Custom SSH User." />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Client ID</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="AZURE_CLIENT_ID" placeholder="Client ID" component={YBFormInput} />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="This is the unique identifier of an application registered in the  Azure Active Directory instance."
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Client Secret</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="AZURE_CLIENT_SECRET"
                      placeholder="Client Secret"
                      component={YBFormInput}
                      type="password"
                      autocomplete="new-password"
                    />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="This is the secret key of an application registered in the  Azure Active Directory instance."
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Private DNS Zone</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="HOSTED_ZONE_ID"
                      placeholder="Private DNS Zone"
                      component={YBFormInput}
                    />
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="This is the unique identifier of an Azure private DNS zone."
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Virtual Network Setup</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="networkSetup">
                      {({ field }) => (
                        <select
                          name="networkSetup"
                          className="form-control"
                          value={field.value}
                          onChange={field.onChange}
                        >
                          <option key={1} value="new_vpc" disabled={true}>
                            Create a new Virtual Network
                          </option>
                          <option key={2} value="existing_vpc">
                            Specify an existing Virtual Network
                          </option>
                        </select>
                      )}
                    </Field>
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Azure Config"
                      content="An Azure Virtual Network (VNet) is a representation of your own network in the cloud. It is a logical isolation of the Azure cloud dedicated to your subscription."
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Air Gap Installation</div>
                  </Col>
                  <Col lg={1}>
                    <Field name="airGapInstall">
                      {({ field }) => (
                        <YBToggle
                          name="airGapInstall"
                          input={{
                            value: field.value,
                            onChange: field.onChange
                          }}
                          defaultChecked={false}
                        />
                      )}
                    </Field>
                  </Col>
                  <Col lg={1} className="config-provider-tooltip">
                    <YBInfoTip
                      title="Air Gap Installation"
                      content="Would you like YugaWare to create instances in air gap mode for your universes?"
                    />
                  </Col>
                </Row>

                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">NTP Setup</div>
                  </Col>
                  <Col lg={7}>
                    <NTPConfig onChange={setFieldValue} fieldType={FIELD_TYPE.FORMIK} hideHelp />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={10}>
                    <AzureRegions
                      regions={regionsFormData}
                      onChange={(value) => setRegionsFormData(value)}
                    />
                  </Col>
                </Row>
              </Col>
            </Row>
            <div className="form-action-button-container">
              <YBButton
                btnType="submit"
                btnClass="btn btn-default save-btn"
                btnText="Save"
                disabled={!isValid}
              />
              {isBack && <YBButton onClick={onBack} btnText={'Back'} btnClass="btn btn-default" />}
            </div>
          </Form>
        )}
      </Formik>
    </div>
  );
};
