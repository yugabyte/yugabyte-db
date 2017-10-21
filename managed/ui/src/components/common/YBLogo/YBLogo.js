// Copyright (c) YugaByte, Inc.

import React from 'react';
import LogoFull from './images/yb_yblogo_darkbg.svg';
import LogoIcon from './images/yb_ybsymbol_original.svg';

function YBLogo(props) {
  if(props.size==="full") {
    return <object className="logo" data={LogoFull} type="image/svg+xml">Yugabyte Logo</object>;
  }
  return <object className="logo" data={LogoIcon} type="image/svg+xml">Yugabyte Logo</object>;
}

export default YBLogo;
