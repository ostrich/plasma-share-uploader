# Configuration application: planning and requirements

Status: Phase 1 implementation authorized by the user on 2026-09-09 and now
implemented. Later phases remain proposals. Created
2026-09-09 following the configuration-app brainstorm.
Baseline: version 0.3.0, commit `e711558`.

This document records intended workflows, features, constraints, open decisions,
and delivery phases. The [app guide](configuration-app.md) describes implemented
behavior; unchecked work and later phases are not release commitments.

## Purpose and design direction

Build a target manager with a capable editor and test panel, plus a small set of
application preferences. The normal workflow should be:

1. Add or enable an upload service.
2. Supply any required endpoint, credentials, or service options.
3. Validate the configuration and optionally test an upload.
4. Use the target from the existing Plasma Share workflow.

Use ShareX's custom uploader capabilities as a feature reference. Choose a UI
that fits this application and Plasma; reproducing ShareX's appearance is not a
requirement. Preserve the ability to manage target JSONs manually.

### Existing decisions to preserve

- Enabled targets come from top-level JSON entries in the active target directory.
- Symlinks enable packaged presets and receive package updates. Regular files
  are complete, independent custom targets.
- Disabling targets must not reintroduce a separate enabled/disabled override
  list, implicit user-over-system merging, or `disabledBundledTargets`.
- An existing empty active directory stays empty. Only a missing directory
  receives the initial default links; new package presets are not silently enabled.
- Keep the removal of the legacy state mechanism. Do not add a permanent one-time
  migration framework as part of this app.
- Uploaded originals are never modified by preprocessing.

### Initial implementation decisions

- Start with a standalone Qt Widgets application, a desktop launcher, and a
  **Configure...** button in the Share picker.
- Use a searchable target list and an adjacent editor. General application
  preferences are deferred until after this first target-management release.
- Use KWallet for managed credentials while retaining environment references.
- Share parsing, validation, preprocessing, and uploading code with the plugin.
- Deliver a complete editor for the existing format before pursuing broad
  ShareX compatibility, history, or automation features.

## Baseline capabilities and planned additions

