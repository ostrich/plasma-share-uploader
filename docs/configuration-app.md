# Plasma Share Uploader Settings

Launch **Plasma Share Uploader Settings** from the application menu, run
`plasma-share-uploader-config`, or choose **Configure...** in the Share picker.
The manager edits the same JSON definitions that the Share plugin reads.

## Add and manage targets

The left side lists configured targets. Search by name, hostname, or filename,
and use the filter to browse enabled/disabled targets, packaged presets and
setup templates, or configurations needing attention.

**Add Target...** offers a blank target, packaged definitions, and native JSON
import. New targets, template copies, duplicates, and imports start as unsaved,
disabled drafts. Edit, **Save disabled draft**, then **Enable** when ready.
Enabling checks the definition and active ID/filename conflicts. It does not
check service availability or automatically upload anything.

- **Enable** on an available preset creates an active symlink. Package updates
  continue to update that definition.
- **Customize** on an enabled link unlocks the editor. **Save** atomically replaces
  the link with an independent regular file; the linked source is never modified.
- **Disable** removes an active packaged link or moves a custom target into
  `targets/disabled/`. Other linked targets are retained in that directory too.
- **Duplicate** creates a new ID and disabled copy. Credential references initially
  keep their existing names, so the duplicate shares those credentials.
- **Restore preset...** replaces a custom active file with its same-filename
  packaged preset after confirmation.
- **Delete...** removes the configured entry. Stored credentials are retained.

Only top-level JSON entries in the active directory are enabled. The manager
uses the plugin's existing initialization: a missing directory gets default
preset links; an existing empty directory stays empty. There is no enablement
list, inherited configuration overlay, migration process, or background daemon.

## Edit a definition

The editor exposes all fields in [target format version 1](target-format.md):

| Page | Contents |
| --- | --- |
| General | Name, description, icon, ID, accepted MIME types and extensions |
| Request | Endpoint, POST/PUT, multipart/raw/form URL encoded/JSON bodies, file field, content type, headers, query parameters |
| Credentials | API key, bearer, and basic-auth helpers backed by KWallet |
| Response | Shared URL and optional thumbnail, deletion, and error extractors; text, JSON pointer, regex/group, header, redirect, and XML path |
| Before Upload | First-match MIME rules, ordered commands and arguments, file handling, per-command timeout |
| JSON | Complete definition, including fields not known to the forms |
| Test | Local validation, response fixtures, preprocessing preview, and explicit uploads |

Form edits and JSON edits share one document. Unknown fields and JSON value
types are preserved. Invalid JSON remains repairable in the JSON page; validation
messages identify field paths. Duplicate table names are reported before they
can overwrite another value. Use the JSON page to repair unsupported structures.

The General page edits `accept.mimeTypes` and `accept.extensions`. Entries within
a list are alternatives; both nonempty categories must match. Request settings
use `request.body`; the primary response extractor is `response.url`. A JSON body
uses `body.value`, including scalar and null values. XML extraction uses the
limited `xml_path` syntax. Empty JSON Pointer selects the whole response; `/`
selects an empty object key. New targets include `schemaVersion: 1`.

The status row below the editor stays one line high. **Needs attention** shows
the issue count; **Details...** opens the complete, selectable diagnostics.
External-file warnings and draft notices use the same row. Long target names and
paths are shortened visually, with their full text available on hover, so changing
selection does not move the header, tabs, or action buttons.

**Save** is explicit. Navigation and closing offer Save, Discard, or Cancel for
unsaved edits. Invalid definitions can be saved as disabled drafts, but cannot
replace an active target or be enabled. Incomplete table edits that cannot be
represented in JSON must be repaired first. External file changes require a
reload before saving; an older draft cannot silently overwrite them.

**Open JSON externally** is available for saved custom regular files. Use
**Customize** and Save before opening a linked preset in an external editor.
Changes take effect on the next share. Use **Reload** in an already-open Share
picker to refresh it after configuration.

## Credentials

Managed credentials use the network wallet's `plasma-share-uploader` folder.
The target stores a reference such as:

```json
"headers": {
  "Authorization": "Bearer ${WALLET:work.api}"
}
```

Credential names accept letters, digits, dots, dashes, and underscores. The
Credentials page can store/replace a secret, bind an existing name, remove a
request-field binding, check references, or forget a stored credential. Storage
changes take effect immediately; adding/removing a binding edits the draft and
requires Save. Forgetting a name affects every target using it.

