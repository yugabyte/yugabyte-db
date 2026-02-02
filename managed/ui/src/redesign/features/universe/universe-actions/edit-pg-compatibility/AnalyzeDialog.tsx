import { FC } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { Box, makeStyles, Typography, Chip, Link } from '@material-ui/core';
import { YBModal } from '../../../../components';

//icons
import ReminderIcon from '../../../../assets/reminder.svg';

interface AnalyzeDialogProps {
  open: boolean;
  onClose: () => void;
}

const useStyles = makeStyles((theme) => ({
  innerContainer: {
    height: 'auto',
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(2),
    backgroundColor: '#FCFCFC',
    border: '1px solid #D7DEE4',
    borderRadius: '8px'
  },
  chip: {
    height: 24,
    width: 'fit-content',
    padding: '4px 6px',
    borderRadius: '6px',
    backgroundColor: '#FFFFFF',
    border: '1px solid #D7DEE4',
    fontFamily: 'Menio',
    color: '#0B1117',
    fontSize: '13px',
    fontWeight: 400
  }
}));

export const AnalyzeDialog: FC<AnalyzeDialogProps> = ({ open, onClose }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  return (
    <YBModal
      open={open}
      title={t('common.reminder')}
      titleIcon={<ReminderIcon />}
      onClose={onClose}
      cancelLabel={t('common.close')}
      cancelTestId="AnalyzeDialog-Cancel"
      overrideHeight={'auto'}
      overrideWidth={'600px'}
      dialogContentProps={{ style: { padding: '32px 16px 32px 24px' }, dividers: true }}
    >
      <Box className={classes.innerContainer}>
        <Typography variant="body2">
          <Trans i18nKey={'universeActions.pgCompatibility.analyzeModal.msg1'} />
          <br />
          <br />
          {t('universeActions.pgCompatibility.analyzeModal.msg2')}&nbsp;
          <Chip label="ANALYZE ;" className={classes.chip} size="small" />
          &nbsp;
          {t('universeActions.pgCompatibility.analyzeModal.msg3')}
          <br />
          <br />
          <Link
            underline="always"
            target="_blank"
            href="https://docs.yugabyte.com/preview/api/ysql/the-sql-language/statements/cmd_analyze/#analyze-affects-query-plans"
          >
            {t('universeActions.pgCompatibility.analyzeModal.learnLink')}
          </Link>
        </Typography>
      </Box>
    </YBModal>
  );
};
