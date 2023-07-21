// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import clsx from 'clsx';

import './YBPanelItem.scss';

export default class YBPanelItem extends Component {
  render() {
    const { noBackground, className, bodyClassName, children } = this.props;
    const panelBodyClassName = clsx('body', noBackground && 'body-transparent', bodyClassName);
    return (
      <div className={className ? 'content-panel ' + className : 'content-panel'}>
        {(this.props.header || this.props.title) && (
          <div className="header">
            {this.props.header} {this.props.title}
          </div>
        )}
        <div className="container">
          {this.props.leftPanel}
          {this.props.body && (
            <div className={panelBodyClassName}>
              {this.props.body}
              {children}
            </div>
          )}
        </div>
      </div>
    );
  }
}

YBPanelItem.defaultProps = {
  hideToolBox: false
};
