// Copyright (c) YugaByte, Inc.

import React from 'react';
import './stylesheets/Footer.scss';
import YBLogo from '../YBLogo/YBLogo';
import * as moment from 'moment';
import { getPromiseState } from 'utils/PromiseUtils';

class Footer extends React.PureComponent {
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
            <span> Version: {version}</span>
          }
        </div>
        <div className="flex-grow copyright">
          Copyright &copy; {moment().get('year')} YugaByte. All Rights Reserved. 
        </div>
      </footer>
    );
  }
};

export default Footer;
