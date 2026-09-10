# Plasma Share Uploader

Upload files from the Share menu in KDE Plasma 6 applications such as Dolphin
and Gwenview. The plugin adds an **Upload To…** action with configurable upload
targets. [Catbox](https://catbox.moe/) and [Uguu](https://uguu.se/) come configured
for images; add other services through the settings app or a JSON definition.

![Share menu and upload target picker](docs/screenshot.png)

*Composite showing the Share menu and upload picker together.*

## Use

After installing, restart Dolphin or Gwenview, then:

1. Select the files you want to upload.
2. Open **Share → Upload To…** and choose a target.
3. The resulting links are copied to the clipboard when the uploads finish.

The picker shows targets that accept all selected files. Use **Configure…** to
manage targets and **Reload** to pick up changes without starting a new share.

## Configuration app

Open **Plasma Share Uploader Settings** from the application launcher, run
`plasma-share-uploader-config`, or choose **Configure…** in the upload picker.

The app lets you:

- Add, customize, enable, disable, and duplicate targets.
- Edit requests, response extraction, file filters, and preprocessing commands.
- Store credentials in KWallet and reference them from target definitions.
- Import and export individual target JSON files.
- Validate a target, test response parsing, preview preprocessing, or upload a
  test file.

Edits require **Save**. New targets and imports start as disabled drafts; save
and enable them when ready. See the [configuration app guide](docs/configuration-app.md)
for details.

## Build

Requires a C++20 compiler, CMake 3.24+, Extra CMake Modules, Qt 6.8+ (including
Qt Declarative and Quick Controls), and KDE Frameworks 6 Purpose, CoreAddons,
Notifications, Wallet, and Kirigami. Install KDE's QQC2 desktop style for the UI.

The bundled targets also use `exiv2` to strip metadata from temporary copies of
JPEG/TIFF files. Install it or remove those rules in the **Before Upload** tab.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
```

An Arch Linux package recipe and dependency list are in [arch/PKGBUILD](arch/PKGBUILD).

## Install

For a system installation:

```sh
sudo cmake --install build
```

This installs the Share plugin, settings app, desktop launcher, bundled targets,
and JSON Schema. Restart applications using the Share menu after installation.

The default prefix follows KDE's installation, normally `/usr`. Presets install
under `${CMAKE_INSTALL_PREFIX}/share/plasma-share-uploader/targets/`; the plugin
uses Qt's plugin directory under `kf6/purpose`.

For a custom installation, set `CMAKE_INSTALL_PREFIX` when configuring. To change
the plugin location too, set `KDE_INSTALL_QTPLUGINDIR` and add that directory to
the host application's `QT_PLUGIN_PATH`.

**Upgrading from 0.3.0:** install the matching plugin, settings app, and presets
together. Linked presets update with the package; custom target files require
[manual format changes](docs/target-format.md#updating-older-targets). There is no
automatic conversion. See the [changelog](CHANGELOG.md) for release details.

## Targets

The settings app manages files in `~/.config/plasma-share-uploader/targets/`
(or `$XDG_CONFIG_HOME/plasma-share-uploader/targets/`). Only top-level `*.json`
entries are active:

- **Symlinks** enable packaged presets and receive package updates.
- **Regular files** are independent custom definitions.
- **Subdirectories**, including `disabled/`, are ignored by the Share picker.

On first use, a missing target directory is created with links to Catbox and
Uguu. An existing directory is used as-is, even if empty. New packaged presets
are not automatically enabled in an existing configuration.

To customize a linked preset, choose **Customize** in the settings app. Saving
replaces the link with an independent copy. When editing manually, replace the
link with a copy first; editing through the link would change the packaged file.

You can also manage targets directly: remove a preset's link to disable it, or
move a custom file into `targets/disabled/` to keep it for later. Keep one active
definition per `id`. Duplicate IDs, invalid JSON, and broken links produce
file-specific diagnostics; the first valid definition in filename order is used.

The packaged [examples](targets/examples) provide starting points for additional
services. Check their [setup requirements and API verification notes](docs/target-api-verification.md)
before enabling them.

## Target format

Each target is one JSON object with `schemaVersion: 1`, an `id`, a `request`, and
a `response`. For example:

```json
{
  "schemaVersion": 1,
  "id": "example",
  "displayName": "ExampleHost",
  "accept": {"mimeTypes": ["image/png", "image/jpeg"]},
  "request": {
    "url": "https://example.com/upload",
    "method": "POST",
    "body": {"type": "multipart", "fileField": "file"}
  },
  "response": {
    "url": {"type": "json_pointer", "pointer": "/data/url"}
  }
}
```

Replace the example endpoint and fields with those required by your service.
MIME entries are alternatives; a file must also match an extension filter when
one is configured. Extracted URLs must be absolute HTTP(S) URLs.

See the [format reference](docs/target-format.md) for body types, placeholders,
credentials, extractors, and preprocessing. A [JSON Schema](schemas/target-v1.schema.json)
is provided for editor validation. The format version is separate from the
application version.

## Test

Tests require Python 3 and `jsonschema` in addition to the build dependencies:

```sh
python3 -m venv /tmp/plasma-share-test-venv
/tmp/plasma-share-test-venv/bin/pip install -r tests/requirements.txt
cmake -S . -B build-test -DBUILD_TESTING=ON \
  -DPython3_EXECUTABLE=/tmp/plasma-share-test-venv/bin/python
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure
cmake --build build-test --target imshare_ui_qmllint
```

The suite covers the schema and parser, target management, upload behavior,
Qt Quick controls, and the plugin through Purpose. Automated uploads use a local
HTTP server and credentials use an in-memory store. GUI tests run offscreen.
See the [UI validation](docs/qml-migration-validation.md) and
[format validation](docs/target-format-validation.md) reports for coverage and
verification limits.

For development using the checkout's presets, configure with
`-DPLASMA_SHARE_UPLOADER_USE_SOURCE_DATA=ON`. Existing target links retain their
current destinations. The [configuration app guide](docs/configuration-app.md#development-and-verification)
shows how to use a separate target directory for testing.

Planned features are tracked in the [configuration application plan](docs/configuration-app-plan.md).

## License

[GPL-3.0-or-later](LICENSE).
