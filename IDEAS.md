# Runtime Target Picker Notes

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

## Open Questions

- Whether the picker should only show targets compatible with the selected files,
  or show everything and explain incompatibilities inline.
- How defaults should be stored and updated over time.
- Whether the config editor and doctor UI should be the same screen or two related
  screens.
