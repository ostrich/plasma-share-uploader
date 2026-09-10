# Changelog

## 1.0.0

Replaced the configuration window and Share picker with Qt Quick/QML interfaces,
keeping the target-management and upload workflows. Editing, credentials, asynchronous
confirmation flows, and test inspection use C++ controllers and models. The
resizable sidebar and permanent diagnostic strip remain, and compact forms scroll
without overlapping the Save controls. QML is embedded in the installed binaries.
Qt Declarative, Kirigami, and QQC2 desktop style are additional dependencies.

Added the **Plasma Share Uploader Settings** application and a Configure action in
the Share picker, including when no compatible targets are available. The app
manages the existing active-directory model, edits the complete current target
format, imports/exports single target JSONs, and tests unsaved drafts with local
validation, response fixtures, preprocessing previews, and explicit uploads.

Managed credentials use KWallet and `${WALLET:name}` references. The plugin and
manager share credential resolution and the upload engine; missing environment or
wallet values stop uploads. KF6 Wallet is now a build/runtime dependency.
Definitions using wallet references require this updated plugin.

Introduced target format **schemaVersion 1** and a published JSON Schema. MIME
alternatives and extension filters now live in `accept`; request bodies are
normalized under `request.body`, with JSON payloads in `body.value`. The main
extractor moves to `response.url`. Unused `pluginTypes` is removed, known field
types are validated, JSON Pointer follows RFC 6901 (including root and empty-key
pointers), and the limited XML extractor is named `xml_path`. Extracted URLs must
be absolute HTTP(S); relative redirect locations remain resolved against the
reply URL.

**Custom targets require manual updates.** Install the matching manager, plugin,
and packaged definitions together. Linked presets update with the package;
independent custom files must follow the [format update guide](docs/target-format.md#updating-older-targets).
Missing or unsupported schema versions are rejected. There is no permanent
migration code or legacy compatibility reader.

See the [configuration app guide](docs/configuration-app.md).

## 0.3.0

### Target API corrections

Pomf and Uguu now extract server error messages from `/description`. The target
configs were checked against service documentation and upstream implementations;
see [target API verification](docs/target-api-verification.md) for sources and
setup requirements. Current vgy.me compatibility remains unverified because its
documentation was inaccessible.

### Active target directory

Enabled upload targets now come exclusively from
`~/.config/plasma-share-uploader/targets/` (respecting `XDG_CONFIG_HOME`). Bundled
presets are enabled using symlinks into the installed preset directory, so package
updates still reach enabled presets. Regular JSON files remain independent custom
targets. Removing a link disables a preset; moving a custom file into `disabled/`
preserves it while disabling it.

The `state.json` / `disabledBundledTargets` mechanism and automatic user-over-system
overrides have been removed. There is no automatic migration or compatibility
reader. If you used the earlier configuration format:

1. Keep your existing custom target files.
2. For each bundled target you want enabled, add a symlink to its installed JSON
   unless you already have a valid custom definition with the same ID. Use the old
   disable list, if present, to preserve your selection.
3. Remove the obsolete `state.json` after recording your selection in the active
   directory.

An existing empty directory stays empty. Only a missing active directory receives
default links on first use. New presets added by later package updates are not
silently enabled for existing configurations.
