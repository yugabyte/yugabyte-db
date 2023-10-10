// Copyright (c) YugaByte, Inc.

import { useEffect, useState, useRef } from 'react';
import * as Yup from 'yup';
import { useQuery, useMutation, useQueryClient } from 'react-query';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { trimStart, trimEnd, isString, isEqual, isUndefined } from 'lodash';
import { toast } from 'react-toastify';
import { Row, Col, OverlayTrigger, Tooltip } from 'react-bootstrap';
import { Formik, Form, Field } from 'formik';
import { YBFormInput, YBButton, YBModal, YBToggle, YBFormSelect } from '../../common/forms/fields';
import { LDAPMappingModal } from './LDAPGroups';
import { getLDAPRoleMapping, setLDAPRoleMapping } from '../../../actions/customers';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';
import { isRbacEnabled } from '../../../redesign/features/rbac/common/RbacUtils';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { YUGABYTE_TITLE } from '../../../config';
import WarningIcon from '../icons/warning_icon';
import Bulb from '../../universes/images/bulb.svg';
import Mapping from '../icons/mapping_icon';
import Pencil from '../icons/pencil_icon';

const VALIDATION_SCHEMA = Yup.object().shape({
  ldap_url: Yup.string()
    .matches(/^(?:(http|https|ldap|ldaps)?:\/\/)?[\w.-]+(?:[\w-]+)+:\d{1,5}$/, {
      message: 'LDAP URL must be a valid URL with port number'
    })
    .required('LDAP URL is Required'),
  ldap_security: Yup.string().required('Please select connection security'),
  ldap_service_account_password: Yup.string().when('ldap_service_account_distinguished_name', {
    is: (username) => username && username.length > 0,
    then: Yup.string().required('Password is Required'),
    otherwise: Yup.string()
  }),
  ldap_search_attribute: Yup.string().when('use_search_and_bind', {
    is: (use_search_and_bind) => use_search_and_bind === 'true',
    then: Yup.string().required('Search Attribute is Required'),
    otherwise: Yup.string()
  }),
  ldap_group_member_of_attribute: Yup.string().when(
    ['ldap_group_use_query', 'ldap_group_use_role_mapping'],
    {
      is: (ldap_group_use_query, ldap_group_use_role_mapping) =>
        ldap_group_use_role_mapping === true && ldap_group_use_query === 'false',
      then: Yup.string().required('User Attribute is Required'),
      otherwise: Yup.string()
    }
  ),
  ldap_group_search_filter: Yup.string().when(
    ['ldap_group_use_query', 'ldap_group_use_role_mapping'],
    {
      is: (ldap_group_use_query, ldap_group_use_role_mapping) =>
        ldap_group_use_role_mapping === true && ldap_group_use_query === 'true',
      then: Yup.string().required('Search Filter is Required'),
      otherwise: Yup.string()
    }
  ),
  ldap_group_search_base_dn: Yup.string().when(
    ['ldap_group_use_query', 'ldap_group_use_role_mapping'],
    {
      is: (ldap_group_use_query, ldap_group_use_role_mapping) =>
        ldap_group_use_role_mapping === true && ldap_group_use_query === 'true',
      then: Yup.string().required('Group Search Base DN is Required'),
      otherwise: Yup.string()
    }
  ),
  ldap_service_account_distinguished_name: Yup.string().when(
    ['use_search_and_bind', 'ldap_group_use_role_mapping'],
    {
      is: (use_search_and_bind, ldap_group_use_role_mapping) =>
        use_search_and_bind === 'true' || ldap_group_use_role_mapping === true,
      then: Yup.string().required('Bind DN is Required'),
      otherwise: Yup.string()
    }
  ),
  ldap_tls_protocol: Yup.string().when(['ldap_security'], {
    is: (ldap_security) => ['enable_ldaps', 'enable_ldap_start_tls'].includes(ldap_security),
    then: Yup.string().required('TLS Version is required'),
    otherwise: Yup.string()
  })
});

const LDAP_PATH = 'yb.security.ldap';

const TOAST_OPTIONS = { autoClose: 1750 };

const TLS_VERSIONS = [
  {
    label: 'TLSv1',
    value: 'TLSv1'
  },
  {
    label: 'TLSv1.1',
    value: 'TLSv1_1'
  },
  {
    label: 'TLSv1.2',
    value: 'TLSv1_2'
  }
];

