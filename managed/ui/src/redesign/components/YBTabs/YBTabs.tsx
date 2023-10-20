/*
 * Created on Tue Sep 26 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Tab, TabProps, Tabs, withStyles } from "@material-ui/core";
import { TabPanelProps } from "@material-ui/lab";

export const YBTabs = withStyles((theme) => ({
  root: {
    borderBottom: '1px solid #e8e8e8'
  },
  indicator: {
    backgroundColor: `${theme.palette.grey[900]} !important`,
    maxWidth: '75px'
  },
}))(Tabs);

export const YBTab = withStyles((theme) => ({
  root: {
    textTransform: 'none',
    minWidth: 72,
    marginRight: theme.spacing(2),
    fontWeight: 400
  }
}))((props: TabProps) => <Tab disableRipple {...props} />);


export function YBTabPanel(props: TabPanelProps) {
  const { children, value, tabIndex, ...other } = props;

  return (
    <div
      role="tabpanel"
      hidden={value !== tabIndex as any}
      id={`simple-tabpanel-${tabIndex}`}
      aria-labelledby={`simple-tab-${tabIndex}`}
      {...other as any}
    >
      {value === tabIndex as any && (
        <>
          {children}
        </>
      )}
    </div>
  );
}
