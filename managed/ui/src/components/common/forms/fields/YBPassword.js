// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { InputGroup, Button } from 'react-bootstrap';
import { YBLabel } from '../../../../components/common/descriptors';
import { YBTextInput } from '.';

export default class YBPassword extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showPassword: false
    };
  }

  render() {
    const {
      label,
      meta,
      insetError,
      infoContent,
      infoTitle,
      infoPlacement,
      className,
      ...otherProps
    } = this.props;
    return (
      <YBLabel
        label={label}
        meta={meta}
        insetError={insetError}
        infoContent={infoContent}
        infoTitle={infoTitle}
        infoPlacement={infoPlacement}
      >
        <InputGroup className={className}>
          <YBTextInput {...otherProps} type={this.state.showPassword ? 'text' : 'password'} />
          <InputGroup.Button>
            <Button onClick={() => this.setState({ showPassword: !this.state.showPassword })}>
              <i className={this.state.showPassword ? 'fa fa-eye' : 'fa fa-eye-slash'}></i>
            </Button>
          </InputGroup.Button>
        </InputGroup>
      </YBLabel>
    );
  }
}
