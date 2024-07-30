/*
 * Created on Thu May 16 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, isValidElement, useImperativeHandle } from 'react';
import clsx from 'clsx';
import { useToggle } from 'react-use';
import { DiffOperation } from './dtos';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Grid,
  Typography,
  makeStyles
} from '@material-ui/core';
import DiffBadge from './DiffBadge';
import { ArrowDropDown } from '@material-ui/icons';

type diffElem = {
  title?: string;
  element?: JSX.Element;
};

type DiffCardProps = {
  attribute: diffElem;
} & (
  | {
      operation: DiffOperation.CHANGED;
      beforeValue: diffElem;
      afterValue: diffElem;
    }
  | {
      // The following two properties are optional for the ADDED and REMOVED operations
      operation: DiffOperation.ADDED | DiffOperation.REMOVED;
      beforeValue?: diffElem;
      afterValue?: diffElem;
    }
);

const useStyles = makeStyles((theme) => ({
  accordionSummary: {
    padding: '12px',
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    height: '48px'
  },
  header: {
    display: 'flex',
    gap: '16px',
    alignItems: 'center'
  },
  headerOp: {
    display: 'flex',
    gap: '8px',
    alignItems: 'center'
  },
  expandMore: {
    fontSize: theme.spacing(4)
  },
  strikeOut: {
    textDecoration: 'line-through'
  },
  strikeOutRed: {
    '&>span': {
      backgroundColor: '#FEEDED',
      mixBlendMode: 'darken'
    }
  },
  strikeOutGreen: {
    '&>span': {
      backgroundColor: '#CDEFE1',
      mixBlendMode: 'darken'
    }
  },
  ellipsis: {
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
    wordBreak: 'break-all',
    maxWidth: '150px',
    overflow: 'hidden'
  },
  accordionDetails: {
    borderTop: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    display: 'block',
    padding: '12px'
  },
  detailsHeader: {
    display: 'flex',
    gap: '8px',
    '&>div': {
      minWidth: '230px',
      maxWidth: '230px',
      padding: '0px 8px 8px 0px',
      wordBreak: 'break-all'
    }
  },
  seperator: {
    borderRight: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  noPaddingLeft: {
    paddingLeft: `0 !important`
  }
}));

export type DiffCardRef = {
  onExpand: (flag: boolean) => void;
};

const DiffCard = forwardRef<DiffCardRef, React.PropsWithChildren<DiffCardProps>>(
  ({ attribute, operation, beforeValue, afterValue }, forwardRef) => {
    // Implement the component logic here
    const classes = useStyles();
    const [expandAccordion, setExpandAccordion] = useToggle(false);

    useImperativeHandle(forwardRef, () => ({ onExpand: setExpandAccordion }), []);

    const getElementOrPlaceHolder = (value: DiffCardProps['beforeValue']) => {
      if (isValidElement(value?.element)) {
        return value?.element;
      }
      return value?.title ? <span>{value.title}</span> : '---';
    };

    return (
      <Accordion expanded={expandAccordion} onChange={() => setExpandAccordion(!expandAccordion)}>
        <AccordionSummary
          expandIcon={<ArrowDropDown className={classes.expandMore} />}
          className={classes.accordionSummary}
        >
          <div className={classes.header}>
            <div className={classes.headerOp}>
              {!expandAccordion && <DiffBadge type={operation} minimal />}
              <DiffBadge type={DiffOperation.REMOVED} customText={attribute.title} hideIcon />
            </div>
            {!expandAccordion && (
              <>
                <Typography
                  variant="body2"
                  title={beforeValue?.title}
                  className={clsx(
                    operation !== DiffOperation.ADDED && classes.strikeOut,
                    classes.ellipsis
                  )}
                >
                  {operation !== DiffOperation.ADDED && beforeValue?.title}
                </Typography>
                <Typography variant="body2" title={afterValue?.title} className={classes.ellipsis}>
                  {afterValue?.title}
                </Typography>
              </>
            )}
          </div>
        </AccordionSummary>
        <AccordionDetails className={classes.accordionDetails}>
          <Grid className={classes.detailsHeader}>
            <Grid item></Grid>
            <Grid item className={classes.noPaddingLeft}>
              <Typography variant="subtitle2">Before</Typography>
            </Grid>
            <Grid item className={classes.noPaddingLeft}>
              <Typography variant="subtitle2">After</Typography>
            </Grid>
          </Grid>
          <Grid className={classes.detailsHeader}>
            <Grid item className={classes.seperator}>
              {attribute.element ?? attribute.title}
            </Grid>
            <Grid
              item
              className={clsx(
                beforeValue && classes.strikeOutRed,
                beforeValue && classes.strikeOut
              )}
            >
              {getElementOrPlaceHolder(beforeValue)}
            </Grid>
            <Grid item className={clsx(afterValue && classes.strikeOutGreen)}>
              {getElementOrPlaceHolder(afterValue)}
            </Grid>
          </Grid>
        </AccordionDetails>
      </Accordion>
    );
  }
);

DiffCard.displayName = 'DiffCard';

export default DiffCard;
