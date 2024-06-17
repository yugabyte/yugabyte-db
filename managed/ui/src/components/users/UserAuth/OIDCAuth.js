// Copyright (c) YugaByte, Inc.

import { useEffect, useState } from 'react';
import * as Yup from 'yup';
import clsx from 'clsx';
import { useQuery, useQueryClient } from 'react-query';
import { trimStart, trimEnd, isString, find, isEqual, differenceWith } from 'lodash';
import { toast } from 'react-toastify';
import { Row, Col, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Formik, Form, Field } from 'formik';
import { YBFormInput, YBButton, YBModal, YBToggle } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { deleteOIDCMappingByGroupName, getOIDCMappings, saveOIDCMappings } from './OIDCApi';

import YBInfoTip from '../../common/descriptors/YBInfoTip';
import OIDCMetadataModal from './OIDCMetadataModal';
import { setSSO, setShowJWTTokenInfo } from '../../../config';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { Action } from '../../../redesign/features/rbac';
import { YBA_ROLES } from './LDAPAuth';
import { OIDCMappingModal } from './OIDCGroupMapping';
import { getAllRoles } from '../../../redesign/features/rbac/api';

import { YBLoadingCircleIcon } from '../../common/indicators';
import WarningIcon from '../icons/warning_icon';
import Mapping from '../icons/mapping_icon';
import Pencil from '../icons/pencil_icon';

const VALIDATION_SCHEMA = Yup.object().shape({
  discoveryURI: Yup.string().required('Discovery URL is required'),
  clientID: Yup.string().required('Client ID is required'),
  secret: Yup.string().required('Client Secret is required')
});

const OIDC_PATH = 'yb.security';
const OIDC_FIELDS = [
  'use_oauth',
  'type',
  'clientID',
  'secret',
  'discoveryURI',
  'oidcProviderMetadata',
  'oidcScope',
  'oidcEmailAttribute',
  'showJWTInfoOnLogin',
  'oidcRefreshTokenEndpoint',
  'oidc_default_role'
];

const TOAST_OPTIONS = { autoClose: 1750 };

