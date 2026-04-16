export interface DateRange {
  startDate?: Date;
  endDate?: Date;
}

export type Setter<T> = React.Dispatch<React.SetStateAction<T>> | ((value: T) => void);

export enum NavigationAction {
  Previous = -1,
  Next = 1
}

export type DefinedRange = {
  startDate: Date;
  endDate: Date;
  label: string;
};
