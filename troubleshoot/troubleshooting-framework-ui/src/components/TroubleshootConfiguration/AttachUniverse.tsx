import { Box, makeStyles } from '@material-ui/core';
import { useForm } from 'react-hook-form';
import { MetadataFields } from '../../helpers/dtos';
import { YBModal, YBInputField } from '@yugabytedb/ui-components';
import { useMutation } from 'react-query';
import { TroubleshootAPI } from '../../api';
import { toast } from 'react-toastify';

const useStyles = makeStyles((theme) => ({
  flexColumn: {
    display: 'flex',
    flexDirection: 'column',
    width: '600px',
    overflow: 'auto',
    marginLeft: theme.spacing(2)
  },
  input: {
    marginTop: theme.spacing(2)
  },
  inProgressIcon: {
    color: '#1A44A5'
  },
  icon: {
    height: '14px',
    width: '14px'
  },
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  }
}));

interface AttachUniverseProps {
  universeUuid: string;
  customerUuid: string;
  apiToken: string;
  baseUrl: string;
  open: boolean;
  isDevMode: boolean;
  onClose: () => void;
}

export const AttachUniverse = ({
  universeUuid,
  customerUuid,
  apiToken,
  baseUrl,
  open,
  isDevMode,
  onClose
}: AttachUniverseProps) => {
  const helperClasses = useStyles();

  const formMethods = useForm<MetadataFields>({
    defaultValues: {
      id: universeUuid,
      customerId: customerUuid,
      apiToken: apiToken,
      platformUrl: baseUrl,
      metricsUrl: isDevMode ? 'http://localhost:9090' : `${baseUrl}:9090`,
      metricsScrapePeriodSec: 30
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });

  const {
    control,
    getValues,
    reset,
    formState: { isDirty }
  } = formMethods;

  const troubleshootUniverse = useMutation(
    (payload: MetadataFields) => TroubleshootAPI.updateUniverseMetadata(payload.id, payload),
    {
      onSuccess: (response: any) => {
        toast.success('Universe is successfully added to troubleshooting service');
        onClose();
      },

      onError: () => {
        reset();
        toast.error('Unable to add universe to troubleshooting service');
        onClose();
      }
    }
  );

  const handleFormSubmit = () => {
    const {
      id,
      customerId,
      apiToken,
      platformUrl,
      metricsUrl,
      metricsScrapePeriodSec
    } = getValues();
    const payload = {
      id,
      customerId,
      apiToken,
      platformUrl,
      metricsUrl,
      metricsScrapePeriodSec,
      dataMountPoints: ['/mnt/d0'],
      otherMountPoints: ['/']
    };
    troubleshootUniverse.mutateAsync(payload);
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={'Attach Universe'}
      onSubmit={handleFormSubmit}
      cancelLabel={'Cancel'}
      submitLabel={'Attach'}
      size="md"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
    >
      <Box className={helperClasses.flexColumn}>
        <YBInputField
          name="id"
          label={'Universe UUID'}
          type="text"
          control={control}
          rules={{
            required: 'Universe UUID required'
          }}
          disabled
          className={helperClasses.input}
          placeholder={'3ef2e0d6-ad28-453d-9498-fb197e888f11'}
        />
        <YBInputField
          label={'Customer UUID'}
          name="customerId"
          type="text"
          control={control}
          rules={{
            required: 'Customer UUID required'
          }}
          disabled
          className={helperClasses.input}
          placeholder={'11d78d93-1381-4d1d-8393-ba76f47ba7a6'}
        />
        <YBInputField
          label={'API Token'}
          name="apiToken"
          type="text"
          rules={{
            required: 'API Token required'
          }}
          disabled
          className={helperClasses.input}
          control={control}
        />
        <YBInputField
          label={'Platform Url'}
          name="platformUrl"
          type="text"
          control={control}
          placeholder={'https://portal.dev.yugabyte.com'}
          className={helperClasses.input}
          disabled
          rules={{
            required: 'Platform Url required'
          }}
        />
        <YBInputField
          label={'Metrics Url'}
          name="metricsUrl"
          type="text"
          control={control}
          disabled
          rules={{
            required: 'Metrics Url required'
          }}
          className={helperClasses.input}
          placeholder={'https://portal.dev.yugabyte.com:9090'}
        />
        <YBInputField
          label={'Scrape Interval Period (secs)'}
          name="metricsScrapePeriodSec"
          type="number"
          control={control}
          rules={{
            required: 'Scrape Interval required'
          }}
          className={helperClasses.input}
          placeholder={'30'}
        />
      </Box>
    </YBModal>
  );
};
