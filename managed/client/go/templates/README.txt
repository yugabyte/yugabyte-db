This folder contains mustache template files used for generating the Go API clients
(client/go/v1 and client/go/v2). These are slightly modified from the original source
found here:
https://github.com/OpenAPITools/openapi-generator/tree/v7.17.0/modules/openapi-generator/src/main/resources/go.
The changes made to each file is documented in place in the respective files.

The YB vendor extensions consumed by these templates are documented here.

1. x-yba-api-stream-response (at operation level)
This is a boolean extension that makes the generated Execute() method hand the response
body back to the caller unread, instead of reading it into memory. Set it on operations
that return large binary payloads, so callers can io.Copy straight from
httpResponse.Body without the whole payload being buffered in memory and then written to
a temp file by APIClient.decode().

Streaming operations return only (*http.Response, error). The decoded return value is
dropped from the generated signature, since nothing is decoded. The caller reads the
payload from httpResponse.Body and owns closing it:

    httpResponse, err := apiClient.SupportBundleAPI.
        DownloadSupportBundle(ctx, cUUID, uniUUID, sbUUID).Execute()
    if err != nil {
        return err
    }
    defer httpResponse.Body.Close()
    _, err = io.Copy(outFile, httpResponse.Body)

Error responses (status >= 300) are still read in full, so GenericOpenAPIError keeps
reporting the server message.

Where this extension may be set and what the build validates is documented with the
other vendor extensions in src/main/resources/openapi_templates/server/README.txt.

Example:
x-yba-api-stream-response: true
