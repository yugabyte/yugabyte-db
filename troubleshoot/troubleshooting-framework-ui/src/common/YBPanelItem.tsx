// // Copyright (c) YugaByte, Inc.

import { makeStyles } from '@material-ui/core';
import clsx from 'clsx';

export interface YBPanelItemProps {
  noBackground?: boolean;
  className?: any;
  bodyClassName?: any;
  children?: any;
  header?: any;
  body: any;
  title?: string;
}

const useStyles = makeStyles((theme) => ({
  header: {
    minHeight: '42px',
    marginBottom: '25px'
  },
  contentPanel: {
    margin: 0
  },
  transparentBody: {
    padding: 0,
    backgroundColor: 'transparent',
    boxShadow: 'none'
  },
  body: {
    position: 'relative',
    // margin: '0px',
    boxShadow: `0 0.12em 2px rgba(colors.$YB_DARK_GRAY_2, 0.05), 0 0.5em 10px rgba(colors.$YB_DARK_GRAY_2, 0.07)`,
    border: 'none',
    backgroundColor: '#fff',
    padding: '20px 25px 25px 25px',
    borderRadius: '7px',
    margin: '20px 0 0 auto'
  }
}));

export const YBPanelItem = ({
  noBackground,
  className,
  bodyClassName,
  children,
  body,
  header,
  title
}: YBPanelItemProps) => {
  const classes = useStyles();
  const panelBodyClassName = clsx(
    classes.body,
    noBackground && classes.transparentBody,
    bodyClassName
  );

  return (
    <div className={className ? classes.contentPanel + className : 'classes.contentPanel'}>
      {(header || title) && (
        <div className={classes.header}>
          {header} {title}
        </div>
      )}
      <div className="container">
        {body && (
          <div className={panelBodyClassName}>
            {body}
            {children}
          </div>
        )}
      </div>
    </div>
  );
};
