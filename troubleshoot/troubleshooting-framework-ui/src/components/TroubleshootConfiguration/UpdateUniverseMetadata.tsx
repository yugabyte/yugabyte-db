import { Box, makeStyles, Theme } from '@material-ui/core';
import { MetadataFields, UpdateMetadataFormFields } from '../../helpers/dtos';
import { YBModal, YBInputField } from '@yugabytedb/ui-components';
import { useMutation } from 'react-query';
import { TroubleshootAPI } from '../../api';
import { useForm } from 'react-hook-form';
import { toast } from 'react-toastify';

interface UpdateUniverseMetadataProps {
  data: MetadataFields;
  open: boolean;
  onClose: () => void;
  onActionPerformed: () => void;
}

const useStyles = makeStyles((theme: Theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  },
  input: {
    marginTop: theme.spacing(2)
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  confirmationText: {
    fontSize: '14px',
    fontWeight: 400,
    fontFamily: 'Inter'
  }
}));

export const UpdateUniverseMetadata = ({
  open,
  data,
  onClose,
  onActionPerformed
}: UpdateUniverseMetadataProps) => {
  const helperClasses = useStyles();
  const universeUuid = data.id;

  const formMethods = useForm<UpdateMetadataFormFields>({
    defaultValues: {
      apiToken: '',
      metricsScrapePeriodSec: data.metricsScrapePeriodSec
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const {
    formState: { isDirty },
    control,
    handleSubmit
  } = formMethods;

  // PUT API call to edit/add release tags to existing release
  const updateReleaseMetadata = useMutation(
    (payload: any) => TroubleshootAPI.updateUniverseMetadata(universeUuid, payload),
    {
      onSuccess: (response: any) => {
        toast.success('Updated universe metadata successfully for further troubleshooting');
        onActionPerformed();
        onClose();
      },
      onError: () => {
        toast.error('Unable to update metadata, please try again');
      }
    }
  );

  const handleFormSubmit = handleSubmit((formValues) => {
    const payload: any = {};
    Object.assign(payload, data);
    payload.apiToken = formValues.apiToken;
    payload.metricsScrapePeriodSec = formValues.metricsScrapePeriodSec;
    // updateReleaseMetadata.mutateAsync(payload);
    updateReleaseMetadata.mutate(payload);
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={'Update Universe Metadata'}
      onSubmit={handleFormSubmit}
      cancelLabel={'Cancel'}
      submitLabel={'Update'}
      size="sm"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      buttonProps={{
        primary: {
          disabled: !isDirty
        }
      }}
    >
      <Box className={helperClasses.flexColumn}>
        <Box
          className={helperClasses.confirmationText}
        >{`Are you sure you want to update metadata of troubleshooting the following universe: ${data.id} ?`}</Box>
        <YBInputField
          label={'API Token'}
          name="apiToken"
          type="text"
          rules={{
            required: 'API Token required'
          }}
          className={helperClasses.input}
          control={control}
        />

        <YBInputField
          label={'Scrape Interval Period (secs)'}
          name="metricsScrapePeriodSec"
          type="text"
          rules={{
            required: 'Scrape Interval period required'
          }}
          className={helperClasses.input}
          control={control}
        />
      </Box>
    </YBModal>
  );
};
