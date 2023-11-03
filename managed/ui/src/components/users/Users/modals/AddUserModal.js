// Copyright (c) YugaByte, Inc.

import * as Yup from 'yup';
import { isSSOEnabled } from '../../../../config';
import { Row, Col } from 'react-bootstrap';
import { Field, Formik } from 'formik';
import { YBModal, YBFormSelect, YBFormInput } from '../../../common/forms/fields';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';

const MIN_PASSWORD_LENGTH = 8;
export const userRoles = [
  { value: 'Admin', label: 'Admin' },
  { value: 'BackupAdmin', label: 'BackupAdmin' },
  { value: 'ReadOnly', label: 'ReadOnly' },
  { value: 'ConnectOnly', label: 'ConnectOnly' }
];

export const AddUserModal = (props) => {
  const {
    modalVisible,
    onHide,
    passwordValidationInfo,
    createUser,
    addCustomerConfig,
    getCustomerUsers
  } = props;
  const minPasswordLength = passwordValidationInfo?.minLength || MIN_PASSWORD_LENGTH;
  const initialValues = {
    email: '',
    password: '',
    confirmPassword: '',
    role: undefined
  };

  const oidcEnabled = isSSOEnabled();

  const submitForm = async (values) => {
    values.role = values.role.value;
    try {
      const config = {
        type: 'PASSWORD_POLICY',
        name: 'password policy',
        data: passwordValidationInfo
      };
      await createUser(values);
      await addCustomerConfig(config);
    } catch (error) {
      console.error('Failed to create user', error);
    } finally {
      onHide();
      getCustomerUsers();
    }
  };

  //Validation schemas
  const passwordSchema = {
    password: Yup.string()
      .required('Password is required')
      .min(8, `Password is too short - must be ${minPasswordLength} characters minimum.`)
      .matches(
        /^(?=.*[0-9])(?=.*[!@#$%^&*])(?=.*[a-z])(?=.*[A-Z])[a-zA-Z0-9!@#$%^&*]{8,256}$/,
        `Password must contain at least ${passwordValidationInfo?.minDigits} digit,
        ${passwordValidationInfo?.minUppercase} capital,
        ${passwordValidationInfo?.minLowercase} lowercase
        and ${passwordValidationInfo?.minSpecialCharacters} of the !@#$%^&* (special) characters.`
      ),
    confirmPassword: Yup.string().oneOf([Yup.ref('password')], 'Passwords must match')
  };
  const validationSchema = Yup.object().shape({
    email: Yup.string().required('Email is required').email('Enter a valid email'),
    role: Yup.object().required('Role is required'),
    ...(!oidcEnabled ? passwordSchema : {})
  });

  return (
    <Formik initialValues={initialValues} validationSchema={validationSchema} onSubmit={submitForm}>
      {({ handleSubmit }) => (
        <YBModal
          visible={modalVisible}
          formName="CreateUserForm"
          onHide={onHide}
          onFormSubmit={handleSubmit}
          title="Add User"
          submitLabel="Submit"
          cancelLabel="Close"
          showCancelButton
        >
          <div className="add-user-container">
            <Row className="config-provider-row">
              <Col lg={3}>
                <div className="form-item-custom-label">Email</div>
              </Col>
              <Col lg={7}>
                <Field name="email" placeholder="Email address" component={YBFormInput} />
              </Col>
            </Row>
            {!oidcEnabled && (
              <>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Password</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="password"
                      placeholder="Password"
                      type="password"
                      component={YBFormInput}
                    />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Confirm Password</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="confirmPassword"
                      placeholder="Confirm Password"
                      type="password"
                      component={YBFormInput}
                    />
                  </Col>
                </Row>
              </>
            )}

            <Row className="config-provider-row">
              <Col lg={3}>
                <div className="form-item-custom-label">Role</div>
              </Col>
              <Col lg={7}>
                <Field
                  name="role"
                  component={YBFormSelect}
                  options={userRoles}
                  isSearchable={false}
                />
              </Col>
            </Row>
          </div>
        </YBModal>
      )}
    </Formik>
  );
};
