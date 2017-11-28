// Copyright (c) YugaByte, Inc.

import React from 'react';
import './YBFlexBox.scss';

const FlexContainer = ({ className, ...props }) => (
  <div className={"flex-cnt "+className}>
    {props.children}
  </div>
);

const FlexGrow = ({ power, ...props }) => (
  <div className="flex-grow" style={{flexGrow: power}}>
    {props.children}
  </div>
);

const FlexShrink = ({ power, ...props }) => (
  <div className="flex-shrink" style={{flexGrow: power}}>
    {props.children}
  </div>
);

export { FlexContainer, FlexGrow, FlexShrink };
