import * as parser from 'cron-parser';
import { setDay } from 'date-fns';

export type CronFields = {
  second: number[];
  minute: number[];
  hour: number[];
  dayOfMonth: number[];
  month: number[];
  dayOfWeek: number[];
};

export const parseCronExpression = (expression: string): parser.CronExpression => {
  return parser.parseExpression(expression);
};

export const convertToCronExpression = (
  fields: parser.CronFields,
  convertToCronInUTC = true
): parser.CronExpression => {
  return parser.fieldsToExpression(convertToCronInUTC ? (convertToUTC(fields) as parser.CronFields) : fields);
};

const convertToUTC = (fields: parser.CronFields): CronFields => {
  let date = new Date();
  // using JSON.parse instead of spread since it throws error
  // that field params are readonly when we try to update
  const fieldsUpdated: CronFields = JSON.parse(JSON.stringify(fields)) as CronFields;
  date.setHours(fieldsUpdated.hour[0]);
  date.setMinutes(fieldsUpdated.minute[0]);
  fieldsUpdated.hour = [date.getUTCHours()];
  fieldsUpdated.minute = [date.getUTCMinutes()];

  fieldsUpdated.dayOfWeek = fieldsUpdated.dayOfWeek.map((day: number) => {
    date = setDay(date, day);
    return date.getUTCDay();
  });
  return fieldsUpdated;
};

export const convertUTCCronToLocal = (fields: parser.CronFields): CronFields => {
  const date = new Date();
  const fieldsUpdated: CronFields = JSON.parse(JSON.stringify(fields)) as CronFields;
  date.setUTCHours(fieldsUpdated.hour[0]);
  date.setUTCMinutes(fieldsUpdated.minute[0]);
  fieldsUpdated.hour = [date.getHours()];
  fieldsUpdated.minute = [date.getMinutes()];
  fieldsUpdated.dayOfWeek = fieldsUpdated.dayOfWeek.map((day: number) => {
    let count = 0;
    do {
      count++;
      date.setUTCDate(count);
    } while (date.getUTCDay() !== day);
    return date.getDay();
  });
  return fieldsUpdated;
};
