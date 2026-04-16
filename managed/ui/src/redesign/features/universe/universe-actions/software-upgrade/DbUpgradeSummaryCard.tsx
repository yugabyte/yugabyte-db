import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbRollbackUpgrade';

const useStyles = makeStyles((theme) => ({
  infoCard: {
    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  infoCardHeader: {
    padding: `${theme.spacing(2.5)}px ${theme.spacing(2)}px`,
    borderBottom: `1px solid ${theme.palette.grey[200]}`,
    fontWeight: 600,
    fontSize: 13
  },
  summaryBody: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3.5),
    padding: `${theme.spacing(3.5)}px ${theme.spacing(2)}px`
  },
  summaryItem: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5)
  },
  summaryLabel: {
    fontWeight: 500,
    fontSize: '11.5px',
    lineHeight: '16px',
    textTransform: 'uppercase',
    color: '#6D7C88'
  },
  summaryValue: {
    fontWeight: 400,
    fontSize: 13,
    lineHeight: '16px',
    color: '#0B1117',
    padding: theme.spacing(0.5, 0)
  }
}));

export const DbUpgradeSummaryCard = () => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  // TODO: Read form values via useFormContext / useWatch to build a real summary.
  // For now, render placeholder content.

  return (
    <div className={classes.infoCard}>
      <Typography className={classes.infoCardHeader} variant="body1">
        Summary
      </Typography>
      <div className={classes.summaryBody}>
        <SummaryItem classes={classes} label={t('targetVersion')} value="—" />
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────
// Sub-components
// ─────────────────────────────────────────────────────────────

const SummaryItem = ({
  classes,
  label,
  value
}: {
  classes: ReturnType<typeof useStyles>;
  label: string;
  value: string;
}) => (
  <div className={classes.summaryItem}>
    <Typography className={classes.summaryLabel}>{label}</Typography>
    <Typography className={classes.summaryValue}>{value}</Typography>
  </div>
);
