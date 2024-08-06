import React, { FC, ReactNode } from 'react';
import clsx from 'clsx';
import {
  makeStyles,
  Accordion,
  AccordionSummary,
  AccordionDetails,
  AccordionProps,
  Theme,
  Box
} from '@material-ui/core';
import ExpandMoreIcon from '@app/assets/expand-more.svg';

const useAccordionStyles = makeStyles((theme: Theme) => ({
  container: {
    display: 'flex',
    width: '100%'
  },
  shrinkContainer: {
    display: 'flex',
    flexShrink: 1
  },
  //add more variants
  primary: {
    border: `1px solid ${theme.palette.grey[200]}`
  },
  summary: {
    minHeight: theme.spacing(6),
    '&.Mui-expanded': {
      minHeight: theme.spacing(2),
      margin: 0
    },
    padding: theme.spacing(0, 2),
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
    fontWeight: 600,
    fontSize: 15,
    color: theme.palette.grey[900]
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

  return (
    <Box className={classes.container}>
      <Accordion {...rest} className={classes.primary}>
        <AccordionSummary
          expandIcon={<ExpandMoreIcon />}
          className={clsx(
            classes.summary,
            classes.container,
            graySummaryBg && classes.graySummaryBg,
            contentSeparator && classes.separator
          )}
        >
          <Box className={classes.title}>{titleContent}</Box>
          {renderChips && <Box className={classes.shrinkContainer}>{renderChips()}</Box>}
        </AccordionSummary>

        <AccordionDetails className={classes.container}>{children}</AccordionDetails>
      </Accordion>
    </Box>
  );
};
