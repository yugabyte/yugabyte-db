import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { makeStyles, Box, Typography } from '@material-ui/core';
import { intlFormat } from 'date-fns';
import { YBButton, YBCodeBlock, YBModal, AlertVariant } from '@app/components';
import { useToast } from '@app/helpers';
import CopyIcon from '@app/assets/copy.svg';

const useStyles = makeStyles((theme) => ({
  dropdown: {
    width: 105,
    marginRight: theme.spacing(1),
    flexGrow: 0
  },
  searchBox: {
    maxWidth: 520,
    flexGrow: 1,
    marginRight: 'auto'
  },
  codeBlock: {
    lineHeight: 1.2,
    padding: theme.spacing(1, 1, 1, 0.5)
  },
  queryStatLabel: {
    marginBottom: theme.spacing(0.5)
  },
  queryInfo: {
    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2)
  }
}));

export interface QueryRowEntry {
  id: string;
  query: string;
  [key: string]: string | number | undefined;
}

export interface QueryDetailsProps {
  open: boolean;
  onClose: () => void;
  title: string;
  currentEntry?: QueryRowEntry;
}

export const QueryDetailsPanel: FC<QueryDetailsProps> = ({ open, onClose, title, currentEntry }) => {
  const classes = useStyles();
  const { t, i18n } = useTranslation();
  const { addToast } = useToast();
  const copyStatement = async () => {
    if (currentEntry) {
      await navigator.clipboard.writeText(currentEntry.query);
      addToast(AlertVariant.Success, t('common.copyCodeSuccess'));
    }
  };

  return (
    <YBModal size="xl" isSidePanel title={title} titleSeparator cancelLabel={'Close'} onClose={onClose} open={open}>
      <Box mt={2}>
        <YBCodeBlock text={currentEntry?.query || 'No query data'} preClassName={classes.codeBlock} />
        <Box py={1} pr={0.5} textAlign="right">
          <YBButton onClick={copyStatement} startIcon={<CopyIcon />}>
            {'Copy Statement'}
          </YBButton>
        </Box>
        <Box display="flex" mt={1} flexWrap="wrap" className={classes.queryInfo}>
          {currentEntry &&
            Object.keys(currentEntry).map((key) => {
              if (key !== 'id' && key !== 'queryid' && key !== 'query') {
                let data = currentEntry[key];
                const isTimeData = ['min_time', 'max_time', 'total_time', 'mean_time', 'elapsed_millis'].includes(key);
                if (key === 'query_start_time' && typeof data === 'string' && data !== '') {
                  data = intlFormat(Date.parse(data), {
                    year: 'numeric',
                    month: '2-digit',
                    day: '2-digit',
                    hour: '2-digit',
                    minute: '2-digit',
                    // @ts-ignore: Parameter is not yet supported by `date-fns` but
                    // is supported by underlying Intl.DateTimeFormat. CLOUDGA-5283
                    hourCycle: 'h23'
                  });
                }
                if (key === 'client_port') {
                  data = !data || data === '' ? '-' : data;
                }
                return (
                  <Box flex="1 0 50%" my={1}>
                    <Typography variant="button" color="textSecondary" className={classes.queryStatLabel}>
                      {i18n.exists(`clusterDetail.performance.queryDetailsPanel.${key}`)
                        ? t((`clusterDetail.performance.queryDetailsPanel.${key}` as unknown) as TemplateStringsArray)
                        : key}
                    </Typography>
                    <Typography variant="body2">
                      {data}
                      {isTimeData ? ' ms' : ''}
                    </Typography>
                  </Box>
                );
              }
              return null;
            })}
        </Box>
      </Box>
    </YBModal>
  );
};
