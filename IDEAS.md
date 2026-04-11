# Runtime Target Picker Ideas

Working notes for the `runtime-target-picker` branch. These are product and UX
ideas for replacing build-time target generation with a single runtime-configurable
plugin.

## Goals

- Fully replace build-time target generation.
- Ship one generic Purpose plugin.
- Load upload targets at runtime from config files.
- Let the user choose a target at share time.

## Picker UI

- The popup window should be a list of buttons for the available upload targets.
- Optimize for minimal clicking and quick target selection.
- The picker should feel like a fast launcher, not a settings form.

## Configuration UX

- Need a config screen for managing runtime upload targets.
- Need a "doctor" interface for validating config and explaining problems clearly.
- The doctor could evolve into a user-friendly editor with:
  - JSON syntax validation
  - schema-aware validation
  - helpful error messages
  - syntax highlighting
- Carry structured response metadata through the app for diagnostics and future UI:
  - HTTP status
  - response URL
  - response headers
  - raw response text

## Target Creation

- Eventually add a "target creator" interface for building new targets without
  hand-editing JSON.
- This should probably come after the config editor and doctor experience exist.

## Default Targets

- Keep a cache or catalog of default targets that can be enabled or disabled.
- Consider seeding this from a broader existing list of upload services, possibly
  inspired by ShareX targets.
- User targets and default targets should probably coexist, with user config able
  to override defaults by `id`.

## Future Ideas

- Richer parsed outputs beyond the main URL:
  - thumbnail URL
  - deletion URL
- First-class text upload targets, not just file/image targets:
  - add a text input mode alongside file input
  - support paste-style targets such as PrivateBin or Hastebin
  - keep this as a real input-model expansion, not just more example configs
- Structured runtime prompts for targets, instead of string mini-language features:
  - select-style prompts
  - free-text input prompts
- SXCU / ShareX custom uploader import as a compatibility layer, not the native
  target format.

## Open Questions

- Whether the picker should only show targets compatible with the selected files,
  or show everything and explain incompatibilities inline.
- How defaults should be stored and updated over time.
- Whether the config editor and doctor UI should be the same screen or two related
  screens.
- Extension-based filters are filename-based. For unsaved or extensionless files
  (for example content exported from another app without a suffix yet), a target
  narrowed by `extensions` may be hidden even if the file content would match by
  MIME. We may want a future fallback policy for that case.
