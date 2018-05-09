// Copyright (c) YugaByte, Inc.

import React, { Fragment, Component } from 'react';
import Dropzone from 'react-dropzone';

import './stylesheets/YBDropZone.scss';

export default class YBDropZone extends Component {
  onDrop = (file, e) => {
    this.props.input.onChange(file[0]);
  }
  render() {
    const { input, title, meta: { touched, error } } = this.props;
    return (
      <Fragment>
        <div className={`form-group ${ touched && error ? 'has-error' : ''}`}>
          <Dropzone
            className={this.props.className}
            name={this.props.name}
            onDrop={this.onDrop}
            >
            <p>{title}</p>
          </Dropzone>
          { touched && error &&
            <span className="help-block standard-error">{error}</span>}
        </div>
        {input.value  && (
          <span className="drop-zone-file"> {input.value.name}</span>
        )}
      </Fragment>
    );
  }
}
