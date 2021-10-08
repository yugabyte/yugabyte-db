export const createErrorMessage = (payload) => {
  const structuredError = payload?.response?.data?.error;
  if (structuredError) {
    if (typeof structuredError == 'string') {
      return structuredError;
    }
    const message = Object.keys(structuredError)
      .map((fieldName) => {
        const messages = structuredError[fieldName];
        return fieldName + ': ' + messages.join(', ');
      })
      .join('\n');
    return message;
  }
  return payload.message;
};
