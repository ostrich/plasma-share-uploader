# Changelog

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