API keys can be placed in a header, query parameter, multipart field, URL encoded
form field, or JSON object field. The body placement must match the request type.
Bearer and basic helpers start with the Authorization header. Basic auth stores
the base64-encoded `username:password` value in KWallet and adds the `Basic ` prefix
in JSON. A reused basic credential must already contain that encoded value.
Anonymous targets need no authentication fields.

References are supported in the endpoint, header/query values, multipart/form
values, and JSON string values. They are not supported in field names, raw file
contents, content-type settings, response extractors, or preprocessing arguments.
`${ENV:NAME}` remains supported in the same request-value locations. Substitution
runs once: placeholder-looking text inside a secret or environment value stays
literal. Prefer the query table to embedding credentials directly in a URL so
query values receive the appropriate encoding.

Missing/empty references and wallet access failures stop an upload. There is no
plaintext fallback. Checking or using wallet credentials may show KWallet's
access/unlock prompt. Environment values are read from the current process:
a variable available to this app may be absent from Dolphin or Gwenview.

Managed secret entry is masked and its value is never written to target JSON.
Existing inline values remain editable as ordinary JSON; move those to references
when configuring credentials.

## Import and export

**Import JSON...** reads one target definition into a disabled draft for review.
Review endpoint, credential references, and preprocessing commands before enabling
or testing it. Incomplete imports can be saved disabled. ID/filename conflicts
produce a new unique draft ID; existing targets are not replaced by import.

**Export...** exports the complete definition, including when the selected target
is a packaged link. It preserves credential references and replaces recognized
inline secret fields with environment references. An editable preview lists the
replaced paths. Review other private values before sharing: arbitrary custom
fields cannot all be identified automatically. Export to a separate regular file.
Supply referenced environment values or wallet entries on the receiving machine.
The app never retrieves wallet values to embed them in an export.

This release supports our single-target JSON format, version 1. Older imports
remain editable as disabled drafts but require the [manual format changes](target-format.md#updating-older-targets)
before enabling; there is no automatic conversion. ShareX `.sxcu` conversion,
bulk transfer, declared service-setup forms, and broader preferences remain in
the [plan](configuration-app-plan.md).

## Test an unsaved draft

The Test page deliberately separates four actions:

1. **Validate** checks schema, environment requirements, wallet reference names,
   and executable availability. It does not unlock KWallet, execute commands, or
   make a network request. Use **Check credentials** to check wallet availability.
2. **Test response parsing** uses pasted or captured response text, status,
   headers, and response URL through the same parser used by uploads. Select a
   string in the JSON tree and **Use selected JSON value** to set a shared URL,
   thumbnail, deletion, or error pointer. Customize a read-only preset first.
3. **Preview preprocessing** runs commands on a temporary copy and reports the
   resulting file, MIME type, and size. **Open prepared file** opens that copy.
   It performs no upload and needs no upload credentials.
4. **Upload test file** uploads the selected file or a generated 32×32 PNG through
   the shared engine, resolving credentials, checking file constraints, running
   preprocessing, and showing progress. This sends data to the configured endpoint.

Preprocessing commands are ordinary local programs, not a sandbox. Review imported
commands before running them. Arguments are passed directly without an implicit
shell. The original sample is retained unchanged, including for output-file rules.
The first matching MIME rule runs; a failed command stops the test.

**Cancel** stops local preprocessing/network work. The remote service may already
have accepted an upload. Cancel before saving, changing targets, or closing the
manager. Tests do not copy upload URLs to the clipboard, send normal upload
notifications, retry, or save history. Results stay in the inspector and diagnostics;
**Copy diagnostics** is explicit. Editing the draft does not retroactively change
a captured response—run the parser or upload again as needed.

Captured responses are capped at 1 MiB, and diagnostic output/tree sizes are
bounded. Known credential/environment values and recognized inline secrets are
redacted in test results; Set-Cookie values are concealed. Pasted fixtures are
user-provided text and should be reviewed before copying diagnostics, especially
for secrets that this process has never resolved.

## Development and verification

Build dependencies include KF6 Wallet. Install the Python test dependencies as
described in the [test setup](../README.md#test). For an isolated manager workspace:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DPLASMA_SHARE_UPLOADER_USE_SOURCE_DATA=ON \
  -DPython3_EXECUTABLE=/tmp/plasma-share-test-venv/bin/python
cmake --build build
mkdir -p /tmp/my-upload-targets
build/src/plasma-share-uploader-config --targets-dir /tmp/my-upload-targets --presets-dir "$PWD/targets"
ctest --test-dir build --output-on-failure
```

Creating that empty test directory deliberately suppresses default activation.
The directory options do not redirect KWallet: credential operations still use
the current desktop's wallet. Automated credential tests inject an in-memory
store and send uploads only to a loopback HTTP server.
