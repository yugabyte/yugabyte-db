// Copyright (c) YugaByte, Inc.

import React, { PureComponent } from 'react';
import './stylesheets/Footer.scss';
import YBLogo from '../YBLogo/YBLogo';
import * as moment from 'moment';
import { getPromiseState } from 'utils/PromiseUtils';

class Footer extends PureComponent {
  render() {
    const { customer: { yugawareVersion } } = this.props;
    const version = getPromiseState(yugawareVersion).isSuccess()
        ? yugawareVersion.data.version
        : null;
    return (
      <footer>
        <div className="flex-grow">
          <YBLogo type="monochrome" />
          { version &&
            <span> Version: {version.substr(0, version.indexOf("-"))}</span>
          }
        </div>
        <div className="flex-grow copyright">
          Copyright &copy; 2016 - {moment().get('year')} YugaByte. All Rights Reserved. 
        </div>
      </footer>
    );
  }
};

export default Footer;
