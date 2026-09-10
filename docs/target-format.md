# Target JSON format, version 1

Each target is one UTF-8 JSON object. This reference and the
[JSON Schema](../schemas/target-v1.schema.json) describe the format read by both
Plasma Share Uploader Settings and the Share plugin. The format version is
independent of the application version.

```json
{
  "schemaVersion": 1,
  "id": "example",
  "displayName": "ExampleHost",
  "description": "Upload PNG and JPEG images",
  "icon": "image-x-generic",
  "accept": {
    "mimeTypes": ["image/png", "image/jpeg"],
    "extensions": ["png", "jpg", "jpeg"]
  },
  "request": {
    "url": "https://example.com/upload",
    "method": "POST",
    "body": {
      "type": "multipart",
      "fileField": "file",
      "fields": {"token": "${ENV:EXAMPLE_TOKEN}"}
    }
  },
  "response": {
    "url": {"type": "json_pointer", "pointer": "/data/url"},
    "error": {"type": "json_pointer", "pointer": "/error/message"}
  }
}
```

The endpoint above is illustrative. Working service definitions and templates
are in [targets](../targets); their upstream references are recorded in
[target API verification](target-api-verification.md).

## Document and metadata

| Field | Requirement and meaning |
| --- | --- |
| `schemaVersion` | Required integer `1`. Missing, mistyped, and unsupported versions are rejected. |
| `id` | Required unique identifier matching `[a-z0-9][a-z0-9_-]*`. |
| `request` | Required request object. |
| `response` | Required response object containing `url`. |
| `displayName` | Optional string. Omitted or empty uses `id`. |
| `description` | Optional string. Defaults to empty. |
| `icon` | Optional icon-name string. Omitted or empty uses `image-x-generic`. |
| `accept` | Optional file acceptance object, described below. |
| `preUpload` | Optional array of preprocessing rules. Omitted or empty runs no commands. |

Optional means the property may be omitted; it does not mean `null` is accepted.
Known fields are checked for the documented JSON type without string/number
coercion. The exception is `request.body.value`, which deliberately accepts any
JSON value, including `null`.

