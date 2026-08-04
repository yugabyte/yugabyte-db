import React, { FC, ReactNode } from 'react';
import clsx from 'clsx';
import {
  makeStyles,
  Accordion,
  AccordionSummary,
  AccordionDetails,
  AccordionProps,
  Theme,
  Box,
  useTheme
} from '@material-ui/core';
import ExpandMoreIcon from '@app/assets/expand-more.svg';

const useAccordionStyles = makeStyles((theme: Theme) => ({
  container: {
    display: 'flex',
    width: '100%'
  },
  shrinkContainer: {
    display: 'flex',
    flexShrink: 1,
    alignSelf: 'center'
  },
  //add more variants
  primary: {
    border: `1px solid ${theme.palette.grey[200]}`,
    '&.MuiAccordion-root': {
      minHeight: '68px !important',
      '&:before': {
        display: 'none'
      }
    },
    '&.MuiAccordion-root.Mui-expanded': {
      minHeight: '68px !important',
      margin: '0 !important'
    },
    // Additional overrides for accordion containers
    '& .MuiAccordion-root': {
      minHeight: '68px !important'
    },
    '& .MuiCollapse-root': {
      minHeight: 'auto'
    }
  },
  summary: {
    height: '68px !important',
    minHeight: '68px !important',
    maxHeight: '68px !important',
    '&.Mui-expanded': {
      minHeight: '68px !important',
      maxHeight: '68px !important',
      margin: '0 !important'
    },
    '&.MuiAccordionSummary-root': {
      minHeight: '68px !important',
      maxHeight: '68px !important'
    },
    '&.MuiAccordionSummary-root.Mui-expanded': {
      minHeight: '68px !important',
      maxHeight: '68px !important',
      margin: '0 !important'
    },
    padding: theme.spacing(3, 3, 3, 2),
    '& .MuiAccordionSummary-content': {
      display: 'flex',
      justifyContent: 'space-between',
      alignItems: 'center',
      margin: '0 !important',
      '&.Mui-expanded': {
        margin: '0 !important'
      }
    }
  },
  graySummaryBg: {
    background: theme.palette.info[400]
  },
  separator: {
    borderBottom: `1px solid ${theme.palette.grey[200]}`,
  },
  title: {
    display: 'flex',
    alignItems: 'center',
    flexGrow: 1,
    fontWeight: theme.typography.body1.fontWeight,
    fontSize: theme.typography.h5.fontSize,
    color: theme.palette.text.primary,
    height: '100%'
  }
}));

interface YBAccordionProps extends AccordionProps {
  titleContent: ReactNode;
  renderChips?: () => ReactNode;
  graySummaryBg?: boolean;
  contentSeparator?: boolean;
}

export const YBAccordion: FC<YBAccordionProps> = ({
  titleContent,
  renderChips,
  graySummaryBg,
  children,
  contentSeparator,
  ...rest
}) => {
  const classes = useAccordionStyles();
  const theme = useTheme();

  return (
    <Box className={classes.container}>
      <Accordion
        {...rest}
        className={classes.primary}
        style={{
          minHeight: theme.spacing(8.5),
          ...rest.style
        }}
      >
        <AccordionSummary
          expandIcon={<ExpandMoreIcon />}
          className={clsx(
            classes.summary,
            classes.container,
            graySummaryBg && classes.graySummaryBg,
            contentSeparator && classes.separator
          )}
          style={{
            minHeight: `${theme.spacing(8.5)} !important`,
            maxHeight: `${theme.spacing(8.5)} !important`,
            height: `${theme.spacing(8.5)} !important`
          }}
        >
          <Box className={classes.title}>{titleContent}</Box>
          {renderChips && <Box className={classes.shrinkContainer}>{renderChips()}</Box>}
        </AccordionSummary>

        <AccordionDetails className={classes.container}>{children}</AccordionDetails>
      </Accordion>
    </Box>
  );
};
