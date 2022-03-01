// Copyright (c) YugaByte, Inc.

import React, { PureComponent } from 'react';

export default class YBPasteButton extends PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      clicked: false
    };
  }

  onClick = (event) => {
    const { onPaste } = this.props;
    this.setState({ clicked: true });

    navigator.clipboard.readText().then((clipText) => {
      onPaste(clipText);
    });

    setTimeout(() => {
      this.setState({ clicked: false });
    }, 1500);
    event.preventDefault();
  };

  render() {
    const { className } = this.state.clicked
      ? { className: '' }
      : { className: ' btn-paste-inactive' };
    return (
      <button
        {...this.props}
        className={'btn btn-small btn-paste btn-paste-w' + className}
        onClick={this.onClick}
      >
        Paste
      </button>
    );
  }
}
