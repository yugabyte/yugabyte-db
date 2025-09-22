import { FC } from 'react';
import { makeStyles } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { ReactComponent as AlertIcon } from '../../../../../../redesign/assets/alert.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '16px',
    borderRadius: '8px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    backgroundColor: theme.palette.ybacolors.backgroundGrayLight
  },
  content: {
    display: 'flex',
    gap: '16px',
    flexDirection: 'column',
    color: theme.palette.grey[700]
  },
  link: {
    color: theme.palette.grey[700],
    textDecoration: 'underline',
    cursor: 'pointer',
    '&:hover, &:focus': {
      color: theme.palette.grey[700],
      textDecoration: 'underline'
    }
  },
  icon: {
    width: '42px',
    height: '42px'
  },
  bold: {
    fontWeight: 600
  },
  warning: {
    display: 'flex',
    gap: '8px'
  }
}));

interface TablespaceUnSupportedProps {
  loggingID?: string;
}

export const TablespaceUnSupported: FC<TablespaceUnSupportedProps> = ({ loggingID }) => {
  const { t } = useTranslation('translation', { keyPrefix: 'backup.restore.target' });
  const classes = useStyles();
  return (
    <div className={classes.root}>
      <div className={classes.content}>
        <span>
          <Trans
            i18nKey="tablespaceUnsupportedText1"
            t={t}
            components={{
              b: <b className={classes.bold} />,
              a: (
                <a
                  className={classes.link}
                  href={`/logs?queryRegex=${loggingID}`}
                  target="_blank"
                  rel="noreferrer"
                />
              )
            }}
          />
        </span>
        <span>
          <div className={classes.warning}>
            <AlertIcon className={classes.icon} />
            <Trans
              i18nKey="tablespaceUnsupportedText2"
              t={t}
              components={{ b: <b className={classes.bold} /> }}
            />
          </div>
        </span>
        <span>
          <Trans
            i18nKey="tablespaceUnsupportedText3"
            t={t}
            components={{
              a: (
                <a
                  className={classes.link}
                  href={`https://docs.yugabyte.com/preview/yugabyte-platform/back-up-restore-universes/restore-universe-data/#prerequisites`}
                  target="_blank"
                  rel="noreferrer"
                />
              )
            }}
          />
        </span>
      </div>
    </div>
  );
};