Unknown properties are preserved by the editor and ignored by the engine. They
are extension metadata, not upload instructions. Removed properties listed under
[Updating older targets](#updating-older-targets) are rejected explicitly, so an
old setting cannot silently acquire a different meaning. A future format that
changes upload semantics must use a new schema version.

Enablement remains a property of the active directory, not a JSON flag. Global
preferences and stored credential values do not belong in target definitions.
See [target management](configuration-app.md#add-and-manage-targets).

## Accepted files

`accept` has two optional arrays of strings:

- `mimeTypes`: exact MIME types, `type/*`, or `*/*`. MIME matching is
  case-insensitive and uses file contents, not the filename extension.
- `extensions`: final filename suffixes, such as `png`, `.jpg`, or `JPEG`.
  Matching is case-insensitive; one optional leading dot is ignored. Compound
  suffixes such as `tar.gz`, wildcards, whitespace, and path separators are not
  accepted.

Entries within each array are alternatives (**OR**). When both arrays are
nonempty, a file must match both categories (**AND**). Every selected file must
qualify for the target to be offered. An omitted or empty array adds no
restriction; omitted `accept` and `"accept": {}` accept all file types.

For the example above, PNG and JPEG files are accepted. A PNG renamed to `.txt`
fails the extension check; plain text renamed to `.png` fails the MIME check.
Acceptance is checked on the original files before preprocessing. Preprocessing
can change their contents or type; acceptance is not rechecked on its outputs.

## Request

`request.url`, `request.method`, and `request.body` are required. Supply an
absolute HTTP(S) endpoint URL; placeholders are resolved before the runtime URL
check. `method` must be explicitly uppercase `POST` or `PUT`; multipart supports
only `POST`. There is no implicit method or body type.

`request.headers` and `request.query` are optional string maps. Map keys must be
nonempty; values must be strings, including empty strings if needed. Query names
and values are percent-encoded and added to the endpoint's existing query.

All body settings live in `request.body`:

| `body.type` | Methods | Body settings and behavior |
| --- | --- | --- |
| `multipart` | POST | Required nonempty `fileField`; optional string map `fields`. Uploads the file as a multipart part with its filename. |
| `raw` | POST, PUT | File bytes are the body. Optional `contentType` string; omitted or empty uses `application/octet-stream`. |
| `form_urlencoded` | POST, PUT | Required string map `fields`, including an empty object if desired. Sends `application/x-www-form-urlencoded`. |
| `json` | POST, PUT | Required `value`, which may be an object, array, string, number, boolean, or null. Sends `application/json`. |

Form and JSON bodies do not implicitly include file bytes or base64-encode a
file. They send the configured values. The request still runs once per selected
file and can use its filename. Raw, form, and JSON Content-Type settings take precedence over a conflicting
Content-Type in the headers map. For multipart, leave Content-Type unset so Qt
can generate it with the correct boundary.

Examples of `body` alone:

```json
{"type": "raw", "contentType": "image/png"}
```

```json
{"type": "form_urlencoded", "fields": {"name": "${FILENAME}"}}
```

```json
{"type": "json", "value": {"name": "${FILENAME}", "public": true}}
```

The editor can retain inactive body settings when switching types. Their known
field types must remain valid, but only the selected type's settings are used.
For example, a retained `value` is not sent by a multipart upload.

The transfer timeout is currently fixed at 30 seconds. With `redirect_url` as the
main extractor, redirects are handled manually so the target location can be
returned without following it. Other main extractors use Qt networking's default
redirect policy.

### Request placeholders and credentials

| Token | Meaning |
| --- | --- |
| `${FILENAME}` | Current upload file's basename; percent-encoded when inserted into the endpoint URL. |
| `${ENV:NAME}` | Environment value; names follow `[A-Za-z_][A-Za-z0-9_]*`. Missing/empty values stop uploads from the manager or Share plugin. |
| `${WALLET:name}` | Managed KWallet credential; names contain letters, digits, dots, dashes, or underscores. Missing/empty values or wallet failures stop uploads. |

These tokens apply to the endpoint, header/query values, multipart/form field
values, and recursively to JSON string values. Object/map keys, content-type
settings, raw file contents, extractors, and preprocessing arguments are not
request substitution locations. Substitution runs once: placeholder-looking text
inside an expanded secret stays literal. Prefer the query map to embedding
credentials in a URL so values receive correct query encoding.

No general expression language, file-content token, or base64 token is supported.
Unsupported `${UPPERCASE}` tokens in JSON body values are validation errors.
Use the documented tokens; unrecognized text elsewhere is not an instruction.
Preprocessing has its own `${FILE}` and `${OUT_FILE}` tokens.

Targets contain credential references, not managed secret values. See the
[credential guide](configuration-app.md#credentials) for binding and export.

## Response extraction

`response.url` is the required shared URL extractor. `response.thumbnail`,
`response.deletion`, and `response.error` are optional extractor objects. Omit an
optional extractor to disable it; an empty object or `null` is invalid.

Each extractor requires an explicit `type`:

| Type | Fields and behavior |
| --- | --- |
| `text_url` | Uses the response body as text. In `error`, it means an error message. |
| `json_pointer` | Required `pointer`; selects a string in a JSON response. |
| `regex` | Required nonempty `pattern`, using Qt's regular expression syntax. Optional integer `group`, default `1`; `0` selects the entire match. The pattern must compile and the group must exist. Uses the first match. |
| `header` | Required nonempty `name`; header names are case-insensitive. |
| `redirect_url` | Uses a redirect target if provided, otherwise the reply URL. A relative redirect target is resolved against the reply URL. |
| `xml_path` | Required `path`; selects the text content of an XML element using the limited path syntax below. |

Like body settings, inactive extractor settings can be retained. Supplied known
fields must have the correct type; pointer/path/regex syntax is checked for the
selected extractor type. `group` is always a nonnegative integer up to 2147483647.

### JSON Pointer

Pointers use [RFC 6901](https://www.rfc-editor.org/rfc/rfc6901), with no URI-fragment
(`#...`) shorthand. Object keys are case-sensitive. `~0` represents `~`, and `~1`
represents `/`; other `~` escapes are invalid.

| Pointer | Selects |
| --- | --- |
| `""` | Entire JSON value, useful when the response is a quoted URL string. |
| `"/"` | Object member whose key is the empty string. |
| `"/files/0/url"` | `url` in the first array entry under `files`. |
| `"/a~1b/m~0n"` | Key `m~n` inside key `a/b`. |

Array indexes are zero-based unsigned decimal integers with no leading zeroes
(except `0`). `01`, `+1`, `-1`, and `-` cannot select array elements, although they
can be literal object keys. Missing values and non-string values produce no
extracted string; objects and numbers are not converted to URLs.

### XML element paths

`xml_path` is deliberately a small element-path feature, not an XPath engine.
A path starts at the document root, for example `/files/file[2]/url`. Each segment
names an element; a child segment may have a positive, one-based index `[n]`.
Without an index, the first matching child is selected. The root segment has no
index. The selected element's text, including descendant text, is returned.

Names use the ASCII subset `[A-Za-z_][A-Za-z0-9_.:-]*`. Namespace prefixes are
matched literally; there is no namespace mapping. `//`, wildcards, attributes,
functions such as `text()`, and arbitrary predicates are rejected.

### URL validation and failures

Extracted URLs are trimmed and must be valid absolute `http` or `https` URLs
with a hostname. Relative paths, bare IDs, other schemes, malformed escapes, and
unescaped spaces are rejected. Text/header/JSON/XML outputs are not resolved
against the endpoint. The explicit exception is `redirect_url`'s HTTP redirect
resolution. URL composition remains a future feature.

The main URL must be present. Missing or empty thumbnail/deletion values are
omitted. A nonempty invalid thumbnail or deletion URL fails response validation
and clears the URL outputs. No returned URL is fetched to check availability;
a syntactically valid URL may still point to a server-side 404.

HTTP errors (`statusCode >= 400`) use `response.error` when it extracts text,
then fall back to the response body or an empty-response error. Error text is not URL-checked.
Network errors fail the upload. On a successful HTTP response with a missing or
invalid main URL, a configured error message is preferred over the URL validation
message. A malformed JSON body for a main JSON extractor gets a JSON parse error.

The Test page uses the same extraction and validation rules for pasted responses
and actual uploads.

## Pre-upload commands

`preUpload` is an ordered array. For each file, the first rule whose `mime`
patterns match its contents runs; patterns within a rule are alternatives. No
matching rule means the original file is uploaded unchanged.

Each rule requires a nonempty `mime` array (same MIME syntax as `accept`),
`fileHandling`, and a nonempty `commands` array:

| `fileHandling` | Behavior |
| --- | --- |
| `inplace_copy` | Copy the original to a temporary file, then run one or more commands on the copy. Upload the resulting copy. |
| `output_file` | Run exactly one command with the original as input and a temporary output path. Upload the output. |

Each command is an object containing a nonempty `argv` array of nonempty strings.
The first argument is the executable. Commands run directly without an implicit
shell. Every command must contain `${FILE}`. In `inplace_copy`, it names the
temporary copy; `${OUT_FILE}` is forbidden. In `output_file`, `${FILE}` names the
input and `${OUT_FILE}` names the output; both are required. Other
`${UPPERCASE}` command placeholders are rejected.

Optional `timeoutMs` is an integer from 1 through 2147483647, defaulting to 30000,
and applies separately to each command. Nonzero exit, timeout, or missing output
fails the upload. The engine preserves the original file; commands are ordinary
programs running with the user's permissions. Commands execute asynchronously.

For example, this rule strips metadata and optimizes **PNG files only**:

```json
{
  "mime": ["image/png"],
  "fileHandling": "inplace_copy",
  "commands": [{"argv": ["oxipng", "--strip", "all", "${FILE}"]}]
}
```

Put a `*/*` fallback last if one is needed. Installing a target does not install
its command dependencies; the Test page checks executable availability.

## Share results

The Share job reports `url`/`urls`, and `thumbnailUrl`/`thumbnailUrls` and
`deletionUrl`/`deletionUrls` when present. `results` contains per-upload objects
with these URLs and a `response` object. That response records `statusCode`,
`reasonPhrase`, `responseUrl`, lowercased `headers`, and `responseText`.

If a later file fails, completed uploads remain in the job output and their URLs
are copied to the clipboard. This result structure is separate from the target's
`response` extractor configuration.

## Schema validation

The published schema uses JSON Schema draft 2020-12 and is installed at
`${CMAKE_INSTALL_PREFIX}/share/plasma-share-uploader/schemas/target-v1.schema.json`.
It can be associated with target files in editors. The optional `$schema` property
in a target can name that file or its published URL; the application does not
fetch schemas at runtime. `schemaVersion` remains required regardless.

The schema checks structure, known-field types, required settings, and supported
pointer/XML syntax. The C++ validator additionally checks regular-expression
compilation/capture groups, credential references, and supported command/JSON
placeholders. The Test page adds environment and executable checks; runtime adds
resolved endpoint and response URL validation. Schema success alone does not
prove a service is reachable or credentials are accepted.

CTest validates the same valid/invalid fixture corpus and all bundled targets
against both the schema and the C++ parser. Cases requiring semantic C++ checks
are marked explicitly in the corpus. See [test setup](../README.md#test).

## Updating older targets

This is an intentional pre-1.0 format change. There is no automatic migration,
legacy reader, or missing-version fallback. Back up custom files and update them
when installing the matching plugin and manager:

| Old field | Version 1 |
| --- | --- |
| No format version | Add `"schemaVersion": 1`. |
| `constraints: ["mimeType:image/*"]` | `accept.mimeTypes: ["image/*"]`; remove the old property. |
| Top-level `extensions` | Move into `accept.extensions`. |
| `pluginTypes` | Remove; it had no runtime effect. |
| `request.type` | Move into `request.body.type`; specify `multipart` if formerly omitted. |
| `request.multipart.fileField` / `.fields` | `request.body.fileField` / `.fields`. |
| `request.formUrlencoded.fields` | `request.body.fields`. |
| `request.json.fields` | `request.body.value`. |
| `request.contentType` | `request.body.contentType`. |
| Primary extractor fields directly under `response` | Move that extractor into `response.url`; keep optional extractors beside it. |
| Extractor `type: "xml_xpath"`, `xpath` | Use `type: "xml_path"`, `path`, within the documented subset. |

Remove old request containers after moving their settings. Check that each
`method` is explicit, multiple MIME patterns are intended as alternatives, `/`
JSON Pointers really mean an empty key, and extracted URLs are absolute HTTP(S).
Use Validate and Test response parsing before enabling a changed custom target.

Linked packaged presets receive the new format with the matching package upgrade.
Independent customized copies need the edits above. Keep a staged test build's
converted targets in a separate directory while the installed plugin still uses
the older format.
