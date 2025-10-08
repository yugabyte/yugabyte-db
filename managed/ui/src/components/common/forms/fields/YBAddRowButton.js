// Copyright (c) YugabyteDB, Inc.

import { YBButton } from './';

const YBAddRowButton = ({ onClick, btnText, ...props }) => (
  <YBButton
    btnClass="yb-add-button"
    btnIcon="fa fa-plus"
    btnText={btnText}
    onClick={onClick}
    {...props}
  />
);

export default YBAddRowButton;
