# Plasma Share Uploader

Create runtime-configurable upload targets for the KDE Plasma 6 Share menu. The package ships one generic `Upload To...` action that loads targets from JSON at runtime. [Catbox](https://catbox.moe/) and [Uguu](https://uguu.se/) are included by default.

![Screenshot](docs/screenshot.png)

## License

GPL-3.0-or-later.

## Build

Requires Qt 6, KF6 Purpose, CoreAddons, Notifications, CMake, and Extra CMake Modules.

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
server, plus a Purpose controller test requiring Qt QML. It does not contact external upload services.

## Install

```sh
cmake --install build
```

The plugin installs to Qt's system plugin search path under `kf6/purpose`, using
[`KDEInstallDirs6`](https://api.kde.org/ecm/kde-module/KDEInstallDirs6.html). Installation there usually requires administrator privileges.
Bundled targets install under `${CMAKE_INSTALL_PREFIX}/share/plasma-share-uploader/targets/`.
The default prefix follows KDE's installation (normally `/usr`, also used by the
Arch package); set `CMAKE_INSTALL_PREFIX` at configure time to override it.
For a custom plugin location, set `KDE_INSTALL_QTPLUGINDIR` at configure time and
add that directory to the host application's `QT_PLUGIN_PATH`.
Restart Dolphin/Gwenview/other Purpose-Share-enabled app after installing so the `Upload To...` Share action shows up.

Installed builds use installed presets and icons. To use the checkout as the
preset and icon source during development, configure with
`-DPLASMA_SHARE_UPLOADER_USE_SOURCE_DATA=ON`. This controls initial default links;
existing active entries keep their current destinations.

## Targets

The active target directory is `~/.config/plasma-share-uploader/targets/`
(or `$XDG_CONFIG_HOME/plasma-share-uploader/targets/` when set). Only top-level
`*.json` files in this directory are loaded:

- Symlinks enable packaged presets and receive their updates automatically.
- Regular files are independent custom targets.
- Subdirectories, including `disabled/`, are ignored.

When the directory does not exist, the first share creates it with links to the
bundled Catbox and Uguu presets. An existing directory is used exactly as it is,
even if empty. New presets shipped in later package versions are not automatically
enabled in an existing directory.

Packaged presets live under
`${CMAKE_INSTALL_PREFIX}/share/plasma-share-uploader/targets/` (normally `/usr/share/...`).
Their filenames stay stable across releases so enabled links continue to work.
The `examples/` subdirectory contains templates that need configuration before use.

For example, enable Catbox with:

```sh
target_dir="${XDG_CONFIG_HOME:-$HOME/.config}/plasma-share-uploader/targets"
mkdir -p "$target_dir"
ln -s /usr/share/plasma-share-uploader/targets/catbox.json "$target_dir/catbox.json"
```

Disable a linked preset by removing its link. To preserve a custom configuration
while disabling it, move the file into `targets/disabled/`; move it back to re-enable
it. Keeping the active directory itself preserves an intentionally empty selection.

To customize an enabled preset, replace its symlink with a copy of the packaged
JSON before editing. The copy is independent and will no longer receive packaged
updates. Do not edit through the symlink. Keep only one active definition for each
`id`; duplicate IDs produce a diagnostic, and the first valid file in filename
order is used.

The target directory is read each time you start a new share. Invalid configs and
broken links produce file-specific diagnostics in the picker.

For updating an existing installation, see [the release notes](CHANGELOG.md).

### Target format

Each target file is a single JSON object with these required fields:
- `id`: unique lowercase identifier; `[a-z0-9][a-z0-9_-]*`.
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
- `${ENV:VARNAME}`: expand from the environment once; environment references inside expanded values are treated literally.
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
- Commands run asynchronously so the host application's UI remains responsive.

If you want a catch-all fallback rule, use `*/*` and place it last.

### Response formats

`response` must include a `type`:
- `text_url`: response body is the URL.
- `regex`: use a valid `pattern` and optional `group` (default `1`, or `0` for the whole match) to extract URL from response text. The group must exist in the pattern.
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

If a later file fails, the job reports the failure while preserving completed
uploads in its output and copying their URLs to the clipboard.

### Example target file

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
      "mime": ["image/jpeg", "image/tiff"],
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

Save it as its own file, for example
`~/.config/plasma-share-uploader/targets/example.json`.

If replacing an enabled bundled preset, remove its link before adding your custom
file with the same `id`.