export const OIDCAuth = (props) => {
  const {
    fetchRunTimeConfigs,
    setRunTimeConfig,
    deleteRunTimeConfig,
    runtimeConfigs
  } = props;
  const [showToggle, setToggleVisible] = useState(false);
  const [dialog, showDialog] = useState(false);
  const [showJWTTokenToggle, setShowJWTTokenToggle] = useState(false);
  const [oidcEnabled, setOIDC] = useState(false);
  const [OIDCMetadata, setOIDCMetadata] = useState(null);
  const [showMetadataModel, setShowMetadataModal] = useState(false);

  let submitAction = undefined;

  const configEntries = runtimeConfigs?.data?.configEntries ?? [];
  const transformData = (values) => {
    const escStr = values.oidcProviderMetadata
      ? values.oidcProviderMetadata.replace(/[\r\n]/gm, '')
      : null;
    const str = JSON.stringify(JSON.parse(escStr));
    const transformedData = {
      ...values,
      oidcProviderMetadata: values.oidcProviderMetadata ? '""' + str + '""' : '',
      type: 'OIDC'
    };

    return transformedData;
  };

  const escapeStr = (str) => {
    let s = trimStart(str, '""');
    s = trimEnd(s, '""');
    return s;
  };

  const resetFormValues = () => {
    const oidcFields = OIDC_FIELDS.map((ef) => `${OIDC_PATH}.${ef}`);
    const oidcConfigs = configEntries.filter((config) => oidcFields.includes(config.key));
    const formData = oidcConfigs.reduce((fData, config) => {
      const [, key] = config.key.split(`${OIDC_PATH}.`);
      fData[key] = '';
      if (key === 'showJWTInfoOnLogin') {
        fData[key] = false;
      }
      return fData;
    }, {});

    const finalFormData = {
      ...formData
    };

    return finalFormData;
  };

  const initializeFormValues = () => {
    const oidcFields = OIDC_FIELDS.map((ef) => `${OIDC_PATH}.${ef}`);
    const oidcConfigs = configEntries.filter((config) => oidcFields.includes(config.key));
    const formData = oidcConfigs.reduce((fData, config) => {
      const [, key] = config.key.split(`${OIDC_PATH}.`);
      if (key === 'oidcProviderMetadata') {
        const escapedStr = config.value ? escapeStr(config.value).replace(/\\/g, '') : '';
        fData[key] = escapedStr ? JSON.stringify(JSON.parse(escapedStr), null, 2) : '';
      } else {
        fData[key] = escapeStr(config.value);
      }
      if (key === 'showJWTInfoOnLogin') {
        fData[key] = config.value === 'true';
      }
      return fData;
    }, {});

    const finalFormData = {
      ...formData
    };

    return finalFormData;
  };

  const saveOIDCConfigs = async (values) => {
    const formValues = transformData(values);
    const initValues = initializeFormValues();
    const promiseArray = Object.keys(formValues).reduce((promiseArr, key) => {
      if (formValues[key] !== initValues[key]) {
        const keyName = `${OIDC_PATH}.${key}`;
        const value = key !== "oidc_default_role" && isString(formValues[key]) ? `"${formValues[key]}"` : formValues[key];
        promiseArr.push(
          formValues[key] !== ''
            ? setRunTimeConfig({
              key: keyName,
              value
            })
            : deleteRunTimeConfig({
              key: keyName
            })
        );
      }

      return promiseArr;
    }, []);

    try {
      toast.warn('Please wait. OIDC configuration is getting saved', TOAST_OPTIONS);
      await Promise.all(promiseArray);

      //set oauth after all other configs are saved - to avoid other configs fail
      const response = await setRunTimeConfig({
        key: `${OIDC_PATH}.use_oauth `,
        value: true
      });
      setSSO(true);
      response && fetchRunTimeConfigs();
      toast.success('OIDC configuration is saved successfully', TOAST_OPTIONS);
    } catch {
      toast.error('Failed to save OIDC configuration', TOAST_OPTIONS);
    }
  };

  const handleToggle = async (e) => {
    const value = e.target.checked;

    if (!value) showDialog(true);
    else {
      setOIDC(true);
      await setRunTimeConfig({
        key: `${OIDC_PATH}.use_oauth`,
        value: true
      });
      setSSO(true);
      fetchRunTimeConfigs();
      toast.success(`OIDC authentication is enabled`, TOAST_OPTIONS);
    }
  };

  const handleDialogClose = () => {
    showDialog(false);
  };

  const queryClient = useQueryClient();

  const doSaveOIDCMappings = async () => {
    const toDelete = differenceWith(oidc_mappings, mappingData, isEqual);

    if (toDelete.length > 0) {
      await Promise.all(toDelete.map(t => deleteOIDCMappingByGroupName(t.groupName)));
    }
    if (mappingData.length > 0) {
      await saveOIDCMappings(mappingData);
    }

    queryClient.invalidateQueries("oidc_mappings");
  };

  const handleDialogSubmit = async () => {
    setOIDC(false);
    showDialog(false);
    await setRunTimeConfig({
      key: `${OIDC_PATH}.use_oauth`,
      value: 'false'
    });
    await setRunTimeConfig({
      key: `${OIDC_PATH}.showJWTInfoOnLogin`,
      value: 'false'
    });
    setSSO(false);
    setShowJWTTokenInfo(false);
    fetchRunTimeConfigs();
    toast.warn(`OIDC authentication is disabled`, TOAST_OPTIONS);
  };

  useEffect(() => {
    const oidcConfig = configEntries.find((config) =>
      config.key.includes(`${OIDC_PATH}.use_oauth`)
    );
    const isOIDCEnhancementEnabled =
      configEntries.find((c) => c.key === `${OIDC_PATH}.oidc_feature_enhancements`)?.value ===
      'true';
    setShowJWTTokenToggle(isOIDCEnhancementEnabled);
    setToggleVisible(!!oidcConfig);
    setOIDC(escapeStr(oidcConfig?.value) === 'true');
  }, [configEntries, setToggleVisible, setOIDC]);

  const isOIDCMappingEnabled = configEntries.find((config) => config.key === 'yb.security.oidc_enable_auto_create_users')?.value === "true";

  const { data: roles } = useQuery('roles', getAllRoles, {
    select: (data) => data.data ?? [],
    enabled: getPromiseState(runtimeConfigs).isSuccess() && isOIDCMappingEnabled,
    refetchOnMount: false
  });

  const { data: oidc_mappings, isFetching } = useQuery('oidc_mappings', getOIDCMappings, {
    select: resp => resp.data.oidcGroupToYbaRolesPairs,
    enabled: isOIDCMappingEnabled,
    refetchOnMount: false
  });

  const [OIDCMapping, setOIDCMapping] = useState(false);
  const [mappingData, setMappingData] = useState(oidc_mappings ?? []);

  useEffect(() => {
    setMappingData(oidc_mappings ?? []);
  }, [oidc_mappings]);


  return (
    <RbacValidator
      customValidateFunction={(userPerm) => find(userPerm, { actions: [Action.SUPER_ADMIN_ACTIONS] }) !== undefined}
    >
      <div className="bottom-bar-padding">
        {dialog && (
          <YBModal
            title="Disable OIDC"
            visible={dialog}
            showCancelButton={true}
            submitLabel="Disable OIDC"
            cancelLabel="Cancel"
            cancelBtnProps={{
              className: 'btn btn-default pull-left oidc-cancel-btn'
            }}
            onHide={handleDialogClose}
            onFormSubmit={handleDialogSubmit}
          >
            <div className="oidc-modal-c">
              <div className="oidc-modal-c-icon">
                <WarningIcon />
              </div>
              <div className="oidc-modal-c-content">
                <b>Note!</b>{' '}
                {
                  "By disabling OIDC users won't be able to login with your current\
            authentication provider. Are you sure?"
                }
              </div>
            </div>
          </YBModal>
        )}
        <Col>
          <Formik
            validationSchema={VALIDATION_SCHEMA}
            initialValues={initializeFormValues()}
            enableReinitialize
            onSubmit={async (values, { setSubmitting, resetForm }) => {
              if (submitAction === 'submit') {
                await saveOIDCConfigs(values);
                isOIDCMappingEnabled && doSaveOIDCMappings();
                setSubmitting(false);
                resetForm(values);
              } else if (submitAction === 'clear') {
                resetForm(resetFormValues());
                setMappingData(oidc_mappings);
              }
            }}
          >
            {({ handleSubmit, setFieldValue, isSubmitting, dirty, values }) => {
              const isDisabled = !oidcEnabled && showToggle;
              const isSaveDisabled = !dirty && isEqual(oidc_mappings, mappingData);

              const OIDCToggle = () => (
                <YBToggle
                  onToggle={handleToggle}
                  name="use_oauth"
                  input={{
                    value: oidcEnabled,
                    onChange: () => { }
                  }}
                  isReadOnly={!showToggle}
                />
              );

              const OIDCToggleTooltip = () => (
                <OverlayTrigger
                  placement="top"
                  overlay={
                    <Tooltip className="high-index" id="oidc-toggle-tooltip">
                      To enable OIDC you need to provide and save the required configurations
                    </Tooltip>
                  }
                >
                  <div>
                    <OIDCToggle />
                  </div>
                </OverlayTrigger>
              );

              const displayJWTToggled = async (event) => {
                await setRunTimeConfig({
                  key: `${OIDC_PATH}.showJWTInfoOnLogin`,
                  value: `${event.target.checked}`
                });
                setShowJWTTokenInfo(event.target.checked);
              };

              const renderOIDCMetadata = () => {
                return (
                  <OIDCMetadataModal
                    open={showMetadataModel}
                    value={OIDCMetadata}
                    onClose={() => {
                      setShowMetadataModal(false);
                    }}
                    onSubmit={(value) => {
                      setFieldValue('oidcProviderMetadata', value);
                      setShowMetadataModal(false);
                    }}
                  ></OIDCMetadataModal>
                );
              };

              return (
                <Form name="OIDCConfigForm" onSubmit={handleSubmit}>
                  <Row className="ua-field-row">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c ua-title-c"></Col>

                        <Col className="ua-toggle-c">
                          <>
                            <Col className="ua-toggle-label-c">
                              OIDC Enabled &nbsp;
                              <YBInfoTip
                                title="OIDC Enabled"
                                content="Enable or Disable OIDC Authentication"
                              >
                                <i className="fa fa-info-circle" />
                              </YBInfoTip>
                            </Col>

                            {showToggle ? <OIDCToggle /> : <OIDCToggleTooltip />}
                          </>
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  <Row key="oidc_clientID">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Client ID &nbsp;
                            <YBInfoTip
                              title="Client ID"
                              content="The unique identifier of your manually created client application in the Identity Provider"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <Field
                            name="clientID"
                            component={YBFormInput}
                            disabled={isDisabled}
                            className="ua-form-field"
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  <Row key="oidc_secret">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Client Secret &nbsp;
                            <YBInfoTip
                              customClass="oidc-info-popover"
                              title="Client Secret"
                              content="The password or secret for authenticating your Yugabyte client application with your Identity Provider"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <Field
                            name="secret"
                            component={YBFormInput}
                            disabled={isDisabled}
                            className="ua-form-field"
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  <Row key="oidc_discoveryURI">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Discovery URL&nbsp;
                            <YBInfoTip
                              title="Discovery URL"
                              content="Endpoint that validates all authorization requests. This can be found in discovery document"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <Field
                            name="discoveryURI"
                            component={YBFormInput}
                            disabled={isDisabled}
                            className="ua-form-field"
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  <Row key="oidc_scope">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Scope&nbsp;
                            <YBInfoTip title="Scope" content="Identity provider scope">
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <Field
                            name="oidcScope"
                            component={YBFormInput}
                            disabled={isDisabled}
                            className="ua-form-field"
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  <Row key="oidc_email_attribute">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Email Attribute&nbsp;
                            <YBInfoTip
                              title="Email Attribute"
                              content="Scope containing email ID of the user"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <Field
                            name="oidcEmailAttribute"
                            component={YBFormInput}
                            disabled={isDisabled}
                            className="ua-form-field"
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  <Row key="oidc_refresh_token_url">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Refresh Token URL&nbsp;
                            <YBInfoTip
                              title="Refresh Token URL"
                              content="Provide the endpoint that will be used for re-fetching the access token"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <Field
                            name="oidcRefreshTokenEndpoint"
                            component={YBFormInput}
                            disabled={isDisabled}
                            className="ua-form-field"
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  {/* ROLE SETTINGS */}
                  {isOIDCMappingEnabled && (
                    <>
                      {
                        (isFetching) ?
                          <Row className='mapping-loading-icon'>
                            <Col lg={6}>
                              <YBLoadingCircleIcon />
                            </Col>
                          </Row>

                          : (
                            <Row className="ldap-sec-container ">
                              <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c adv-settings">
                                <Row className="ua-field-row">
                                  <Col lg={9} className=" ua-title-c">
                                    <h5>Role Settings</h5>
                                  </Col>
                                </Row>

                                <div className='default-iodc'>
                                  <div className="default-ldap-role-c">
                                    <div className="ua-box-c default-ldap-role-b">
                                      <Row className="ua-field-row">
                                        <Col className="ua-label-c">
                                          <div>User&apos;s default role &nbsp;</div>
                                        </Col>
                                        <Col lg={12} className="ua-field ua-radio-c">
                                          <Row className="ua-radio-field-c">
                                            {YBA_ROLES.filter((role) => role.showInDefault).map(
                                              ({ label, value }) => (
                                                <Col
                                                  key={`oidc_default_value-${value}`}
                                                  className="ua-auth-radio-field"
                                                >
                                                  <Field
                                                    name={'oidc_default_role'}
                                                    type="radio"
                                                    component="input"
                                                    value={value}
                                                    checked={`${value}` === `${values['oidc_default_role']}`}
                                                    disabled={isDisabled}
                                                  />
                                                  &nbsp;&nbsp;{label}&nbsp;
                                                  {value === 'ConnectOnly' && (
                                                    <YBInfoTip
                                                      customClass="ldap-info-popover"
                                                      title="ConnectOnly role"
                                                      content="Users with ConnectOnly role cannot see any information other than their own profile information"
                                                    >
                                                      <i className="fa fa-info-circle" />
                                                    </YBInfoTip>
                                                  )}
                                                </Col>
                                              )
                                            )}
                                          </Row>
                                        </Col>
                                      </Row>
                                    </div>
                                  </div>
                                  <div className="ua-box-c ">
                                    <div className="box-title">Define Role to Group Mapping</div>
                                    <div className="box-content map-roles">
                                      <div className="ua-box-c">
                                        {mappingData.length === 0 && (
                                          <Col className="ua-field-row-c ">
                                            <Row className="ua-field-row create-map-c">
                                              <YBButton
                                                className="ldap-btn"
                                                disabled={isDisabled}
                                                btnText={
                                                  <>
                                                    <Mapping />
                                                    <>Create Mappings</>
                                                  </>
                                                }
                                                onClick={() => {
                                                  setOIDCMapping(true);
                                                }}
                                              />
                                            </Row>
                                          </Col>
                                        )}
                                        {mappingData.length > 0 && (
                                          <Col className="ua-field-row-c ">
                                            <Row className="ua-field-row edit-map-c">
                                              <YBButton
                                                className="ldap-btn edit-roles-btn"
                                                disabled={isDisabled}
                                                btnText={
                                                  <>
                                                    <Pencil />
                                                    <>Edit </>
                                                  </>
                                                }
                                                onClick={() => {
                                                  setOIDCMapping(true);
                                                }}
                                              />
                                            </Row>
                                          </Col>
                                        )}
                                      </div>

                                      {mappingData.length > 0 && (
                                        <div className="ua-box-c">
                                          <Col className="ua-field-row-c ">
                                            <Row className="ua-field-row">
                                              <BootstrapTable
                                                data={mappingData}
                                                className="ua-table"
                                                trClassName="ua-table-row"
                                                tableHeaderClass="ua-table-header"
                                              >
                                                <TableHeaderColumn
                                                  dataField="id"
                                                  isKey={true}
                                                  hidden={true}
                                                />
                                                <TableHeaderColumn
                                                  dataField="roles"
                                                  dataSort
                                                  width="170px"
                                                  dataFormat={(r) => {
                                                    const role = roles?.find(role => role.roleUUID === r[0]);
                                                    return role?.name ?? '-';
                                                  }}
                                                >
                                                  ROLE
                                                </TableHeaderColumn>
                                                <TableHeaderColumn dataField="groupName">
                                                  Group NAME
                                                </TableHeaderColumn>
                                              </BootstrapTable>
                                            </Row>
                                          </Col>
                                        </div>
                                      )}
                                    </div>
                                  </div>
                                </div>
                              </Col>
                            </Row>
                          )
                      }
                    </>

                  )}
                  {OIDCMapping && (
                    <OIDCMappingModal
                      open={OIDCMapping}
                      values={mappingData}
                      onClose={() => setOIDCMapping(false)}
                      onSubmit={(values) => {
                        setOIDCMapping(false);
                        setMappingData(values);
                      }}
                    />
                  )}
                  {/* ROLE SETTINGS */}

                  {showJWTTokenToggle && (
                    <Row key="oidc_show_jwt_attribute">
                      <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                        <Row className="ua-field-row">
                          <Col className="ua-label-c">
                            <div>
                              Display JWT token on login&nbsp;
                              <YBInfoTip
                                title="Display JWT token on login"
                                content="Option to display button to retrieve the JWT token"
                              >
                                <i className="fa fa-info-circle" />
                              </YBInfoTip>
                            </div>
                          </Col>
                          <Col lg={12} className="ua-field">
                            <Field name="showJWTInfoOnLogin">
                              {({ field }) => (
                                <YBToggle
                                  name="showJWTInfoOnLogin"
                                  onToggle={displayJWTToggled}
                                  input={{
                                    value: field.value,
                                    onChange: field.onChange
                                  }}
                                  isReadOnly={isDisabled}
                                  defaultChecked={false}
                                />
                              )}
                            </Field>
                          </Col>
                        </Row>
                      </Col>
                    </Row>
                  )}

                  <Row key="oidc_provider_meta">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">

                      <Row className="ua-field-row">
                        <Col className="ua-label-c">
                          <div>
                            Configure OIDC Provider Metadata&nbsp;
                            <YBInfoTip
                              title="Configure OIDC Metadata"
                              content={<div>If you have an airgapped installation, provide an OpenID Provider Configuration Document for your provider. <br/>
                              This contains the provider OIDC endpoints, supported claims, and other metadata.<br/>
                              YugabyteDB Anywhere uses the metadata to discover the URLs to use for authentication and the authentication service&apos;s public signing keys</div>}
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </div>
                        </Col>
                        <Col lg={12} className="ua-field">
                          <div
                            className={clsx('ua-provider-meta', isDisabled && 'ua-btn-disabled')}
                            onClick={() => {
                              if (isDisabled) return;
                              const escapedStr = values?.oidcProviderMetadata
                                ? escapeStr(values.oidcProviderMetadata).replace(/\\/g, '')
                                : '';
                              setOIDCMetadata(
                                escapedStr ? JSON.stringify(JSON.parse(escapedStr), null, 2) : ''
                              );
                              setShowMetadataModal(true);
                            }}
                          >
                            Configure OIDC Provider Metadata
                          </div>
                        </Col>
                      </Row>
                    </Col>
                  </Row>

                  <br />

                  <Row key="oidc_submit">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c ua-action-c">
                      <YBButton
                        btnText="Save"
                        onClick={() => {
                          submitAction = 'submit';
                          handleSubmit();
                        }}
                        disabled={isSubmitting || isDisabled || isSaveDisabled}
                        btnClass="btn btn-orange pull-right"
                      />
                      <YBButton
                        btnText="Clear"
                        onClick={() => {
                          submitAction = 'clear';
                          handleSubmit();
                        }}
                        disabled={isSubmitting || isDisabled}
                        btnClass="btn btn-orange pull-right clear-btn"
                      />
                    </Col>
                  </Row>

                  {showMetadataModel && renderOIDCMetadata()}
                </Form>
              );
            }}
          </Formik>
        </Col>
      </div>
    </RbacValidator>
  );
};
