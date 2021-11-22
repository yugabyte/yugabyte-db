// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field } from 'formik';
import * as Yup from 'yup';

import { YBFormInput, YBFormSelect } from '../../../components/common/forms/fields';
import { YBModalForm } from '../../../components/common/forms';
import { toast } from 'react-toastify';

const getValidationSchema = (type) => {
  const shape = {
    version: Yup.string()
      .matches(/^((\d+).(\d+).(\d+).(\d+)(?:-[a-z0-9]+)*)$/, {
        message: 'Incorrect version format. Valid formats: 1.1.1.1, 1.1.1.1-b1 or 1.1.1.1-b12-remote',
        excludeEmptyString: true
      })
      .required('Release Version is Required'),
    x86_64: Yup.string()
      .matches(/(?:(https|s3|gs):\/\/).+$/, {
        message: 'Path should starts with gs, s3 or https'
      })
      .required('Path is required')
  };

  switch (type) {
    case 's3':
      shape['accessKeyId'] = Yup.string().required('Access key id is required');
      shape['secretAccessKey'] = Yup.string().required('Secret access key is required');
      break;
    case 'gcs':
      shape['credentialsJson'] = Yup.string().required('Credentials Json is required');
      break;
    case 'http':
      shape['x86_64_checksum'] = Yup.string().required('Checksum is required');
      break;
    default:
      throw new Error('Unknown import type ' + type);
  }

  return Yup.object().shape(shape);
};

const PathField = () => <Field name="x86_64" label="Path" component={YBFormInput} />;

const S3Fields = () => (
  <>
    <PathField />
    <Field name="accessKeyId" label="Access key id" component={YBFormInput} />
    <Field name="secretAccessKey" label="Secret access key" component={YBFormInput} />
  </>
);

const GcsFields = () => (
  <>
    <PathField />
    <Field name="credentialsJson" label="Credentials Json" component={YBFormInput} />
  </>
);

const HttpFields = () => (
  <>
    <PathField />
    <Field name="x86_64_checksum" component={YBFormInput} label="Checksum" />
  </>
);

const getFields = (type) => {
  switch (type.value) {
    case 's3':
      return <S3Fields />;
    case 'gcs':
      return <GcsFields />;
    case 'http':
      return <HttpFields />;
    default:
      throw new Error('Unknown import type ' + type.value);
  }
};

const preparePayload = (values) => {
  const version = values['version'];
  const importType = values['import_type'].value;

  const payload = {
    [version]: {
      [importType]: {}
    }
  };

  switch (importType) {
    case 's3':
      payload[version][importType] = {
        accessKeyId: values['accessKeyId'],
        secretAccessKey: values['secretAccessKey']
      };
      break;
    case 'gcs':
      try {
        //check whether given json is valid or not
        JSON.parse(values['credentialsJson']);
      } catch (e) {
        toast.error('Credential Json should be a valid json');
        return;
      }
      payload[version][importType] = {
        credentialsJson: values['credentialsJson']
      };
      break;
    case 'http':
      break;
    default:
      throw new Error(importType + ' not supported');
  }

  payload[version][importType]['paths'] = {
    x86_64: values['x86_64']
  };

  if (importType === 'http') {
    payload[version][importType]['paths'] = {
      ...payload[version][importType]['paths'],
      x86_64_checksum: values['x86_64_checksum']
    };
  }

  return payload;
};
export default class ImportRelease extends Component {
  constructor(props) {
    super(props);
    this.state = {
      importType: props.initialValues.import_type.value
    };
  }

  importRelease = (values, { setSubmitting }) => {
    const { importYugaByteRelease } = this.props;
    const payload = preparePayload(values);
    setSubmitting(false);
    if (!payload) return;
    importYugaByteRelease(payload);
  };

  render() {
    const { visible, onHide } = this.props;
    const { importType } = this.state;
    return (
      <div className="universe-apps-modal">
        <YBModalForm
          title={'Import Release'}
          visible={visible}
          onHide={onHide}
          showCancelButton={true}
          cancelLabel={'Cancel'}
          className="import-release-modal"
          onFormSubmit={this.importRelease}
          initialValues={this.props.initialValues}
          validationSchema={getValidationSchema(importType)}
          render={({ values }) => (
            <>
              <Row>
                <Col lg={12}>
                  <Field name="version" component={YBFormInput} label={'Release Version'} />
                </Col>
                <Col lg={12}>
                  <Field
                    name="import_type"
                    component={YBFormSelect}
                    options={this.props.import_types}
                    label="Import from"
                    onChange={({ form }, value) => {
                      form.setFieldValue('import_type', value);
                      this.setState({
                        importType: value.value
                      });
                    }}
                  />
                </Col>
              </Row>
              <Row>
                <Col lg={12}>{getFields(values['import_type'])}</Col>
              </Row>
            </>
          )}
        ></YBModalForm>
      </div>
    );
  }
}
