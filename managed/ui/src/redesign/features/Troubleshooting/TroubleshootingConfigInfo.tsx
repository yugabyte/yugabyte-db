import { TS_SERVER_IP } from '@yugabytedb/troubleshoot-ui';
import { Box, makeStyles } from '@material-ui/core';
import { YBInput, YBLabel } from '../../components';
import { YBPanelItem } from '../../../components/panels';
import { IN_DEVELOPMENT_MODE, ROOT_URL } from '../../../config';

const useStyles = makeStyles((theme) => ({
  infoBox: {
    marginTop: theme.spacing(2),
    display: 'flex',
    flexDirection: 'row'
  },
  textBox: {
    width: '400px'
  }
}));

export const TroubleshootingConfigInfo = () => {
  const helperClasses = useStyles();
  const baseUrlSplit = ROOT_URL.split('/api/');
  const baseUrl = baseUrlSplit[0];

  return (
    <YBPanelItem
      body={
        <>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="UniverseNameField-Label">{'Troubleshoot URL'}</YBLabel>
            <YBInput
              name="id"
              type="text"
              value={TS_SERVER_IP}
              disabled
              className={helperClasses.textBox}
            />
          </Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="UniverseNameField-Label">{'Platform URL'}</YBLabel>
            <YBInput type="text" disabled value={baseUrl} className={helperClasses.textBox} />
          </Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="UniverseNameField-Label">{'Metrics URL'}</YBLabel>
            <YBInput
              type="text"
              disabled
              value={IN_DEVELOPMENT_MODE ? 'http://localhost:9090/' : `${baseUrl}:9090`}
              className={helperClasses.textBox}
            />
          </Box>
        </>
      }
    />
  );
};
