// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Alert } from 'react-bootstrap';
import PropTypes from 'prop-types';
import { YBFormInput, YBFormDatePicker, YBFormDropZone } from '../common/forms/fields';
import { getPromiseState } from '../../utils/PromiseUtils';
import { YBModalForm } from '../common/forms';
import { isDefinedNotNull, isNonEmptyObject } from '../../utils/ObjectUtils';
import { Field } from 'formik';
import * as Yup from "yup";

import MomentLocaleUtils, {
  formatDate,
  parseDate,
} from 'react-day-picker/moment';

import moment from 'moment';

export default class AddCertificateForm extends Component {
  static propTypes = {
    backupInfo: PropTypes.object
  };

  readUploadedFileAsText = (inputFile, isRequired) => {
    const fileReader = new FileReader();
    return new Promise((resolve, reject) => {
      fileReader.onloadend = () => {
        resolve(fileReader.result);
      };
      // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
      if (isDefinedNotNull(inputFile)) {
        fileReader.readAsText(inputFile);
      }
      if (!isRequired && !isDefinedNotNull(inputFile)) {
        resolve(null);
      }
    });
  };

  addCertificate = (vals, setSubmitting) => {
    const self = this;
    const certificateFile = vals.certContent;
    const keyFile = vals.keyContent;
    const formValues = {
      label: vals.label,
      certStart: moment.utc().format("x"),
      certExpiry: moment.utc(vals.certExpiry).format("x")
    };
    const fileArray = [
      this.readUploadedFileAsText(certificateFile, false),
      this.readUploadedFileAsText(keyFile, false)
    ];

    // Catch all onload events for configs
    Promise.all(fileArray).then(files => {
      formValues.certContent = files[0];
      formValues.keyContent = files[1];
      self.props.addCertificate(formValues, setSubmitting);
    }, reason => {
      console.warn("File Upload gone wrong. "+reason);
      setSubmitting(false);
    });
  }

  onHide = () => {
    this.props.onHide();
    this.props.addCertificateReset();
  }

  render() {
    const { customer: { addCertificate }} = this.props;
    const validationSchema = Yup.object().shape({
      certContent: Yup.mixed().required('Certificate file is required'),
      keyContent: Yup.mixed().required('Key file is required'),
      label: Yup.string().required('Certificate name is required'),
      certExpiry: Yup.date().typeError('Set a valid expiration date').required('Expiry date is required')
    });

    const initialValues = {
      certContent: null,
      keyContent: null,
      label: "",
      certExpiry: null
    };

    return (
      <div className="universe-apps-modal">
        <YBModalForm
          title={"Add Certificate"}
          className={getPromiseState(addCertificate).isError() ? "modal-shake" : ""}
          visible={this.props.visible}
          onHide={this.onHide}
          showCancelButton={true}
          submitLabel={"Add"}
          cancelLabel={"Cancel"}
          onFormSubmit={(values, {setSubmitting}) => {
            setSubmitting(true);
            const payload = {
              ...values,
              label: values.label.trim()
            };
            this.addCertificate(payload, setSubmitting);
          }}
          initialValues={initialValues}
          validationSchema={validationSchema}
          render={(props) => (
            <Fragment>
              <Field name="certContent" component={YBFormDropZone}
                className="upload-file-button"
                title={"Upload Certificate File"} />

              <Field name="keyContent" component={YBFormDropZone}
                className="upload-file-button"
                title={"Upload Key File"} />

              <Field name="label"
                component={YBFormInput}
                label={"Name Certificate"} />

              <Field name="certExpiry"
                component={YBFormDatePicker}
                label={"Expiration Date"}
                formatDate={formatDate}
                parseDate={parseDate}
                format="LL"
                placeholder={`${formatDate(new Date(), 'LL', 'en')}`}
                dayPickerProps={{
                  localeUtils: MomentLocaleUtils,
                  disabledDays: {
                    before: new Date(),
                  }
                }}
                onDayChange={(val) => props.setFieldValue("certExpiry", val)}
                pickerComponent={YBFormInput}
              />
              { getPromiseState(addCertificate).isError() &&
                isNonEmptyObject(addCertificate.error) &&
                <Alert bsStyle={'danger'} variant={'danger'}>
                  Certificate adding has been failed:<br/>
                  {JSON.stringify(addCertificate.error)}
                </Alert>
              }
            </Fragment>
          )}
        />
      </div>
    );
  }
}
