// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import _ from 'lodash';

import { YBDropZone } from '../../../configRedesign/providerRedesign/components/YBDropZone/YBDropZone';

import './stylesheets/YBDropZone.scss';

export default class YBFormDropZone extends Component {
  onDrop = (acceptedFile, e) => {
    const { name } = this.props.field;
    const { setFieldValue, setFieldTouched } = this.props.form;

    setFieldValue(name, acceptedFile);
    setFieldTouched(name, true);
  };

  render() {
    const {
      title,
      field: { name },
      form,
      accept
    } = this.props;
    const { errors, touched } = form;
    const error = _.get(errors, name);
    const hasError = error && (_.get(touched, name) || form.submitCount > 0);

    return (
      <>
        <YBDropZone
          className={this.props.className}
          accept={accept}
          value={this.props.field.value}
          actionButtonText={title}
          multipleFiles={false}
          name={name}
          onChange={this.onDrop}
          showHelpText={false}
          disabled={!!this.props.disabled}
        />
        {hasError && <span className="help-block standard-error">{error}</span>}
      </>
    );
  }
}
