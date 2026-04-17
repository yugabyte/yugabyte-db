import { makeStyles, Typography } from '@material-ui/core';

interface SummaryRowProps {
  label: string;
  children: React.ReactNode;
}

const useStyles = makeStyles((theme) => ({
  container: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  label: {
    fontWeight: 500,
    fontSize: '11.5px',
    lineHeight: '16px',
    color: theme.palette.grey[600]
  },
  value: {
    lineHeight: '16px',
    color: theme.palette.grey[900]
  }
}));

export const SummaryRow = ({ label, children }: SummaryRowProps) => {
  const classes = useStyles();

  return (
    <div className={classes.container}>
      <Typography className={classes.label}>{label}</Typography>
      <Typography variant="body2" className={classes.value}>
        {children}
      </Typography>
    </div>
  );
};
