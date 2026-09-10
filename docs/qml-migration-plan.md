# Migrate both application interfaces to QML

## Summary

Rewrite the configuration app and Share upload picker in Qt Quick/QML, preserving their current layout, functionality, and desktop interaction patterns. Allow normal differences in control
appearance and spacing; retain the existing screen organization.

The current eight test suites pass. Use this implementation as the behavioral baseline and capture reference screenshots before replacing its views.

## Architecture and integration

- Use Qt Quick Controls, Qt Quick Layouts, and Kirigami’s desktop styling helpers. Default the standalone manager to KDE’s desktop control style while respecting explicit user style settings.
- Embed the QML module and resources into the executable and Purpose plugin using Qt’s QML module tooling (https://doc.qt.io/qt-6/qtquick-deployment.html). Installed builds must work without the
  source checkout.

- Extract widget-owned behavior into C++ QObject controllers: target management, draft editing, credentials, test inspection, and picker selection. Expose typed properties, signals, commands, and
  Qt item models.

- Keep JSON parsing, validation, persistence, preprocessing, credential resolution, and uploading in the existing C++ backend.
- Replace application-owned Widgets screens and dialogs. Use Qt Quick Dialogs for file selection, retaining native desktop dialogs where available. Keeping QApplication and its Widgets dependency
  for KDE styling/native integration is acceptable.

- Keep the picker inside the Purpose host process. Each picker owns its QML engine/window through its job-owned controller. Do not change the host’s global Qt Quick style, graphics backend,
  application identity, or import paths.

## Layout and behavior

Configuration app

- Preserve the searchable/filterable sidebar, target actions, three-line heading, and tabs in their current order: General, Request, Credentials, Response, Before Upload, JSON, Test.
- Retain the resizable sidebar, scrolling forms, bottom Save/external-editor actions, and permanent single-line diagnostic area with a Details dialog.
- Target names, paths, diagnostics, and selected error icons must remain readable without moving the surrounding controls. Use elision, tooltips, and explicit layout constraints.
- Preserve every existing target operation: creation, presets/templates, customization, duplication, enable/disable, deletion, restore, reload, import/export review, and external editing.
- Implement Save/Discard/Cancel through an asynchronous controller transition. Resume the requested operation only after successful resolution; Cancel or a failed Save preserves the current draft
  and selection. Identify entries by stable paths rather than row numbers.

- Preserve revision checks, linked-preset protections, disabled invalid drafts, duplicate-ID diagnostics, and running-test restrictions.

Editing and credentials

- Make the C++ draft controller authoritative for raw JSON, parsed data, diagnostics, editability, and dirty state. Preserve unknown fields and JSON types; opening or switching tabs must not
  rewrite the document.

- Back editable maps/lists, preprocessing rules, commands, and arguments with Qt models. Retain incomplete edits and duplicate keys for repair; never silently collapse them into JSON or save stale
  values.

- Propagate edits before Save, selection changes, export, and tests. Invalid raw JSON remains repairable in the JSON tab; structured forms remain unavailable until it parses.
- Retain all request formats, response extractors, preprocessing options, and credential helpers.
- Keep resolved wallet secrets out of QML properties and diagnostics. Clear transient password inputs after successful operations or target changes. Guard asynchronous credential results with
  target identity and draft revision.

Test panel and Share picker

- Preserve validation, offline response parsing, editable response metadata/body, JSON-tree pointer selection, preprocessing preview, explicit uploads, progress, cancellation, diagnostic copying,
  redaction, and existing size limits.

- Keep captured test results separate from the subsequently edited draft.
- Preserve the picker’s target buttons, descriptions, icon resolution/cache behavior, diagnostics, Configure, Reload, and Cancel—including an empty target list.
- Refactor icon loading to update QML models instead of labels, retaining fallback and cache behavior.
- Parent the picker transiently to the invoking window and preserve modality. Escape/window close cancels once; reload updates the list without completing the job. Destroying the job cleans up its
  window, engine, and outstanding callbacks. QML load failure produces a normal job error.

## Verification and acceptance

- Retain backend coverage and port QWidget-dependent tests to controller tests and Qt Quick interaction tests. Exercise actual controls, including keyboard editing, focus changes, dialogs, and
  cancellation.

- Cover every tab and target lifecycle operation, unknown-field/type preservation, malformed JSON, incomplete table edits, external changes, symlink protection, import/export redaction, and
  delayed credential callbacks.

- Preserve loopback upload tests for credentials, preprocessing, response limits, cancellation, and unchanged original files. Automated tests must not require public services or a real wallet.
- Port the real Purpose controller tests for selection, cancellation, linked presets, empty-list reload, input staging, clipboard output, and exactly-once completion. Add repeated picker creation/
  destruction and host-window parenting checks.

- Compare screenshots against the Widgets baseline at 1180×860 and 960×720, in light/dark KDE themes and standard/high DPI. Verify stable shell geometry across valid, invalid, long-name, and
  externally changed targets; inspect selected error icons and keyboard focus.

- Require successful Debug/Release builds, all tests, QML linting, and staged-install launches without missing imports, binding errors, or layout loops.
- Smoke-test the installed manager and Share picker from Dolphin and Gwenview on Plasma, including window activation, native file dialogs, and real KWallet access. Report any unavailable desktop
  verification explicitly.

## Delivery and compatibility

Implement in order: capture baseline → extract/test controllers → port manager → port picker → remove superseded Widgets views → validate packaging and desktop integration.

Add Qt Declarative, Kirigami, and KDE desktop-style dependencies to the build/package instructions. Preserve executable names, CLI directory options, desktop entry, Purpose plugin identity/output,
target JSON format, filesystem layout, and wallet keys.

No configuration migration, new product features, version bump, tag replacement, or publication is included. Preserve unrelated packaging changes and generated artifacts.
