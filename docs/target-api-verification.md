# Target API verification

Checked on 2026-09-09 against service documentation and upstream implementations.
This checks endpoints, request methods, multipart fields, authentication, and
response extraction. It does not establish that a public service is available or
that an upload will pass its current size, file-type, account, or retention rules.
No files were uploaded to public services during this check.

| Target | Result and setup |
| --- | --- |
| Catbox | Matches the anonymous API: POST `/user/api.php`, `reqtype=fileupload`, file field `fileToUpload`, plain URL response. `userhash` is optional. |
| Uguu | Matches POST `/upload`, file field `files[]`, default JSON response, and `/files/0/url`. Added `/description` to extract readable server errors. |
| Chevereto | Matches API V1: POST `/api/1/upload`, file field `source`, `key` and `format=json` fields, `/image/url_viewer`, `/image/thumb/url`, and `/error/message`. Replace `example.invalid` and provide `CHEVERETO_API_KEY` in the environment of the application invoking Share. The form `key` is a documented alternative to `X-API-Key`. |
| Pomf | Matches upstream POST `/upload.php`, file field `files[]`, default JSON, and `/files/0/url`. Corrected the error pointer from `/error` to `/description`. Replace `example.invalid` with your host. This targets the upstream version linked below; forks can differ, including returning relative filenames instead of absolute URLs. |
| transfer.sh | Matches upstream multipart POST `/` and a plain URL response. The server accepts arbitrary file field names, including `file`; the documentation uses `filedata` in its multipart example. The maintainer recommends running your own instance. Configure the endpoint and any authentication required by that instance; the hostname in the example is not an availability guarantee. |
| vgy.me | **Not verified against current service documentation.** `https://vgy.me/api` returned HTTP 403 from both the browsing tool and a direct HTTP request. The example was left unchanged: its authentication requirements, accepted extensions (including WebP), and response/error fields remain unconfirmed. |

The image MIME and extension filters are local selection rules, not an exhaustive
statement of a service's supported formats. Server limits still apply. Catbox and
Uguu also run `exiv2` on temporary copies of JPEG/TIFF files; that is local
preprocessing, not a requirement of either API.

Validation after the corrections: all six files parse as JSON, the registry and
uploader test suites pass, and a temporary loopback probe using the actual Pomf
and Uguu configs passes both successful uploads and HTTP 400 error responses in
the upstream formats. These fixtures verify local request construction and
response handling; they do not substitute for live service tests.

## Primary references

- Catbox: [API documentation](https://catbox.moe/tools.php),
  [official ShareX config](https://catbox.moe/resources/catbox.moe.sxcu),
  [official upload client](https://catbox.moe/resources/uploadform.js),
  [service restrictions](https://catbox.moe/faq.php).
- Uguu: [API documentation](https://uguu.se/api),
  [response implementation](https://github.com/nokonoko/Uguu/blob/d234f1c5756b6ea6c18adceff44a7cc02d9df992/src/Classes/Response.php),
  [file URL construction](https://github.com/nokonoko/Uguu/blob/d234f1c5756b6ea6c18adceff44a7cc02d9df992/src/Classes/Upload.php).
- Chevereto: [API V1 upload documentation](https://v4-docs.chevereto.com/api/1/file-upload.html),
  [authorization](https://v4-docs.chevereto.com/api/1/authorization.html),
  [API V1 error handling in Chevereto Free](https://github.com/chevereto/chevereto-free/blob/master/app/routes/route.api.php).
- Pomf: [upstream maintenance notice](https://github.com/pomf/pomf/blob/b699df7971f84be4b3e8c09aa8943ab7f065ff13/README.md),
  [upload endpoint](https://github.com/pomf/pomf/blob/b699df7971f84be4b3e8c09aa8943ab7f065ff13/static/php/upload.php),
  [response implementation](https://github.com/pomf/pomf/blob/b699df7971f84be4b3e8c09aa8943ab7f065ff13/static/php/includes/Core.namespace.php),
  [file URL construction](https://github.com/pomf/pomf/blob/b699df7971f84be4b3e8c09aa8943ab7f065ff13/static/php/includes/Upload.class.php).
- transfer.sh: [usage and hosting guidance](https://github.com/dutchcoders/transfer.sh/blob/c37bfd95797fd6da8a6da53fc13d191994b3f687/README.md),
  [multipart example](https://github.com/dutchcoders/transfer.sh/blob/c37bfd95797fd6da8a6da53fc13d191994b3f687/examples.md#uploading-multiple-files-at-once),
  [POST handler](https://github.com/dutchcoders/transfer.sh/blob/c37bfd95797fd6da8a6da53fc13d191994b3f687/server/handlers.go#L445).
- vgy.me: [API documentation URL](https://vgy.me/api) (inaccessible during this check).
