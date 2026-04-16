// Copyright (c) YugabyteDB, Inc.

import LogoFull from './images/yb_yblogo_darkbg.svg?img';
import LogoMonochrome from './images/yb_yblogo_whitebg.svg?img';
import LogoIcon from './images/yb_ybsymbol_original.svg?img';

function YBLogo(props) {
  if (props.type === 'full') {
    return <img src={LogoFull} className="logo" alt="full" />;
  }
  if (props.type === 'monochrome') {
    return <img src={LogoMonochrome} className="logo" alt="monochrome" />;
  }
  return <img src={LogoIcon} className="logo" alt="default" />;
}

export default YBLogo;
