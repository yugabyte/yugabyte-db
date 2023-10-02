// Copyright (c) YugaByte, Inc.

import { useState } from 'react';
import { Field } from 'formik';
import { Row, Col } from 'react-bootstrap';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';
import * as Yup from 'yup';

import { YBFormInput, YBFormSelect } from '../../../components/common/forms/fields';
import { YBModalForm } from '../../../components/common/forms';
import { YBLoading } from '../../common/indicators';
import { api } from '../../../redesign/helpers/api';

const getValidationSchema = (type) => {
  const shape = {
    version: Yup.string()
      .matches(/^((\d+).(\d+).(\d+).(\d+)(?:-[a-z0-9]+)*)$/, {
        message:
          'Incorrect version format. Valid formats: 1.1.1.1, 1.1.1.1-b1 or 1.1.1.1-b12-remote',
        excludeEmptyString: true
      })
      .required('Release Version is Required'),
    x86_64: Yup.string()
      .matches(/(?:(https|s3|gs):\/\/).+$/, {
        message: 'Path should starts with gs, s3 or https'
      })
      .required('Path is required'),
    helmChart: Yup.string()
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
      shape['x86_64_checksum'] = Yup.string()
        .required('Checksum is required')
        .matches(/(MD5|SHA1|SHA256):\w+/, {
          message:
            'Checksum must have a pattern of [MD5|SHA1|SHA256]:[checksum_value];' +
            ' e.g., MD5:99d42a85b0d2b2813d6cea877aaab919'
        });
      shape['helmChartChecksum'] = Yup.string()
        .when('helmChart', {
          is: (val) => val && val.length > 0,
          then: Yup.string().required('Checksum is required'),
          otherwise: Yup.string()
        })
        .matches(/(MD5|SHA1|SHA256):\w+/, {
          message:
            'Checksum must have a pattern of [MD5|SHA1|SHA256]:[checksum_value];' +
            ' e.g., MD5:99d42a85b0d2b2813d6cea877aaab919'
        });
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
    <Field name="helmChart" label="Helm chart" component={YBFormInput} />
    <Field name="accessKeyId" label="Access key id" component={YBFormInput} />
    <Field name="secretAccessKey" label="Secret access key" component={YBFormInput} />
  </>
);

const GcsFields = () => (
  <>
    <PathField />
    <Field name="helmChart" label="Helm chart" component={YBFormInput} />
    <Field name="credentialsJson" label="Credentials Json" component={YBFormInput} />
  </>
);

const HttpFields = () => (
  <>
    <PathField />
    <Field name="x86_64_checksum" component={YBFormInput} label="Checksum" />
    <Field name="helmChart" label="Helm chart" component={YBFormInput} />
    <Field name="helmChartChecksum" label="Helm chart checksum" component={YBFormInput} />
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

  if (values['helmChart']) {
    payload[version][importType]['paths'] = {
      ...payload[version][importType]['paths'],
      helmChart: values['helmChart']
    };
  }

  if (values['helmChartChecksum']) {
    payload[version][importType]['paths'] = {
      ...payload[version][importType]['paths'],
      helmChartChecksum: values['helmChartChecksum']
    };
  }

  return payload;
};

export const ImportRelease = (props) => {
  const [importType, setImportType] = useState(props.initialValues.import_type.value);
  const { import_types, initialValues, visible, onHide } = props;

  const importRelease = useMutation((queryParams) => api.importReleases(queryParams), {
    onSuccess: () => {
      toast.success('Release imported successfully!.');
    },
    onError: (error) => {
      const defaultErrorMessage = 'Import release failed';
      toast.error(error?.response?.data?.error ?? defaultErrorMessage);
    }
  });

  const handleSubmit = async (values, { setSubmitting }) => {
    const payload = preparePayload(values);
    setSubmitting(false);
    if (!payload) {
      return;
    }
    await importRelease.mutateAsync(payload);
    props.onModalSubmit();
  };

  const isLoading = importRelease.isLoading;

  return (
    <div className="universe-apps-modal">
      <YBModalForm
        isButtonDisabled={!!isLoading}
        title={'Import Release'}
        visible={visible}
        onHide={onHide}
        showCancelButton={true}
        cancelLabel={'Cancel'}
        className="import-release-modal"
        onFormSubmit={handleSubmit}
        initialValues={initialValues}
        validationSchema={getValidationSchema(importType)}
        render={({ values }) => (
          <>
            {isLoading ? (
              <>
                <YBLoading text={'Importing Release'} />
              </>
            ) : (
              <>
                <Row>
                  <Col lg={12}>
                    <Field name="version" component={YBFormInput} label={'Release Version'} />
                  </Col>
                  <Col lg={12}>
                    <Field
                      name="import_type"
                      component={YBFormSelect}
                      options={import_types}
                      label="Import from"
                      onChange={({ form }, value) => {
                        form.setFieldValue('import_type', value);
                        setImportType(value.value);
                      }}
                    />
                  </Col>
                </Row>
                <Row>
                  <Col lg={12}>{getFields(values['import_type'])}</Col>
                </Row>
              </>
            )}
          </>
        )}
      ></YBModalForm>
    </div>
  );
};
