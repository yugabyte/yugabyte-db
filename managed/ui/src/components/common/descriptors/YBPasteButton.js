// Copyright (c) YugaByte, Inc.

import { PureComponent } from 'react';
import { toast } from 'react-toastify';

export default class YBPasteButton extends PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      clicked: false
    };
  }

  onClick = (event) => {
    const { onPaste } = this.props;

    navigator.clipboard
      .readText()
      .then((clipText) => {
        //set and reset clicked - to show/hide tick icon
        this.setState({ clicked: true });
        setTimeout(() => {
          this.setState({ clicked: false });
        }, 1500);

        //return clipboard contents to callback method
        clipText && onPaste(clipText);
      })
      .catch(() => {
        toast.error('Something went wrong. Could not access clipboard.', { autoClose: 2000 });
      });

    event.preventDefault();
  };

  render() {
    const { className } = this.state.clicked
      ? { className: '' }
      : { className: ' btn-paste-inactive' };

    if (window.isSecureContext && navigator?.clipboard?.readText)
      return (
        <button
          {...this.props}
          className={'btn btn-small btn-paste btn-paste-w' + className}
          onClick={this.onClick}
        >
          Paste
        </button>
      );
    return null;
  }
}
