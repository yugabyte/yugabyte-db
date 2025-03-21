import { useEffect, useRef, useState } from 'react';
import moment from 'moment-timezone';
import { makeStyles } from '@material-ui/core';
import Paper from '@material-ui/core/Paper';
import { YBAutoComplete } from '../../../redesign/components';
import { DEFAULT_TIMEZONE } from '../../../redesign/helpers/constants';
import { isEmptyString } from '../../../utils/ObjectUtils';
import { useSelector } from 'react-redux';

interface GraphPanelHeaderTimezoneProps {
  selectedTimezone: string;
  handleTZChange: (tz: string | null) => void;
}

const useStyles = makeStyles((theme) => ({
  timezoneSelect: {
    width: '135px',
    '& .MuiInput-root.Mui-focused': {
      border: '1px solid #DEDEE0',
      boxShadow: 'none'
    }
  },
  paperMenu: {
    width: '300px',
    left: 'auto !important', // Position the dropdown to the left
    right: '0 !important' // Override the default right position
  }
}));

interface Timezone {
  label: string;
  value: string;
}

export const GraphPanelHeaderTimezone = ({
  selectedTimezone,
  handleTZChange
}: GraphPanelHeaderTimezoneProps) => {
  const classes = useStyles();

  // State variables
  const [inputValue, setInputValue] = useState<string>('');
  const [isTyping, setIsTyping] = useState(false);

  // Refs
  const prevInputValueRef = useRef('');
  const autoCompleteRef = useRef(null);

  const { currentUser } = useSelector((state: any) => state.customer);

  const formatTimezoneLabel = (tz: string) => {
    const formattedTimezone = tz?.replace('_', ' ');
    return `(UTC${moment.tz(tz).format('ZZ')}) ${formattedTimezone} Time`;
  };

  // If metricsTimezone is not set in sessionStorage, set it to the currentUser's timezone
  // If currentUser's timezone is set to default, then set it to the default timezone
  useEffect(() => {
    if (
      isEmptyString(sessionStorage.getItem('metricsTimezone')) ||
      !sessionStorage.getItem('metricsTimezone')
    ) {
      setInputValue(
        isEmptyString(currentUser.data.timezone)
          ? DEFAULT_TIMEZONE.value
          : currentUser.data.timezone
      );
      handleTZChange(
        isEmptyString(currentUser.data.timezone)
          ? DEFAULT_TIMEZONE.value
          : currentUser.data.timezone
      );
    }
  }, []);

  useEffect(() => {
    if (selectedTimezone) {
      selectedTimezone === DEFAULT_TIMEZONE.value
        ? setInputValue(DEFAULT_TIMEZONE.label)
        : setInputValue(`UTC${moment.tz(selectedTimezone).format('ZZ')}`);
    }
  }, [selectedTimezone, inputValue]);

  // Transform the array of strings into an array of objects and include the default option
  const timezoneOptions = [
    DEFAULT_TIMEZONE,
    ...moment.tz.names().map((tz) => ({
      label: formatTimezoneLabel(tz),
      value: tz
    }))
  ];

  const getOptionLabel = (timezone: Record<string, string>): string => {
    const option = (timezone as unknown) as Timezone;
    return DEFAULT_TIMEZONE.value === option.value
      ? DEFAULT_TIMEZONE.label
      : formatTimezoneLabel(option.value);
  };

  const renderOption = (option: Record<string, string>) => {
    return <>{getOptionLabel(option)}</>;
  };

  const selectedOption =
    timezoneOptions.find((option) => option.value === selectedTimezone) ?? null;
  // const filterOptions = (options: Record<string, string>[]) => options;
  const filterOptions = (
    options: Record<string, string>[],
    { inputValue }: { inputValue: string }
  ) => {
    if (!isTyping) {
      return options;
    }
    return options.filter((option) =>
      getOptionLabel(option).toLowerCase().includes(inputValue.toLowerCase())
    );
  };

  const handleBlur = () => {
    if (!inputValue) {
      const firstOption = timezoneOptions[0];
      if (firstOption) {
        setInputValue(DEFAULT_TIMEZONE.value);
        handleTZChange(DEFAULT_TIMEZONE.value);
      }
    }
  };

  return (
    <>
      <YBAutoComplete
        className={classes.timezoneSelect}
        value={selectedOption}
        options={(timezoneOptions as unknown) as Record<string, string>[]}
        getOptionLabel={getOptionLabel}
        renderOption={renderOption}
        filterOptions={filterOptions}
        ybInputProps={{
          'data-testid': 'GraphHeaderHeaderTimezone-Select'
        }}
        inputValue={inputValue}
        onInputChange={(event, newInputValue) => {
          setIsTyping(newInputValue !== prevInputValueRef.current);
          setInputValue(newInputValue);
          prevInputValueRef.current = newInputValue;
        }}
        PaperComponent={(props) => <Paper {...props} className={classes.paperMenu} />}
        ref={autoCompleteRef}
        onBlur={handleBlur}
        onChange={(e, newValue: any) => {
          if (newValue) {
            const changedTimezone = newValue.value;
            handleTZChange(changedTimezone);
            changedTimezone === DEFAULT_TIMEZONE.value
              ? setInputValue(DEFAULT_TIMEZONE.value)
              : setInputValue(`UTC${moment.tz(changedTimezone as string).format('ZZ')}`);
          } else {
            handleTZChange('');
            setInputValue('');
          }
        }}
      />
    </>
  );
};
