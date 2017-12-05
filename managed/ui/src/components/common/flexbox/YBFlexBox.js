// Copyright (c) YugaByte, Inc.

import React from 'react';
import './YBFlexBox.scss';

const FlexContainer = ({ className, ...props }) => (
  <div className={"flex-cnt "+className} {...props}>
    {props.children}
  </div>
);

const FlexGrow = ({ power, ...props }) => (
  <div {...props} className={ props.className ? "flex-grow " + props.classNames : "flex-grow"} style={{...props.style, flexGrow: power}}>
    {props.children}
  </div>
);

const FlexShrink = ({ power, ...props }) => (
  <div {...props} className={ props.className ? "flex-shrink " + props.className : "flex-shrink" } style={{...props.style}}>
    {props.children}
  </div>
);

export { FlexContainer, FlexGrow, FlexShrink };
