// Copyright (c) YugaByte, Inc.

import LogoFull from './images/yb_yblogo_darkbg.svg';
import LogoMonochrome from './images/yb_yblogo_whitebg.svg';
import LogoIcon from './images/yb_ybsymbol_original.svg';

function YBLogo(props) {
  if (props.type === 'full') {
    return <img src={LogoFull} alt="full" className="logo" />;
  }
  if (props.type === 'monochrome') {
    return <img src={LogoMonochrome} alt="monochrome" className="logo" />;
  }
  return <img src={LogoIcon} alt="default" className="logo" />;
}

export default YBLogo;