const SECURITY_OPTIONS = [
  {
    label: 'Secure LDAP (LDAPS)',
    value: 'enable_ldaps'
  },
  {
    label: 'LDAP with StartTLS',
    value: 'enable_ldap_start_tls'
  },
  {
    label: 'None',
    value: 'unsecure'
  }
];

const AUTH_MODES = [
  {
    label: 'Simple Bind',
    value: false
  },
  {
    label: 'Search and Bind',
    value: true
  }
];

const LDAP_SCOPES = [
  {
    label: 'Object Scope',
    value: 'OBJECT'
  },
  {
    label: 'One Level Scope',
    value: 'ONELEVEL'
  },
  {
    label: 'Subtree Scope',
    value: 'SUBTREE'
  }
];
const DEFAULT_SCOPE = LDAP_SCOPES[2];

export const YBA_ROLES = [
  {
    label: 'Admin',
    value: 'Admin'
  },
  {
    label: 'BackupAdmin',
    value: 'BackupAdmin'
  },
  {
    label: 'ReadOnly',
    value: 'ReadOnly',
    showInDefault: true
  },
  {
    label: 'ConnectOnly',
    value: 'ConnectOnly',
    showInDefault: true
  }
];

export const LDAPAuth = (props) => {
  const {
    fetchRunTimeConfigs,
    setRunTimeConfig,
    deleteRunTimeConfig,
    featureFlags,
    runtimeConfigs: {
      data: { configEntries }
    }
  } = props;
  const [showToggle, setToggleVisible] = useState(false);
  const [dialog, showDialog] = useState(false);
  const [ldapEnabled, setLDAP] = useState(false);
  const [LDAPMapping, setLDAPMapping] = useState(false);
  const [mappingData, setMappingData] = useState([]);

  const queryClient = useQueryClient();
  const initMappingValue = useRef([]);

  const enableLDAPRoleMapping =
    featureFlags.test.enableLDAPRoleMapping || featureFlags.released.enableLDAPRoleMapping;

  useQuery('ldap_mapping', () => getLDAPRoleMapping(), {
    onSuccess: (resp) => {
      const initValue = resp?.data?.ldapDnToYbaRolePairs ?? [];
      setMappingData(initValue);
      initMappingValue.current = initValue;
    }
  });
  const setLDAPMappingData = useMutation((payload) => setLDAPRoleMapping(payload), {
    onSuccess: () => {
      void queryClient.invalidateQueries('ldap_mapping');
    }
  });

  const transformData = (values) => {
    const splittedURL = values.ldap_url.split(':');
    const ldap_port = splittedURL[splittedURL.length - 1];
    const ldap_url = values.ldap_url.replace(`:${ldap_port}`, '');
    const security = values.ldap_security;
    const use_search_and_bind = values.use_search_and_bind;
    const ldap_group_use_role_mapping = values.ldap_group_use_role_mapping;
    const ldap_group_search_scope = values.ldap_group_search_scope;
    const showServiceAccToggle =
      values.use_search_and_bind === 'false' && values.ldap_group_use_role_mapping === false;

    const transformedData = {
      ...values,
      ldap_url: ldap_url ?? '',
      ldap_port: ldap_port ?? '',
      enable_ldaps: `${security === 'enable_ldaps'}`,
      enable_ldap_start_tls: `${security === 'enable_ldap_start_tls'}`,
      ldap_group_use_role_mapping: `${ldap_group_use_role_mapping}`
    };

    if (use_search_and_bind === 'false') {
      transformedData.ldap_search_attribute = '';
    }

    if (ldap_group_search_scope?.value) {
      transformedData.ldap_group_search_scope = ldap_group_search_scope.value;
    }

    if (showServiceAccToggle && !values.use_service_account) {
      transformedData.ldap_service_account_distinguished_name = '';
      transformedData.ldap_service_account_password = '';
    }

    delete transformedData.ldap_security;
    delete transformedData.use_service_account;

    return transformedData;
  };

  const escapeStr = (str) => {
    let s = trimStart(str, '""');
    s = trimEnd(s, '""');
    return s;
  };

  const initializeFormValues = () => {
    const ldapConfigs = configEntries.filter((config) => config.key.includes(LDAP_PATH));
    const formData = ldapConfigs.reduce((fData, config) => {
      const [, key] = config.key.split(`${LDAP_PATH}.`);
      fData[key] = escapeStr(config.value);
      return fData;
    }, {});

    let finalFormData = {
      ...formData,
      use_search_and_bind: formData.use_search_and_bind ?? false,
      ldap_url: formData.ldap_url ? [formData.ldap_url, formData.ldap_port].join(':') : '',
      ldap_group_use_role_mapping: formData.ldap_group_use_role_mapping === 'true',
      ldap_group_search_scope: formData?.ldap_group_search_scope
        ? LDAP_SCOPES.find((scope) => scope.value === formData.ldap_group_search_scope)
        : null,
      use_service_account: !!formData.ldap_service_account_distinguished_name
    };

    //transform security data
    const { enable_ldaps, enable_ldap_start_tls } = formData;
    if (showToggle) {
      const ldap_security =
        enable_ldaps === 'true'
          ? 'enable_ldaps'
          : enable_ldap_start_tls === 'true'
            ? 'enable_ldap_start_tls'
            : 'unsecure';
      finalFormData = { ...finalFormData, ldap_security };
    }

    return finalFormData;
  };

  const saveLDAPConfigs = async (values) => {
    if (values.ldap_group_use_role_mapping && !mappingData.length) {
      toast.warn('Map YugabyteDB Anywhere builtin roles to your existing LDAP groups.', {
        autoClose: 3000
      });
      return false;
    }

    const formValues = transformData(values);
    const initValues = initializeFormValues();

    const promiseArray = Object.keys(formValues).reduce((promiseArr, key) => {
      if (formValues[key] !== initValues[key]) {
        const keyName = `${LDAP_PATH}.${key}`;
        const value =
          isString(formValues[key]) &&
            !['ldap_default_role', 'ldap_group_search_scope', 'ldap_tls_protocol'].includes(key)
            ? `"${formValues[key]}"`
            : formValues[key];
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

    //enable ldap on save
    promiseArray.push(
      setRunTimeConfig({
        key: `${LDAP_PATH}.use_ldap `,
        value: true
      })
    );

    //set mapping
    if (values.ldap_group_use_role_mapping)
      setLDAPMappingData.mutate({ ldapDnToYbaRolePairs: mappingData });

    try {
      toast.warn('Please wait. LDAP configuration is getting saved', TOAST_OPTIONS);
      const response = await Promise.all(promiseArray);
      response && fetchRunTimeConfigs();
      toast.success('LDAP configuration is saved successfully', TOAST_OPTIONS);
    } catch {
      toast.error('Failed to save LDAP configuration', TOAST_OPTIONS);
    }
  };

  const handleToggle = async (e) => {
    const value = e.target.checked;

    if (!value) showDialog(true);
    else {
      setLDAP(true);
      await setRunTimeConfig({
        key: `${LDAP_PATH}.use_ldap`,
        value: true
      });
      fetchRunTimeConfigs();
      toast.success(`LDAP authentication is enabled`, TOAST_OPTIONS);
    }
  };

  const handleDialogClose = () => {
    showDialog(false);
  };

  const handleDialogSubmit = async () => {
    setLDAP(false);
    showDialog(false);
    await setRunTimeConfig({
      key: `${LDAP_PATH}.use_ldap`,
      value: 'false'
    });
    fetchRunTimeConfigs();
    toast.warn(`LDAP authentication is disabled`, TOAST_OPTIONS);
  };

  useEffect(() => {
    const ldapConfig = configEntries.find((config) => config.key.includes(`${LDAP_PATH}.use_ldap`));
    setToggleVisible(!!ldapConfig);
    setLDAP(escapeStr(ldapConfig?.value) === 'true');
  }, [configEntries, setToggleVisible, setLDAP]);

  return (
    <div className="bottom-bar-padding">
      {dialog && (
        <YBModal
          title="Disable LDAP"
          visible={dialog}
          showCancelButton={true}
          submitLabel="Disable LDAP"
          cancelLabel="Cancel"
          cancelBtnProps={{
            className: 'btn btn-default pull-left ldap-cancel-btn'
          }}
          onHide={handleDialogClose}
          onFormSubmit={handleDialogSubmit}
        >
          <div className="ldap-modal-c">
            <div className="ldap-modal-c-icon">
              <WarningIcon />
            </div>
            <div className="ldap-modal-c-content">
              <b>Note!</b> Users authenticated via LDAP will no longer be able to login if you
              disable LDAP. Are you sure?
            </div>
          </div>
        </YBModal>
      )}
      <Col>
        <Formik
          validationSchema={VALIDATION_SCHEMA}
          initialValues={initializeFormValues()}
          enableReinitialize
          onSubmit={(values, { setSubmitting, resetForm }) => {
            saveLDAPConfigs(values);
            setSubmitting(false);
            resetForm(values);
          }}
        >
          {({ handleSubmit, isSubmitting, errors, dirty, values, setFieldValue }) => {
            const isDisabled = !ldapEnabled && showToggle;
            const isSaveEnabled = dirty || !isEqual(initMappingValue.current, mappingData);
            const showServiceAccToggle =
              values.use_search_and_bind === 'false' &&
              values.ldap_group_use_role_mapping === false;
            const showServAccFields = values.use_service_account || !showServiceAccToggle;

            const LDAPToggle = () => (
              <YBToggle
                onToggle={handleToggle}
                name="use_ldap"
                input={{
                  value: ldapEnabled,
                  onChange: () => { }
                }}
                isReadOnly={!showToggle}
              />
            );

            const LDAPToggleTooltip = () => (
              <OverlayTrigger
                placement="top"
                overlay={
                  <Tooltip className="high-index" id="ldap-toggle-tooltip">
                    To enable LDAP you need to provide and save the required configurations
                  </Tooltip>
                }
              >
                <div>
                  <LDAPToggle />
                </div>
              </OverlayTrigger>
            );

            return (
              <Form name="LDAPConfigForm" onSubmit={handleSubmit}>
                <Row className="ua-field-row">
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                    <Row className="ua-field-row">
                      <Col className="ua-label-c ua-title-c">
                        <h5>LDAP Configuration</h5>
                      </Col>

                      <Col className="ua-toggle-c">
                        <>
                          <Col className="ua-toggle-label-c">
                            LDAP Enabled &nbsp;
                            <YBInfoTip
                              title="LDAP Enabled"
                              content="Enable or Disable LDAP Authentication"
                            >
                              <i className="fa fa-info-circle" />
                            </YBInfoTip>
                          </Col>

                          {showToggle ? <LDAPToggle /> : <LDAPToggleTooltip />}
                        </>
                      </Col>
                    </Row>
                  </Col>
                </Row>

                {/* LDAP CONFIG */}
                <Row>
                  <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c ldap-config">
                    <div className="ua-box-c">
                      <Row key="ldap_url">
                        <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                          <Row className="ua-field-row">
                            <Col className="ua-label-c">
                              <div>
                                LDAP URL &nbsp;
                                <YBInfoTip
                                  title="LDAP URL"
                                  content="LDAP URL must be a valid URL with port number, Ex:- 0.0.0.0:0000"
                                >
                                  <i className="fa fa-info-circle" />
                                </YBInfoTip>
                              </div>
                            </Col>
                            <Col lg={12} className="ua-field">
                              <Field
                                name="ldap_url"
                                component={YBFormInput}
                                disabled={isDisabled}
                                className="ua-form-field"
                              />
                            </Col>
                          </Row>
                        </Col>
                      </Row>

                      <Row key="connection_security">
                        <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                          <Row className="ua-field-row">
                            <Col className="ua-label-c">
                              <div>
                                Security Protocol &nbsp;
                                <YBInfoTip
                                  customClass="ldap-info-popover"
                                  title="Security Protocol"
                                  content="Configure the LDAP server connection to use LDAPS (SSL) or LDAP with StartTLS (TLS) encryption"
                                >
                                  <i className="fa fa-info-circle" />
                                </YBInfoTip>
                              </div>
                            </Col>
                            <Col lg={12} className="ua-field ua-radio-c">
                              <Row className="ua-radio-field-c">
                                {SECURITY_OPTIONS.map(({ label, value }) => (
                                  <Col key={`security-${value}`} className="ua-auth-radio-field">
                                    <Field
                                      name={'ldap_security'}
                                      type="radio"
                                      component="input"
                                      value={value}
                                      checked={`${value}` === `${values['ldap_security']}`}
                                      disabled={isDisabled}
                                    />
                                    &nbsp;&nbsp;{label}
                                  </Col>
                                ))}
                              </Row>
                              <Row className="has-error">
                                {errors.ldap_security && (
                                  <div className="help-block standard-error">
                                    <span>{errors.ldap_security}</span>
                                  </div>
                                )}
                              </Row>
                            </Col>
                          </Row>
                        </Col>
                      </Row>

                      {['enable_ldap_start_tls', 'enable_ldaps'].includes(
                        values?.ldap_security
                      ) && (
                          <Row key="tls_protocol">
                            <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                              <Row className="ua-field-row">
                                <Col className="ua-label-c">
                                  <div>
                                    TLS Version &nbsp;
                                    <YBInfoTip
                                      customClass="ldap-info-popover"
                                      title="TLS Protocol Version"
                                      content="Configure the TLS Protocol Version to be used in case of StartTLS or LDAPS"
                                    >
                                      <i className="fa fa-info-circle" />
                                    </YBInfoTip>
                                  </div>
                                </Col>
                                <Col lg={12} className="ua-field ua-radio-c">
                                  <Row className="ua-radio-field-c">
                                    {TLS_VERSIONS.map(({ label, value }) => (
                                      <Col key={`tls-${value}`} className="ua-radio-field">
                                        <Field
                                          name={'ldap_tls_protocol'}
                                          type="radio"
                                          component="input"
                                          value={value}
                                          checked={`${value}` === `${values['ldap_tls_protocol']}`}
                                          disabled={isDisabled}
                                        />
                                        &nbsp;&nbsp;{label}
                                      </Col>
                                    ))}
                                  </Row>
                                  <Row className="has-error">
                                    {errors.ldap_security && (
                                      <div className="help-block standard-error">
                                        <span>{errors.ldap_security}</span>
                                      </div>
                                    )}
                                  </Row>
                                </Col>
                              </Row>
                            </Col>
                          </Row>
                        )}

                      <Row key="ldap_basedn">
                        <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                          <Row className="ua-field-row">
                            <Col className="ua-label-c">
                              <div>
                                LDAP Base DN <br />
                                (Optional) &nbsp;
                                <YBInfoTip
                                  customClass="ldap-info-popover"
                                  title="LDAP Base DN"
                                  content="Base of the DN for LDAP queries. Will be appended to the user name at login time to query the LDAP server"
                                >
                                  <i className="fa fa-info-circle" />
                                </YBInfoTip>
                              </div>
                            </Col>
                            <Col lg={12} className="ua-field">
                              <Field
                                name="ldap_basedn"
                                component={YBFormInput}
                                disabled={isDisabled}
                                className="ua-form-field"
                              />
                            </Col>
                          </Row>
                        </Col>
                      </Row>

                      <Row key="ldap_dn_prefix">
                        <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                          <Row className="ua-field-row">
                            <Col className="ua-label-c">
                              <div>
                                LDAP DN Prefix <br />
                                (Optional)&nbsp;
                                <YBInfoTip
                                  title="LDAP DN Prefix"
                                  content="LDAP DN Prefix will be set to CN= by default if not provided"
                                >
                                  <i className="fa fa-info-circle" />
                                </YBInfoTip>
                              </div>
                            </Col>
                            <Col lg={12} className="ua-field">
                              <Field
                                name="ldap_dn_prefix"
                                component={YBFormInput}
                                disabled={isDisabled}
                                className="ua-form-field"
                              />
                            </Col>
                          </Row>
                        </Col>
                      </Row>

                      <Row key="ldap_customeruuid">
                        <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                          <Row className="ua-field-row">
                            <Col className="ua-label-c">
                              <div>
                                LDAP Customer UUID <br />
                                (Optional) &nbsp;
                                <YBInfoTip
                                  title="LDAP Customer UUID"
                                  content="Unique ID of the customer in case of multi-tenant platform"
                                >
                                  <i className="fa fa-info-circle" />
                                </YBInfoTip>
                              </div>
                            </Col>
                            <Col lg={12} className="ua-field">
                              <Field
                                name="ldap_customeruuid"
                                component={YBFormInput}
                                disabled={isDisabled}
                                className="ua-form-field"
                              />
                            </Col>
                          </Row>
                        </Col>
                      </Row>

                      <Row key="auth_mode">
                        <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                          <Row className="ua-field-row">
                            <Col className="ua-label-c">
                              <div>
                                Binding Mechanism &nbsp;
                                <YBInfoTip
                                  customClass="ldap-info-popover"
                                  title="Binding Mechanism"
                                  content="Mechanism used to bind to the LDAP server"
                                >
                                  <i className="fa fa-info-circle" />
                                </YBInfoTip>
                              </div>
                            </Col>
                            <Col lg={12} className="ua-field ua-radio-c">
                              <Row className="ua-radio-field-c">
                                {AUTH_MODES.map(({ label, value }) => (
                                  <Col key={`auth-mode-${value}`} className="ua-auth-radio-field">
                                    <Field
                                      name={'use_search_and_bind'}
                                      type="radio"
                                      component="input"
                                      value={value}
                                      checked={`${value}` === `${values['use_search_and_bind']}`}
                                      disabled={isDisabled}
                                    />
                                    &nbsp;&nbsp;{label}
                                  </Col>
                                ))}
                              </Row>
                            </Col>
                          </Row>
                        </Col>
                      </Row>

                      {values?.use_search_and_bind === 'true' && (
                        <Row key="ldap_search_attribute">
                          <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                            <Row className="ua-field-row">
                              <Col className="ua-label-c">
                                <div>
                                  Search Attribute &nbsp;
                                  <YBInfoTip
                                    title="Search Attribute"
                                    content="Attribute that will be used to search for the user in the LDAP server"
                                  >
                                    <i className="fa fa-info-circle" />
                                  </YBInfoTip>
                                </div>
                              </Col>
                              <Col lg={12} className="ua-field">
                                <Field
                                  name="ldap_search_attribute"
                                  component={YBFormInput}
                                  disabled={isDisabled}
                                  className="ua-form-field"
                                />
                              </Col>
                            </Row>
                          </Col>
                        </Row>
                      )}
                      <br />
                      <Row key="footer_banner-1">
                        <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                          <Row className="ua-field-row">
                            <div className="footer-msg">
                              <Col>
                                <img alt="--" src={Bulb} width="24" />
                              </Col>
                              &nbsp;
                              <Col>
                                <Row>
                                  <b>Note!</b> {YUGABYTE_TITLE} will use the Service Account{' '}
                                  {`{{Bind DN}}`} to connect to your LDAP server and perform the
                                  search.
                                </Row>
                              </Col>
                            </div>
                          </Row>
                        </Col>
                      </Row>
                    </div>
                  </Col>
                </Row>
                {/* LDAP CONFIG */}

                {/* ROLE SETTINGS */}
                {enableLDAPRoleMapping && (
                  <Row className="ldap-sec-container ">
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c adv-settings">
                      <Row className="ua-field-row">
                        <Col lg={9} className=" ua-title-c">
                          <h5>Role Settings</h5>
                        </Col>
                      </Row>

                      <div>
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
                                        key={`ldap_default_value-${value}`}
                                        className="ua-auth-radio-field"
                                      >
                                        <Field
                                          name={'ldap_default_role'}
                                          type="radio"
                                          component="input"
                                          value={value}
                                          checked={`${value}` === `${values['ldap_default_role']}`}
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
                          <Row
                            key="ldap_use_role_mapping"
                            className="role-mapping-c mapping-toggle"
                          >
                            <Col xs={10} sm={9} md={8} lg={4} className="ua-field-row-c">
                              <YBToggle
                                name="ldap_group_use_role_mapping"
                                input={{
                                  value: values.ldap_group_use_role_mapping,
                                  onChange: (e) => {
                                    setFieldValue('ldap_group_use_role_mapping', e.target.checked);
                                    if (isUndefined(values.ldap_group_use_query))
                                      setFieldValue('ldap_group_use_query', 'false');
                                  }
                                }}
                              />{' '}
                              &nbsp; Map YugabyteDB Anywhere built-in roles to your existing LDAP
                              groups
                            </Col>
                          </Row>
                          {`true` === `${values.ldap_group_use_role_mapping}` && (
                            <>
                              <div className="box-title">1. Configure membership lookup</div>

                              <div className="box-content config-membership">
                                <Row className="ua-radio-field-c">
                                  <Col className="ua-auth-radio-field box-content-row">
                                    <Field
                                      name={'ldap_group_use_query'}
                                      type="radio"
                                      component="input"
                                      value={'false'}
                                      checked={`false` === `${values['ldap_group_use_query']}`}
                                      disabled={isDisabled}
                                    />
                                    &nbsp;&nbsp;{'User Attribute'}
                                  </Col>

                                  <Col className="ua-auth-radio-field box-content-row">
                                    <Field
                                      name={'ldap_group_use_query'}
                                      type="radio"
                                      component="input"
                                      value={'true'}
                                      checked={`true` === `${values['ldap_group_use_query']}`}
                                      disabled={isDisabled}
                                    />
                                    &nbsp;&nbsp;{'Group Search Filter'}
                                  </Col>
                                </Row>

                                {`false` === `${values.ldap_group_use_query}` ? (
                                  <Row key="ldap_group_member_of_attribute">
                                    <Col xs={10} sm={9} md={8} lg={4} className="ua-field-row-c ">
                                      <Row className="ua-field-row attrib-name">
                                        <Field
                                          name="ldap_group_member_of_attribute"
                                          label=" Attribute that will be used to find the list of groups that is a
                                        member of"
                                          component={YBFormInput}
                                          disabled={isDisabled}
                                          className="ua-form-field"
                                          placeholder="memberOf"
                                          defaultValue="memberOf"
                                        />
                                      </Row>
                                    </Col>
                                  </Row>
                                ) : (
                                  <>
                                    <Row key="ldap_group_search_filter" className="filter_query">
                                      <Col className="ua-field-row-c ">
                                        <Row className="ua-field-row filter_query">
                                          <Field
                                            name="ldap_group_search_filter"
                                            label="Filter to find all groups that a user belongs to"
                                            component={YBFormInput}
                                            disabled={isDisabled}
                                            className="ua-form-field"
                                            placeholder="(&(objectClass=group)(member=CN={username},OU=Users,DC=yugabyte,DC=com)"
                                          />
                                        </Row>
                                        <Row className="helper-text helper-text-1">{`Use {username} to refer to the the YBA username`}</Row>
                                        <Row className="helper-text helper-text-2">
                                          {`Example: (&(objectClass=group)(member=CN={username},OU=Users,DC=yugabyte,DC=com)`}
                                        </Row>
                                      </Col>
                                    </Row>
                                    <br />
                                    <Row key="ldap_group_search_base_dn" className="filter_query">
                                      <Col className="ua-field-row-c ">
                                        <Row className="ua-field-row filter_query">
                                          <Field
                                            name="ldap_group_search_base_dn"
                                            label={
                                              <>
                                                Group Search Base DN&nbsp;
                                                <YBInfoTip
                                                  customClass="ldap-info-popover"
                                                  title="Group Search Base DN"
                                                  content="Base DN used to search for user group membership."
                                                >
                                                  <i className="fa fa-info-circle" />
                                                </YBInfoTip>
                                              </>
                                            }
                                            component={YBFormInput}
                                            disabled={isDisabled}
                                            className="ua-form-field"
                                            placeholder="Example:- OU=Groups,DC=yugabyte,DC=com"
                                          />
                                        </Row>
                                      </Col>
                                    </Row>
                                    <br />
                                    <Row key="ldap_group_search_scope" className="scope-selector">
                                      <Col className="ua-field-row-c ">
                                        <Row className="ua-field-row scope-selector">
                                          <Field
                                            name="ldap_group_search_scope"
                                            label="Select Scope"
                                            component={YBFormSelect}
                                            options={LDAP_SCOPES}
                                            isDisabled={isDisabled}
                                            value={DEFAULT_SCOPE}
                                            defaultValue={DEFAULT_SCOPE}
                                          />
                                        </Row>
                                      </Col>
                                    </Row>
                                  </>
                                )}
                              </div>

                              <div className="box-title">2. Define Role to Group Mapping</div>
                              <div className="box-content map-roles">
                                <div className="ua-box-c">
                                  {!mappingData.length && (
                                    <Col className="ua-field-row-c ">
                                      <Row className="ua-field-row create-map-c">
                                        <YBButton
                                          className="ldap-btn"
                                          btnText={
                                            <>
                                              <Mapping />
                                              <>Create Mappings</>
                                            </>
                                          }
                                          onClick={() => {
                                            setLDAPMapping(true);
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
                                          btnText={
                                            <>
                                              <Pencil />
                                              <>Edit </>
                                            </>
                                          }
                                          onClick={() => {
                                            setLDAPMapping(true);
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
                                            dataField="ybaRole"
                                            dataSort
                                            width="170px"
                                          >
                                            ROLE
                                          </TableHeaderColumn>
                                          <TableHeaderColumn dataField="distinguishedName">
                                            DN NAME
                                          </TableHeaderColumn>
                                        </BootstrapTable>
                                      </Row>
                                    </Col>
                                  </div>
                                )}
                              </div>
                            </>
                          )}
                        </div>
                      </div>
                    </Col>
                  </Row>
                )}
                {/* ROLE SETTINGS */}

                {/* SERVICE ACCOUNT */}
                <Row className="ldap-sec-container">
                  <Row className="ua-field-row">
                    <Col lg={9} className=" ua-title-c">
                      <h5>Service Account Details</h5>
                    </Col>
                  </Row>

                  <Row
                    key="ldap_service_account_details"
                    className="ldap-service-account ua-flex-box"
                  >
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                      <div className="ua-box-c">
                        {showServiceAccToggle ? (
                          <Row key="ldap_use_service_account">
                            <Col
                              xs={10}
                              sm={9}
                              md={8}
                              lg={4}
                              className="ua-field-row-c serv-acc-toggle"
                            >
                              <YBToggle
                                name="use_service_account"
                                input={{
                                  value: values.use_service_account,
                                  onChange: (e) => {
                                    setFieldValue('use_service_account', e.target.checked);
                                  }
                                }}
                              />{' '}
                              &nbsp; Add Service Account Details
                            </Col>
                          </Row>
                        ) : (
                          <Row key="ldap_use_service_account-2">
                            <div className="box-title add-serv-acc-no-toggle">
                              Add Service Account Details
                            </div>
                          </Row>
                        )}
                        {showServAccFields && (
                          <>
                            <Row key="ldap_service_account_distinguished_name">
                              <Col xs={10} sm={9} md={8} lg={4} className="ua-field-row-c">
                                <Row className="ua-field-row">
                                  <Col className="ua-label-c">
                                    <div>
                                      Bind DN &nbsp;
                                      <YBInfoTip
                                        customClass="ldap-info-popover"
                                        title="Bind DN"
                                        content="Bind DN will be used to login to the service account"
                                      >
                                        <i className="fa fa-info-circle" />
                                      </YBInfoTip>
                                    </div>
                                  </Col>
                                  <Col lg={12} className="ua-field">
                                    <Field
                                      name="ldap_service_account_distinguished_name"
                                      component={YBFormInput}
                                      disabled={isDisabled}
                                      className="ua-form-field"
                                    />
                                  </Col>
                                </Row>
                              </Col>
                            </Row>

                            <Row key="ldap_service_account_password">
                              <Col xs={10} sm={9} md={8} lg={4} className="ua-field-row-c">
                                <Row className="ua-field-row">
                                  <Col className="ua-label-c">
                                    <div>
                                      Password&nbsp;
                                      <YBInfoTip
                                        title="Password"
                                        content="Password for Service Account"
                                      >
                                        <i className="fa fa-info-circle" />
                                      </YBInfoTip>
                                    </div>
                                  </Col>
                                  <Col lg={12} className="ua-field">
                                    <Field
                                      name="ldap_service_account_password"
                                      component={YBFormInput}
                                      disabled={isDisabled}
                                      className="ua-form-field"
                                      type="password"
                                      autoComplete="new-password"
                                    />
                                  </Col>
                                </Row>
                              </Col>
                            </Row>
                          </>
                        )}

                        <br />
                        <Row key="footer_banner-2">
                          <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c">
                            <Row className="ua-field-row">
                              <div className="footer-msg">
                                <Col>
                                  <img alt="--" src={Bulb} width="24" />
                                </Col>
                                &nbsp;
                                <Col>
                                  <Row>
                                    <b>Note!</b> {YUGABYTE_TITLE} will use the Service Account{' '}
                                    {`{{Bind DN}}`} to connect to your LDAP server and perform the
                                    search.
                                  </Row>
                                </Col>
                              </div>
                            </Row>
                          </Col>
                        </Row>
                      </div>
                    </Col>
                  </Row>
                </Row>
                {/* SERVICE ACCOUNT */}

                {LDAPMapping && (
                  <LDAPMappingModal
                    open={LDAPMapping}
                    values={mappingData}
                    onClose={() => setLDAPMapping(false)}
                    onSubmit={(values) => {
                      setLDAPMapping(false);
                      setMappingData(values);
                    }}
                  />
                )}

                <Row key="ldap_submit">
                  <RbacValidator
                    accessRequiredOn={UserPermissionMap.updateLDAP}
                    isControl
                  >
                    <Col xs={12} sm={11} md={10} lg={6} className="ua-field-row-c ua-action-c">
                      <YBButton
                        btnText="Save"
                        btnType="submit"
                        disabled={(isSubmitting || isDisabled || !isSaveEnabled) && !isRbacEnabled()}
                        btnClass="btn btn-orange pull-right"
                      />
                    </Col>
                  </RbacValidator>
                </Row>
              </Form>
            );
          }}
        </Formik>
      </Col>
    </div >
  );
};
