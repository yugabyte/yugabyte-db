export type DeepPartial<T> = T extends object ? { [P in keyof T]?: DeepPartial<T[P]> } : T;

/**
 * Returns type T with fields K marked as optional.
 */
export type Optional<T, K extends keyof T> = Omit<T, K> & Pick<Partial<T>, K>;
export type ValidationErrors = Record<string, { message?: string } | undefined>;

export interface ControllerRenderProps<T> {
  value: T;
  onChange(value: T): void;
  onBlur(): void;
}
