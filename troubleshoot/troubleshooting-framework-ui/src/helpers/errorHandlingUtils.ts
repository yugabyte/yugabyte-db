export const assertUnreachableCase = (value: never) => {
  throw new Error(`Encountered unhandled value: ${value}`);
};
