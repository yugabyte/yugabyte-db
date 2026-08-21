import { forwardRef } from 'react';
import {
  Accordion,
  AccordionDetails,
  type AccordionProps,
  AccordionSummary,
  makeStyles,
  Typography
} from '@material-ui/core';
import clsx from 'clsx';

import WarningIcon from '@app/redesign/assets/approved/alert-solid.svg';
import ErrorIcon from '@app/redesign/assets/approved/status-failed.svg';
import ExpandMoreIcon from '@app/redesign/assets/approved/triangle-arrow-down.svg';
import CheckIcon from '@app/redesign/assets/check.svg';
import LoadingIcon from '@app/redesign/assets/default-loading-circles.svg';

export const AccordionCardState = {
  NEUTRAL: 'NEUTRAL',
  IN_PROGRESS: 'IN_PROGRESS',
  SUCCESS: 'SUCCESS',
  WARNING: 'WARNING',
  FAILED: 'FAILED'
} as const;
export type AccordionCardState = (typeof AccordionCardState)[keyof typeof AccordionCardState];

interface AccordionCardProps {
  state: AccordionCardState;
  title: string;

  accordionProps?: Omit<AccordionProps, 'children'>;
  headerAccessories?: React.ReactNode;
  className?: string;
  stepNumber?: number;
  // Disable ability to expand without using the disabled
  // card state.
  isExpandDisabled?: boolean;
  children?: React.ReactNode;
}

const useStyles = makeStyles((theme) => ({
  accordionRoot: {
    '&:before': {
      display: 'none'
    },
    '&.Mui-expanded': {
      margin: 0
    },
    '&.Mui-disabled': {
      backgroundColor: theme.palette.grey[100]
    }
  },
  accordionSummaryRoot: {
    padding: 0,
    minHeight: 0,

    '&.Mui-expanded': {
      minHeight: 'unset'
    }
  },
  accordionSummaryExpandDisabled: {
    cursor: 'text !important',
    userSelect: 'text'
  },
  accordionSummaryContent: {
    display: 'flex',
    alignItems: 'center',

    margin: 0,

    '&.Mui-expanded': {
      margin: 0
    }
  },
  accordionSummaryExpandIcon: {
    padding: 0
  },
  accordionDetailRoot: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    marginLeft: theme.spacing(4)
  },
  accordionCard: {
    display: 'flex',
    position: 'relative',

    padding: theme.spacing(1.5, 2),

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius
  },
  step: {
    '&:not(:last-child)::after': {
      position: 'absolute',
      bottom: `calc(${theme.spacing(-2)}px - 1px)`,
      left: theme.spacing(4),
      transform: 'translateX(-50%)',

      height: theme.spacing(2),
      width: '1px',

      content: '""',
      backgroundColor: theme.palette.grey[400]
    }
  },
  headerPrimarySection: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2)
  },
  iconContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    height: 32,
    width: 32,

    borderRadius: '50%',
    backgroundColor: theme.palette.grey[100],

    '&$disabledState': {
      backgroundColor: theme.palette.grey[200],
      '& $icon': {
        color: theme.palette.grey[600]
      }
    },
    '&$inProgressState': {
      backgroundColor: theme.palette.primary[200],
      '& $icon': {
        height: 16,
        width: 16,
        color: theme.palette.primary[600]
      }
    },
    '&$successState': {
      backgroundColor: theme.palette.success[50],
      '& $icon': {
        color: theme.palette.success[500]
      }
    },
    '&$warningState': {
      backgroundColor: theme.palette.warning[100],
      '& $icon': {
        color: theme.palette.warning[500]
      }
    },
    '&$failedState': {
      backgroundColor: theme.palette.error[100],
      '& $icon': {
        color: theme.palette.error[500]
      }
    }
  },
  neutralState: {},
  disabledState: {},
  inProgressState: {},
  successState: {},
  warningState: {},
  failedState: {},
  icon: {}
}));

export const AccordionCard = forwardRef<HTMLElement, AccordionCardProps>(({
  state,
  title,
  accordionProps,
  headerAccessories,
  className,
  isExpandDisabled = false,
  stepNumber,
  children
}, ref) => {
  const classes = useStyles();
  const stepStateToIcon = {
    [AccordionCardState.NEUTRAL]: {
      className: classes.neutralState,
      iconContent: <Typography variant="subtitle1">{stepNumber}</Typography>
    },
    [AccordionCardState.IN_PROGRESS]: {
      className: classes.inProgressState,
      iconContent: <LoadingIcon className={classes.icon} />
    },
    [AccordionCardState.SUCCESS]: {
      className: classes.successState,
      iconContent: <CheckIcon className={classes.icon} />
    },
    [AccordionCardState.WARNING]: {
      className: classes.warningState,
      iconContent: <WarningIcon className={classes.icon} height={14} width={14} />
    },
    [AccordionCardState.FAILED]: {
      className: classes.failedState,
      iconContent: <ErrorIcon className={classes.icon} />
    }
  };
  return (
    <Accordion
      {...accordionProps}
      ref={ref}
      expanded={isExpandDisabled ? false : accordionProps?.expanded}
      className={clsx(classes.accordionCard, classes.step, className)}
      classes={{ root: classes.accordionRoot }}
      data-testid={`accordion-card-${title?.toLowerCase().replace(/ /g, '-')}`}
    >
      <AccordionSummary
        data-testid={`accordion-card-summary-${title?.toLowerCase().replace(/ /g, '-')}`}
        expandIcon={isExpandDisabled ? null : <ExpandMoreIcon />}
        classes={{
          root: clsx(
            classes.accordionSummaryRoot,
            isExpandDisabled && classes.accordionSummaryExpandDisabled
          ),
          content: classes.accordionSummaryContent,
          expandIcon: classes.accordionSummaryExpandIcon
        }}
      >
        <div className={classes.headerPrimarySection}>
          <div
            className={clsx(
              classes.iconContainer,
              accordionProps?.disabled ? classes.disabledState : stepStateToIcon[state].className
            )}
          >
            {stepStateToIcon[state].iconContent}
          </div>
          <Typography variant="body1">{title}</Typography>
        </div>
        {headerAccessories}
      </AccordionSummary>
      <AccordionDetails
        classes={{ root: classes.accordionDetailRoot }}
        data-testid={`accordion-card-details-${title?.toLowerCase().replace(/ /g, '-')}`}
      >
        {children ?? null}
      </AccordionDetails>
    </Accordion>
  );
});
AccordionCard.displayName = 'AccordionCard';
