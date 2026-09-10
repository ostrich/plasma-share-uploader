# Target format v1 validation

Verified on 2026-09-10 on Arch Linux with Qt 6.11.2. This covers the format
cleanup described in [the reference](target-format.md), on top of the QML
migration work. The application version remains 0.3.0 in this development build;
`schemaVersion: 1` is a separate format version. No release or tag was created.

## Checks completed

- Debug and Release: all **11 CTest suites pass**.
- The schema test checks **108 definitions** (102 shared valid/invalid cases and
  all six packaged targets) against JSON Schema draft 2020-12 and the compiled
  C++ validator. Semantic checks that JSON Schema cannot express are marked in
  the corpus. The installed schema matches the source schema byte for byte.
- Upload tests cover all four request body types, scalar/null JSON payloads,
  headers, query encoding, credentials, preprocessing, structured errors, and
  optional URL outputs through a loopback HTTP server. Added response fixtures
  check valid and invalid URLs across text, JSON, regex, header, and XML
  extractors; relative HTTP redirects retain their existing resolution behavior.
- Acceptance tests prove PNG/JPEG alternatives work, MIME and suffix checks both
  apply, content detection rejects misleading filenames, case normalization
  works, and every selected file must qualify.
- JSON Pointer tests distinguish the root from an empty object key, handle
  escaped keys, and reject malformed escapes and noncanonical array indexes.
  XML tests verify indexed element paths, descendant text, and unsupported syntax.
- Manager tests cover the new request/response paths, credentials, root-pointer
  selection, inactive body settings, and retained unknown fields. The Qt Quick
  suite reports **21 passing cases**, including editing and saving MIME and
  extension lists through the actual controls.
- `imshare_ui_qmllint` passes. All seven tabs were rendered at 1180×860 and
  960×720 with Breeze Light and Breeze Dark. Expanded Response forms were also
  rendered with JSON, regex, XML, and header extractors configured; the inspected
  views retain their spacing and scroll at compact sizes.
- README/reference JSON examples parse; their complete target examples pass the
  application validator. `git diff --check` passes.

These checks perform no public-service uploads and use an in-memory credential
store. They do not establish public service availability, real KWallet access,
or host-application behavior with the system-installed old plugin. The service
verification boundaries in [target API verification](target-api-verification.md)
still apply.

## Local test build

The Release build is installed under `/tmp/plasma-share-qml-install`, including
its manager, plugin, packaged targets, icons, and schema. The staged manager
started successfully with the converted target directory and no QML startup
errors:

```sh
/tmp/plasma-share-qml-install/bin/plasma-share-uploader-config \
  --targets-dir /tmp/plasma-share-schema-v1-targets-98zc_ow7
```

The five original files in `~/.config/plasma-share-uploader/targets` were left
unchanged and checked against their original content hashes. The test directory
has links to the new staged Catbox/Uguu presets and converted copies of the local
diagnostic fixtures; intentional invalid fixtures still report errors. This
avoids mixing new definitions with the installed 0.3.0 plugin.

Original active entries were backed up, retaining symlinks, at
`/tmp/plasma-share-before-schema-v1-x_7fjn8a/active-targets`. The same parent
contains a source snapshot from before the format changes. There is no automatic
migration code in the application.

Build/test logs are `/tmp/plasma-share-schema-{tests,release-tests,qmllint}.log`.
Render artifacts are under `/tmp/plasma-share-schema-screenshots-*`; startup
output is `/tmp/plasma-share-schema-startup.log`. These are temporary local
artifacts, not installed application data.

The schema test's Python dependencies were installed into an isolated environment
at `/tmp/plasma-share-schema-venv`; both build trees select its interpreter with
`Python3_EXECUTABLE`. See [test setup](../README.md#test) to reproduce this on
another machine.
