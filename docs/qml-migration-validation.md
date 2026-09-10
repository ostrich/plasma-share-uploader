# QML migration validation

Implementation of [the migration plan](qml-migration-plan.md), starting from
`48f59dd` (`Document QML migration plan`). Verified on Arch Linux with Qt 6.11.2
and Plasma 6.7.4 on 2026-09-10.

## Implementation

Both application-owned interfaces now use the embedded `ShareUploader` QML module:
the configuration manager and the in-process Purpose upload picker. The manager
keeps its searchable sidebar, target actions, seven tabs, bottom actions, and
permanent diagnostic strip. Forms scroll inside the available space. Long names
and paths are elided, with tooltips; error icons keep their color when selected.

C++ controllers own the draft, target operations, credentials, test results, and
picker lifecycle. Qt item models back editable collections. Raw JSON remains
authoritative, including unknown fields and JSON types; incomplete structured
edits remain available for repair and block operations that would lose them.
Save/Discard/Cancel uses asynchronous transitions, including closing the manager.

The existing parser, validator, filesystem store, credential store, preprocessing,
and upload code remain the backend. Superseded Widgets views were removed.
`QApplication` and Qt Widgets remain for native KDE integration. The standalone
manager's desktop-style default is an executable-only resource, so loading the
picker does not change its host's style, graphics backend, or application identity.

Each picker owns its engine and window through its Purpose job. It is a modal
dialog with a transient parent, including the temporary focus gap after a Share
menu closes. Reload retains that parent. Cancellation completes once; destroying
the job cleans up the picker. QML and picker scene-graph failures become job errors.

No target format, storage layout, wallet-key, configuration migration, executable
name, plugin identity, version, tag, or release change is included. Existing
packaging version edits and generated package artifacts were preserved.

## Automated checks

Debug and Release builds and all ten CTest suites pass. The module's generated
`imshare_ui_qmllint` target passes without warnings.
The final Qt Quick run reports 20 passed, zero failed/skipped (including setup and
cleanup), in 8.786 seconds.

| Area | Coverage |
| --- | --- |
| Existing backend | Parsing, validation, registry, input staging, preprocessing, credential resolution/redaction, loopback uploads, limits, cancellation, original-file preservation |
| Target lifecycle | Creation, presets, customization, duplication, enable/disable, restore, deletion, reload, guarded transitions, import/export, external changes, revision conflicts, linked-preset protection |
| Editing | Every tab renders; unknown fields/types survive; malformed JSON repair; duplicate/incomplete map rows; incomplete JSON bodies; editing the second rule and command without losing selection |
| Credentials | Binding placements, validation, delayed callbacks, revision guards, transient-input clearing, incomplete-edit protection; automated tests use a fake store |
| Qt Quick interactions | Actual editing, focus/tab changes, keyboard target selection, Save/Discard/Cancel, clean/dirty window close, export review and file-dialog acceptance, offline parsing, explicit loopback upload |
| Purpose integration | Real built plugin/controller, selection, cancellation, linked presets, empty-list reload, staged inputs, outputs and clipboard, exactly-once completion |
| Picker lifecycle | Repeated creation/destruction, QWidget and QQuickWindow hosts, Share-menu closing, reload parenting, icon caching and callback lifetime, normal QML/graphics error reporting |

The Qt C++ and QML review workflows also covered model contracts, ownership,
threading, API use, bindings, layout, delegates, loading, errors, and performance.
Functional findings were repaired and exercised. Their advisory source-pattern
linters still report formatting/preferences and intentional patterns, such as
whole-path QML lists, bounded upload-result accumulation, and Qt container use;
these are distinct from the clean Qt type/binding lint result.

To reproduce the normal checks:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-debug --parallel
cmake --build build-debug --target imshare_ui_qmllint
ctest --test-dir build-debug --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

## Visual and desktop checks

Widgets reference screenshots were captured before replacing the views. The QML
manager was compared at 1180×860 and 960×720. All seven tabs were rendered in Breeze
Light and Breeze Dark at scale factors 1 and 2. Geometry tests cover valid, invalid,
malformed, long/multiline-name, and externally changed targets. Selected error
icons remain red; the diagnostic strip does not shift the editor. The compact
Test tab scrolls without overlapping Save or the footer.

The staged Release installation was also exercised on the live Plasma desktop
through XWayland at 125% scale, using OpenGL:

- Manager startup and native KDE export file-dialog display/cancellation.
- Real KWallet write/read comparison and removal of a uniquely named temporary
  credential. No existing credential was read or changed.
- The staged picker from Dolphin and Gwenview Share menus, with native transient
  parent, modal and focused window properties inspected.
- Picker Reload, Cancel, and Configure; Configure launched the staged manager.
- Manager and host launches with the source checkout hidden in a private mount
  namespace, demonstrating that the installed QML resources are sufficient.
- A style probe using the manager's resource confirmed the desktop default and
  explicit `-style Fusion` / `QT_QUICK_CONTROLS_STYLE=Fusion` overrides.

For the isolated host check, the staged plugin was mounted over the system plugin
only inside each test process's namespace. This avoids Dolphin selecting an older
installed plugin when both copies exist. The host's installation was not modified.
The namespace must include `--dev-bind /dev /dev` for GPU access; omitting it caused
test-environment OpenGL failures. Successful runs used the normal OpenGL backend.

## Artifacts and limits

Local validation artifacts from this run:

- Debug build: `/tmp/plasma-share-qml-build`
- Release build: `/tmp/plasma-share-qml-release`
- Staged install: `/tmp/plasma-share-qml-install`
- Widgets references: `/tmp/plasma-share-qml-baseline`
- QML and desktop screenshots: `/tmp/plasma-share-qml-shots`
- CTest logs: `/tmp/plasma-share-qml-ctest-debug.txt` and
  `/tmp/plasma-share-qml-ctest-release.txt`
- Qt Quick JUnit and Markdown reports: `/tmp/plasma-share-qml-build/tests/reports/`

Temporary artifacts are not part of the source distribution. The automated suite
requires neither public upload services nor a real wallet. Public-service behavior,
other Qt versions, and interactive native-Wayland host behavior were not reverified
by this migration; the live host interaction checks used XWayland on Plasma.