The baseline below describes 0.3.0. The configuration application exposes the
existing engine rather than
implement a second uploader. See [the target format](../README.md#target-format)
for the authoritative user documentation.

| Area | Existing support | Proposed additions |
| --- | --- | --- |
| Target selection | Runtime picker; MIME and extension filtering; file-specific diagnostics | Management UI, favorites, ordering, preferred target |
| Requests | POST/PUT; multipart, raw file, form URL encoded, JSON; headers and query parameters | Friendly auth controls, declared setup inputs, configurable network behavior |
| Substitution | Environment references and filename substitution | Credential references, typed setup values, limited output composition |
| Responses | Text URL, JSON pointer, regex, header, redirect URL, XML XPath; optional thumbnail/deletion/error extraction | Interactive parser testing; relative URL/ID composition; direct versus viewer URL selection |
| Preprocessing | Ordered MIME rules; first match wins; temporary-copy or output-file commands; per-rule timeout | Rule editor, dependency checks, local previews, selected convenience actions |
| Completion | Copies completed URLs to clipboard; notifications; preserves completed results on later failure | Configurable formatting/notifications, explicit retry workflow, optional history |

The current upload request has a fixed 30-second transfer timeout. Preprocessing
rules already support `timeoutMs`, defaulting to 30 seconds. These are different
settings and must not be presented as one timeout.

## Main window and target lifecycle

### Target list

The list should show name, destination hostname, enabled state, and actionable
problems such as missing credentials, invalid configuration, broken links, or
missing external programs. Provide search and filters for enabled, disabled,
custom, packaged, and attention-needed targets.

Keep packaged presets that are available to add distinct from the user's
configured targets. **Add Target...** should offer a packaged preset, a template
requiring setup, import, or a blank definition. A separate online catalog is not
required for the first release.

Do not equate these statuses:

- **Configuration valid:** syntax and supported schema pass validation.
- **Setup complete:** required values and dependencies are available locally.
- **Upload tested:** a particular configuration successfully uploaded a file at
  a recorded time. Editing relevant settings makes this result historical.
- **Documentation checked:** compatibility was compared with identified service
  documentation or source. This is not proof of current service availability.

### Operations

| Operation | Required behavior |
| --- | --- |
| Enable packaged preset | Create its active symlink; detect filename/ID conflicts before changing anything. |
| Disable packaged preset | Remove its active link. Its packaged definition remains available to add again. |
| Disable custom target | Move the definition into `targets/disabled/`, preserving it for later re-enabling. |
| Enable custom target | Move the disabled definition back into the active directory after validation and conflict checks. |
| Customize packaged preset | Replace the active link with an independent copy, then edit the copy. Never write through the link into the packaged file. |
| Duplicate target | Create a complete definition with a new unique ID, supporting separate personal/work accounts or instances. |
| Add from template | Collect required setup values and create an independent definition; incomplete drafts must not become active. |
| Delete custom target | Distinguish deletion from disabling and handle it as removal of the user's definition. |
| Restore packaged version | Explicitly replace a customized copy with the packaged link, with a clear account of the custom changes being discarded. |

The manager must use the same first-run initialization semantics as the plugin.
Creating its working directories must not accidentally turn a missing target
directory into an intentionally empty one before default initialization.

A custom target may eventually record which preset it was based on, enabling
**Compare with packaged preset** after updates. This is informational provenance,
not runtime inheritance or a source of merged fields. Existing links to other
locations must be recognized without assuming the manager owns their destination.

## Target editor

All pages edit one complete target definition. Basic controls and an advanced
JSON view must not maintain competing copies of the configuration.

| Page | Requirements |
| --- | --- |
| General | Edit name, description, icon, MIME rules, and extensions. Expose the internal ID as an advanced field with uniqueness validation. Add a documentation link and file-size limits when the schema supports them. |
| Request | Edit endpoint, supported method, body format, file field, raw content type, headers, query parameters, and body values. Only show controls relevant to the body format. Detect duplicate names instead of silently replacing entries. Preserve JSON types. |
| Authentication | Offer anonymous, API key, bearer token, and basic authentication setup. Support the header/query/form placement required by the service. Auth helpers produce the same request configuration the engine uses. |
| Response | Edit shared URL, thumbnail, deletion URL, and error extractors. Provide the fields appropriate to JSON pointer, regex/group, header, redirect, text, or XML extraction. |
| Before Upload | Add/reorder MIME rules and commands, choose temporary-copy or output-file handling, edit arguments and per-rule timeouts, and inspect required programs. |
| Test | Validate locally, test response extraction, preview preprocessing, or explicitly upload a test file. |

### Editing behavior

- Display errors next to the affected fields and provide a consolidated diagnostic
  view with links back to the editor. Invalid existing files must remain visible
  and repairable rather than disappearing from the application.
- Provide advanced JSON editing and an external-editor/open-folder action.
- Keep unsaved edits as drafts. Do not auto-save invalid intermediate edits over
  a working active target. Settle Save/Apply behavior before implementation.
- Preserve fields the editor does not understand. When an unsupported structure
  cannot safely be edited through forms, identify that limitation rather than
  silently deleting or rewriting it.
- Detect external changes while a draft is open; offer reload or explicit
  conflict resolution instead of overwriting them unnoticed.
- Keep the header, tabs, and actions stationary when changing selection. Use a
  persistent single-line status row with a Details dialog for full diagnostics;
  diagnostic length must not resize the editor.
- Explain supported placeholders near the relevant fields. Keep substitutions
  deterministic; environment values are not recursively evaluated as templates.

### Friendly service setup

For templates with declared inputs, present fields such as **Server URL**, **API
key**, **Album**, or **Expiration** as applicable. Put HTTP details under Advanced.
Do not hardcode separate application logic for every hosting service.

This requires a declared-input schema extension; it is not present in 0.3.0.
Design explicit value types, required/default values, secret markers, and bindings
to request fields. Templates should materialize a complete target definition.
Do not use setup values as an arbitrary target-override mechanism.

### Preprocessing

- Make the existing first-matching-rule behavior visible. Reordering rules can
  change which commands run; do not imply that every matching rule is a pipeline.
- Within a selected rule, show command order and argument boundaries. Execution
  remains direct argument-based process launching, without an implicit shell.
- Check whether required tools are installed. Show failures, timeouts, and useful
  stderr without silently uploading an unprocessed original after failure.
- Offer a local preview on a temporary copy, showing the resulting file, format,
  and size. A preprocessing preview must not also perform a network upload.
- Metadata removal is a useful initial convenience action. Resizing, conversion,
  and optimization can follow with explicit dependency and output-format handling.
- Show executable actions in imported definitions before the user enables or
  runs them. Parsing, viewing, and validating a definition must not execute them.

## Credentials

Implemented approach: store managed secrets in KWallet and put stable references
in target definitions. Continue to support `${ENV:...}` for manual/scripted setups.

- Support creating, replacing, forgetting, and rebinding a credential. A duplicated
  target must make it clear whether it shares a credential or uses a different one.
- Handle missing entries, a locked/unavailable wallet, and cancelled unlocks in
  both the standalone app and the host application invoking Share.
- Do not assume that an environment variable set in the manager's shell will
  exist in Dolphin, Gwenview, or another host process.
- Mask secrets in editors and request previews; diagnostic exports must redact
  known secret values in URLs, headers, fields, and returned data where present.
- Export placeholders/references by default. Import must explain which credentials
  need to be supplied on the destination machine.
- Credential storage must not become an alternate source of enabled state or
  general target fields. Removing a target must not unexpectedly remove a secret
  still used by another target.

The reference syntax is `${WALLET:name}`. Credentials use the network wallet
and its `plasma-share-uploader` folder; names are shared across targets. Missing
credentials, cancelled access, and unavailable wallets stop the upload. There is
no plaintext fallback. Credential writes are immediate; reference edits require
Save. No new general schema version or migration was introduced. Older plugins
cannot resolve wallet references; install the updated plugin with the manager.

## Validation and testing interface

Keep the following actions separate and clearly named:

| Action | Inputs | Result and side effects |
| --- | --- | --- |
| Validate configuration | Current draft and local environment | Schema, placeholder, credential, and dependency diagnostics; no upload or command execution. |
| Test response parsing | Pasted/captured body, HTTP status, headers, and response URL | Extracted URLs/error or parser diagnostics; no network request. |
| Preview preprocessing | Selected local sample and current rules | Commands run on temporary files; original unchanged; no upload. |
| Upload test file | Generated sample image or explicitly selected file | Actual upload through the shared engine, with progress and cancellation. |

The inspector should display the destination, method, request fields, file metadata,
HTTP status, response headers/body, and extracted outputs. Conceal credentials;
bound displayed response sizes. Show which preprocessing rule and output file were
used when relevant.

Allow a captured response to be reused for parser work without repeated uploads.
A JSON tree should let the user select a string value and generate its pointer.
Include error-response testing, not just success extraction. A test upload must
clearly indicate that it sends data to the displayed destination.

Use the same config parser, preprocessing implementation, uploader, and response
extractors as the plugin. Support testing an unsaved valid draft without first
making it the active runtime definition. Test results should remain in the panel;
they should not unexpectedly apply normal clipboard/notification preferences.

## Application preferences and later workflow features

These settings are proposed additions. Preferences and transient UI state should
have storage separate from complete target definitions, with clearly defined
ownership. They must not control enablement through a hidden override list.

| Area | Proposed options | Starting behavior |
| --- | --- | --- |
| Target choice | Always ask; remember last selection; preferred target | Always show the picker. Distinguish remembering selection from automatic submission. Fall back to the picker when the preferred target is unavailable or unsuitable. |
| Picker organization | Favorites, manual ordering, handling of unavailable targets | Predictable order; explain why a target cannot accept the selected files. |
| Clipboard | Enable copying; plain links, Markdown, BBCode; multi-file formatting preview | Preserve plain URLs, one per line. Formatting must not change canonical URL values returned to Purpose callers. |
| Notifications | Separate success/failure preferences | Preserve current behavior initially. Notification text must reflect whether copying is enabled. |
| Network | Inactivity timeout, system proxy, eventual concurrency limit | Preserve conservative behavior. Define inactivity separately from total duration and preprocessing timeouts. |
| Batch failure | Stop/continue policy; retry failed or remaining files | Preserve completed results. Do not blindly retry a timed-out upload that may already have succeeded remotely. |
| History | Opt-in recording, retention, search, clear history, copy/open links | No persistent upload history by default. Store useful URLs, timestamps, and target identity, not full response dumps. |

History could expose stored deletion links. Automated deletion is a separate
capability: a deletion URL may require a browser visit, confirmation form, or an
HTTP operation. Define the operation and credential requirements before offering
a button that claims to delete the remote file. Deletion tokens deserve the same
care as other secrets in history and diagnostic exports.

Direct-file versus viewer-page URL selection needs an explicit output model.
Neither a thumbnail URL nor a deletion URL is a substitute for the shared URL.
The storage and precedence of global network defaults versus any explicit
per-target network settings must be documented before adding both.

## Engine and interoperability additions

### URL composition

Support building a URL from an extracted ID or relative filename and a configured
base URL. This addresses differences among Pomf-style hosts. Define escaping,
absolute-versus-relative behavior, and final URL validation, and make composition
testable with pasted responses. Choose a small explicit model before adopting a
general expression language.

### Native import and export

- Import/export our target JSONs, with validation, ID/filename conflict handling,
  and clear credential requirements.
- Export a portable complete definition when the source is a packaged link;
  do not export a machine-specific symlink as the transferable artifact.
- Allow incomplete imports to be saved disabled for repair. Report missing or
  unsupported pieces explicitly. Loading an import must not trigger an upload.
- Decide whether the first release needs multi-file/bulk export; single-target
  transfer is the minimum useful workflow.

### ShareX import

Import the common `.sxcu` subset into our format and provide a conversion report.
Full compatibility with ShareX's request methods and expression language is not
an initial requirement.

Candidate mappings include name, endpoint, compatible request method/body type,
headers, query parameters, multipart fields/file name, and representable URL,
thumbnail, deletion, and error extractors. Translate simple JSONPath property/index
access only when its meaning is preserved by a JSON pointer.

Explicitly report unsupported methods, XML request bodies, expressions, nested
transformations, and non-file workflows. Do not silently drop them and activate an
apparently working target. The existing engine's XML response extraction does not
mean it supports XML request bodies.

Possible later features inspired by ShareX include typed per-upload prompts,
base64/encoding helpers, richer filename templates, and text/URL-sharing workflows.
OAuth, multi-request authentication, URL shortening, remote catalogs, broad
automation, and a general scripting language are not first-release commitments.

## Implementation and persistence requirements

- Extract an appropriate shared core library from the current plugin sources for
  use by the plugin, manager, and tests. Keep UI code separate from protocol logic.
- Start without a background daemon. The plugin already reloads targets for each
  new share; define when new preferences and credential changes become visible.
- Use atomic file writes and safe link replacement. A failed save must leave a
  working previous definition intact and must never modify a packaged symlink target.
- Respect XDG locations and the existing distinction between installed presets
  and development data. Keep temporary tests and previews out of the active directory.
- Preserve unknown JSON fields and handle unsupported schema versions explicitly.
  Decide whether new features need a schema version before defining their format.
- Keep the app usable through keyboard navigation, normal Plasma theme/font/DPI
  settings, resizable layouts, accessible labels, and selectable diagnostic text.
- Add launcher/install integration and a Configure action that is available even
  when the picker has no usable targets. Reuse an existing manager window where practical.
- Keep diagnostics attached to the affected target. A separate diagnostics page
  can be added if needed; it should not become the only way to locate an error.

An existing `target-manager-wip` branch contains a manager shell, management model,
and diagnostics UI. Review it for reusable presentation code. Its data model still
contains the old override concepts and must be reconciled with the current active
directory design before reuse. Do not assume it can be merged unchanged.

## Proposed delivery phases

### Phase 1: first coherent configuration-app release

- [x] Resolve storage, credential, and save behavior: filesystem activation, KWallet references, explicit Save, Save/Discard/Cancel navigation.
- [x] Share the core runtime code and establish the manager shell/launcher.
- [x] Implement target discovery and lifecycle operations under the current directory model.
- [x] Edit all currently supported target fields, with advanced JSON access and field-path diagnostics.
- [x] Add managed credentials and retain environment references.
- [x] Add native single-target import/export with credential handling.
- [x] Implement validation, offline response testing, preprocessing preview, and explicit test uploads.
- [x] Add Configure and Reload actions in the Share picker, including its empty state.
- [ ] Smoke-test installed Configure/Reload and wallet unlock/cancellation in Dolphin or Gwenview on a real Plasma desktop.

The implementation uses `plasma-share-uploader-config` / **Upload Targets**.
New, imported, and duplicated targets start as disabled drafts. Templates use the
full editor until declared-input forms are implemented. Customizing a link takes
effect on Save; single-target export materializes the definition and previews
credential redaction. No broad preferences, history, ShareX importer, or service
setup schema was added.

Automated coverage includes real manager dialogs, file/link lifecycle operations,
unknown-field preservation, credential injection into the shared test/runtime
engines, loopback uploads and response limits, and preprocessing cancellation.
Credentials are injected from an in-memory test store; these checks do not prove
live KWallet unlock behavior. Offscreen screenshots establish the basic window
layout. Live host integration, large-font/desktop-theme checks, and real provider
uploads are separate validation layers. Inline error decorations, clickable
field navigation, and reuse of an existing manager window remain UI improvements.

Avoid making history or full ShareX compatibility prerequisites for this release.

### Phase 2: easier setup and interoperability

- [ ] Add declared template inputs and friendly service setup forms.
- [ ] Add URL composition and an explicit direct/viewer output model.
- [ ] Import a documented subset of `.sxcu`, using those features where needed.
- [ ] Add clipboard formatting, favorites/ordering, selection preferences,
  notification controls, and configurable network behavior.
- [ ] Add packaged-versus-custom comparison and selected preprocessing conveniences.

### Phase 3: workflows supported by demonstrated use

- [ ] Add opt-in history and deletion-link retrieval.
- [ ] Design explicit remote-deletion operations if needed.
- [ ] Improve batch recovery/retry controls and consider bounded parallel uploads.
- [ ] Evaluate per-upload prompts, richer templates, and additional service/auth protocols.

These phases express priorities, not a fixed implementation sequence or estimates.
Move features between phases deliberately and record the reason here.

## Acceptance scenarios

Use these as review checkpoints and as a basis for meaningful integration tests:

1. Enable/disable a packaged preset and a custom target; verify the actual active
   directory and the next Share picker agree. An intentionally empty directory
   remains empty across app launches and package preset additions.
2. Customize a packaged target, save edits, and verify the installed preset is
   untouched. Duplicate it without creating an ID collision.
3. Edit through forms and JSON, preserve unknown fields, handle an external edit,
   and recover from a failed save without losing the previous working target.
4. Configure credentials in the manager and use them from a real Purpose host.
   Exercise missing/locked wallet and missing-environment cases; verify export
   and diagnostic output do not disclose known secrets.
5. Exercise every supported request/response format against a loopback server,
   including structured errors, malformed replies, redirects, and interrupted uploads.
   The manager's tests and the plugin must interpret the same definition identically.
6. Reuse a captured success or error response to fix an extractor without uploading
   again. Local validation performs no upload or command execution.
7. Preview preprocessing, including a missing tool, failed command, timeout, and
   missing output. The original file remains unchanged and failures stay visible.
8. Import/export a native target without losing behavior; for ShareX import, show
   unsupported constructs and prevent incomplete conversions from becoming active.
9. Preserve completed URLs after a batch failure. Optional formatting affects
   presentation without corrupting the machine-readable URLs returned to callers.
10. Complete the main workflow with keyboard navigation, enlarged fonts, and an
    empty or entirely invalid target list. Configuration remains reachable.

Loopback tests establish our behavior, not live provider compatibility. Record
provider documentation checks and optional live upload results separately, as in
[target API verification](target-api-verification.md). Do not make an unavailable
provider's documentation or service block unrelated local configuration work.

## Open decisions to settle before their implementation

| Decision | Current leaning / question |
| --- | --- |
| App identity and integration | Resolved for Phase 1: standalone `plasma-share-uploader-config`, display name Upload Targets, desktop launcher and picker action. A System Settings module remains optional later. |
| Editing transaction | Resolved: explicit Save; Save/Discard/Cancel on navigation; enable/disable acts immediately after resolving the draft. |
| Credential contract | Resolved: `${WALLET:name}`, network wallet / `plasma-share-uploader` folder, shared names, explicit access errors with no plaintext fallback. |
| Template inputs | Define typed input declarations and how saved values materialize a complete definition without field merging. |
| Schema evolution | Decide version handling and how older engines diagnose definitions using newer features. |
| URL composition | Explicit base URL/template with extracted values versus a limited expression grammar; establish encoding rules. |
| Selection preferences | Distinguish last-selected highlighting, preferred target, and optional automatic submission. |
| Preference ownership | Choose config/state locations; define any per-target exceptions explicitly without creating a generic override system. |
| File constraints | Decide size-limit units and whether constraints are evaluated before preprocessing, after it, or at both stages. |
| Import scope | Define the exact supported SXCU subset and behavior for embedded secrets and executable actions. |
| History and deletion | Choose storage, retention, protection for deletion links, and requirements for actual remote deletion. |
| Initial settings scope | Resolved: first release focuses on targets, existing-format editing, credentials, native JSON transfer, and tests. Broader preferences follow. |

## References

- [Current target format and behavior](../README.md#targets).
- [0.3.0 configuration model](../CHANGELOG.md#030).
- [Provider API verification and limitations](target-api-verification.md).
- [ShareX custom uploader documentation](https://getsharex.com/docs/custom-uploader):
  request configuration, output extraction, expressions, and SXCU interchange.
- [ShareX development editor implementation](https://github.com/ShareX/ShareX/blob/develop/ShareX/Presentation/CustomUploaderSettings/CustomUploaderSettingsViewModel.cs):
  upload testing and separate response-expression testing, inspected during the
  brainstorm on 2026-09-09. The development branch can change.
