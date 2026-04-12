# Plasma Share Uploader

Create runtime-configurable upload targets for the KDE Plasma 6 Share menu. The package ships one generic `Upload...` action that loads targets from JSON at runtime. [Catbox](https://catbox.moe/) and [Uguu](https://uguu.se/) are included by default.

![Screenshot](docs/screenshot.png)

## License

GPL-3.0-or-later.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Test

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suite includes C++ unit and integration tests that use a local in-process HTTP
server. It does not contact external upload services.

## Install

```sh
cmake --install build
```

Plugins install to the Purpose plugin directory (`${KDE_INSTALL_QTPLUGINDIR}/kf6/purpose`).
The package also installs bundled targets under `/usr/share/plasma-share-uploader/targets/`.
Restart Dolphin/Gwenview/other Purpose-Share-enabled app after installing so the `Upload...` Share action shows up.

## Targets

Targets are loaded at runtime from:
- system defaults: `/usr/share/plasma-share-uploader/targets/*.json`
- user overrides/custom targets: `~/.config/plasma-share-uploader/targets/*.json`

Each file contains exactly one target object. Targets are merged by `id`, and user
targets override system targets with the same `id`.

You do not need to rebuild after editing the user config. Add or edit files in the user
directory, then restart the Share-enabled app or reopen its Share dialog so it reloads
the target list.

Invalid target files are skipped. If a config file contains errors, the picker dialog shows
the paths it checked and the validation errors it encountered.

### Target format

Each target file is a single JSON object with these required fields:
- `id`: lowercase identifier for overrides and merges; `[a-z0-9][a-z0-9_-]*`.
- `displayName`: human-friendly name shown in the picker.
- `description`: short description shown under the target name.
- `icon`: icon name (e.g. `image-x-generic`).
- `request`: upload configuration (see below).
- `response`: how to extract the URL from the server response.

Optional fields:
- `pluginTypes`: accepted for compatibility, but not used by the runtime picker.
- `constraints`: target filters such as `["mimeType:image/*"]`. These are evaluated at runtime against the files being shared.
- `extensions`: optional list of file suffixes such as `["png", ".jpg"]`. If present, every shared file must match one of them.
- `preUpload`: ordered list of per-file preprocessing rules to run before upload.

### Request formats

`request` includes:
- `url`: upload endpoint URL. Supports `${ENV:VAR}` substitution, and `${FILENAME}` in URL paths.
- `method`: HTTP method. `multipart`, `raw`, `form_urlencoded`, and `json` currently support `POST` and `PUT` as documented below.
- `query`: optional query-string parameter map.
- `headers`: optional header map.
- `type` (optional): `multipart` (default), `raw`, `form_urlencoded`, or `json`.

Request string placeholders:
- `${ENV:VARNAME}`: expand from the environment.
- `${FILENAME}`: expand to the local file name.

Multipart uploads:
- `request.type`: `multipart` (or omitted).
- `request.multipart.fileField`: form field name for the file.
- `request.multipart.fields`: optional extra form fields (string values only).
  Field values support `${ENV:VARNAME}` and `${FILENAME}`.

Raw uploads:
- `request.type`: `raw`.
- `request.contentType`: optional Content-Type to set for the file body.

Form URL encoded uploads:
- `request.type`: `form_urlencoded`.
- `request.formUrlencoded.fields`: required string map sent as `application/x-www-form-urlencoded`.
  Field values support `${ENV:VARNAME}` and `${FILENAME}`.

JSON uploads:
- `request.type`: `json`.
- `request.json.fields`: required JSON value written as `application/json`.
  String values inside the JSON body support `${ENV:VARNAME}` and `${FILENAME}`.

Headers and query parameters:
- `request.headers`: string map. Values support `${ENV:VARNAME}` and `${FILENAME}`.
- `request.query`: string map. Values support `${ENV:VARNAME}` and `${FILENAME}`.

### Pre-upload commands

Targets may optionally define `preUpload` rules to transform a file before it is uploaded.
Rules are evaluated once per file, in order. The first matching rule is used. If no
rule matches, the original file is uploaded unchanged.

Each `preUpload` entry must include:
- `mime`: non-empty array of MIME patterns. Supported forms are exact MIME types such as `image/png`, wildcard subtype patterns such as `image/*`, and `*/*` to match any MIME type.
- `fileHandling`: one of:
  - `inplace_copy`: copy the original file to a temporary path, substitute `${FILE}` with that temporary path, run one or more commands in place on the copy, then upload the modified copy.
  - `output_file`: substitute `${FILE}` with the original file path and `${OUT_FILE}` with a temporary output path, run exactly one command, then upload `${OUT_FILE}`.
- `commands`: non-empty array of command objects.
  - `inplace_copy` rules may contain one or more commands.
  - `output_file` rules must contain exactly one command.

Each command object must include:
- `argv`: non-empty array of command arguments. Commands are executed directly without a shell.

Available placeholders in `argv`:
- `${FILE}`: required in every command.
- `${OUT_FILE}`: required for `output_file`, and not allowed for `inplace_copy`.

Behavior:
- First matching `preUpload` rule wins.
- No match: upload the original file.
- Non-zero exit, timeout, or missing output file: fail that upload and surface stderr.
- Original user files are never modified.

If you want a catch-all fallback rule, use `*/*` and place it last.

### Response formats

`response` must include a `type`:
- `text_url`: response body is the URL.
- `regex`: use `pattern` and optional `group` to extract URL from response text.
- `json_pointer`: use `pointer` (must start with `/`) to locate a string URL in a JSON response.
- `header`: use `name` to read a response header.
- `redirect_url`: use the redirect target URL, or the final reply URL if no redirect target is reported.
- `xml_xpath`: use `xpath` (must start with `/`) to locate a text node in an XML response.

Optional error extraction:
- `response.error`: optional extractor object with the same `type` choices as `response`.
- On HTTP error responses, the uploader will try `response.error` before falling back to the raw server response text.

Optional variant outputs:
- `response.thumbnail`: optional extractor object for a thumbnail URL.
- `response.deletion`: optional extractor object for a deletion URL.

`ShareJob` output now includes:
- `url` / `urls`
- `thumbnailUrl` / `thumbnailUrls` when configured
- `deletionUrl` / `deletionUrls` when configured
- `results`: per-upload objects containing `url`, optional `thumbnailUrl`, optional `deletionUrl`, and `response`

Each `response` object contains:
- `statusCode`
- `reasonPhrase`
- `responseUrl`
- `headers` with lowercased header names
- `responseText`

### User override example

```json
{
  "id": "example",
  "displayName": "ExampleHost",
  "description": "Upload images to ExampleHost",
  "icon": "image-x-generic",
  "pluginTypes": ["ShareUrl", "Export"],
  "constraints": ["mimeType:image/*"],
  "request": {
    "url": "https://example.com/upload",
    "method": "POST",
    "multipart": {
      "fields": {
        "token": "${ENV:EXAMPLE_TOKEN}"
      },
      "fileField": "file"
    }
  },
  "preUpload": [
    {
      "mime": ["image/*"],
      "fileHandling": "inplace_copy",
      "commands": [
        {
          "argv": ["exiv2", "rm", "${FILE}"]
        },
        {
          "argv": ["oxipng", "--strip", "all", "${FILE}"]
        }
      ]
    }
  ],
  "response": {
    "type": "json_pointer",
    "pointer": "/data/url"
  }
}
```

To define this as a user target, save it as its own file such as
`~/.config/plasma-share-uploader/targets/example.json`:

```json
{
  "id": "example",
  "displayName": "ExampleHost",
  "description": "Upload images to ExampleHost",
  "icon": "image-x-generic",
  "constraints": ["mimeType:image/*"],
  "request": {
    "url": "https://example.com/upload",
    "method": "POST",
    "multipart": {
      "fields": {
        "token": "${ENV:EXAMPLE_TOKEN}"
      },
      "fileField": "file"
    }
  },
  "preUpload": [
    {
      "mime": ["image/*"],
      "fileHandling": "inplace_copy",
      "commands": [
        {
          "argv": ["exiv2", "rm", "${FILE}"]
        },
        {
          "argv": ["oxipng", "--strip", "all", "${FILE}"]
        }
      ]
    }
  ],
  "response": {
    "type": "json_pointer",
    "pointer": "/data/url"
  }
}
```

If you want to override a bundled target, give your file the same `id` as the system
target. The user definition wins at runtime.
