# Profile System Design Clarification

This document addresses the open design questions around the KD100 profile
system: what gets replaced on profile switch, how profile files should be
structured, what happens when no profile matches, and how hot reload should
work.

---

## 1. Current Behavior (v1.7.1) and Its Problems

### How it works now

The current profile system uses a single `profiles.cfg` file. Each profile can
optionally reference a full `config_file` (an alternate `.cfg`). When a profile
with a `config_file` is activated, the **entire** `config_t` is swapped out via
`profile_manager_get_config()` in `device.c:395-401`.

```
profiles.cfg:
  profile: Krita
  pattern: krita*
  config: krita_config.cfg   <-- optional full config replacement
  description_0: Brush (B)
  ...
```

### Problems with this approach

1. **Full config replacement is confusing.** If `config: krita_config.cfg` is
   specified, it replaces the entire configuration -- buttons, wheel, leader,
   OSD, everything. This means the user must duplicate the entire default
   config just to change a few button mappings for Krita. If they forget a
   setting, it silently reverts to the `config_create()` defaults, not to
   `default.cfg`.

2. **Profile-only descriptions are incomplete.** Without a `config_file`, a
   profile can only change `description_0..18` labels. It cannot remap actual
   key bindings, wheel functions, or leader settings. The descriptions change
   in the OSD, but the actual key functions remain whatever `default.cfg`
   defined.

3. **No visual feedback on profile switch.** When switching from Krita to
   Blender, only a debug log line appears (`Profile switched: 'Blender'`).
   The OSD descriptions update silently. The user gets no confirmation that
   their keypad is now in a different mode.

4. **Single monolithic profiles file.** All profiles live in `profiles.cfg`.
   If Krita and Blender both map button 0 to "Brush", this is duplicated
   across profile blocks. Adding or removing an app profile means editing
   a shared file.

5. **No hot reload.** Config files are loaded once at startup. Editing
   `profiles.cfg` or any profile config while the driver is running has no
   effect until restart.

6. **Unclear behavior when no profile matches.** If the user switches to
   Firefox (no profile defined), the system falls back to the default profile
   (pattern: `*`). But if no default profile exists, `active_profile_index`
   becomes -1 and `default_config` is used directly. This inconsistency is
   not documented.

---

## 2. Design Decisions

### 2.1 What parameters should profiles be able to change?

**Decision: Profiles should be able to override these parameter groups only:**

| Parameter Group     | Can Override? | Rationale |
|---------------------|:------------:|-----------|
| Button key bindings (type, function) | Yes | Core purpose of profiles |
| Button descriptions (`description_N`) | Yes | OSD labels per app |
| Leader descriptions (`leader_description_N`) | Yes | Leader OSD labels per app |
| Wheel functions (clockwise/counter-clockwise) | Yes | Different wheel behavior per app |
| Wheel descriptions (`wheel_description_N`) | Yes | Wheel OSD labels per app |
| Leader config (button, function, mode, timeout) | No | Leader key behavior is muscle memory and should be consistent across apps |
| OSD config (position, size, opacity, etc.) | No | OSD appearance is a user preference, not per-app |
| Wheel mode (sequential/sets) | No | Navigation paradigm should stay consistent |
| wheel_click_timeout | No | Timing preference, not per-app |
| enable_uclogic | No | Hardware-level setting |
| Profile settings (auto_switch, check_interval) | No | Meta-config, not per-app |

**Rationale for "No" items:** These are user-level preferences about how the
hardware behaves. Changing leader mode or OSD position when switching windows
would be disorienting. The user should set these once and have them stay
consistent. Profiles should only change what the keys **do**, not how the
hardware **feels**.

### 2.2 Overlay semantics (not full replacement)

**Decision: Profile configs overlay on top of the default config.**

When a profile is activated, the system starts with a **copy** of the default
config and then **overlays** only the parameters that the profile explicitly
defines. Parameters not mentioned in the profile config remain at their
default values.

Example:
```
default.cfg:
  Button 0
  type: 0
  function: b            <-- default: brush
  description_0: Brush

krita.cfg:
  Button 0
  function: b             <-- same key
  description_0: Brush (B)  <-- different label

  Button 4
  function: ctrl+shift+n  <-- different from default's "Insert"
  description_4: New Paint Layer
```

When Krita activates:
- Button 0: function stays `b`, description becomes `Brush (B)`
- Button 4: function becomes `ctrl+shift+n`, description becomes `New Paint Layer`
- Button 1-3, 5-18: **unchanged from default.cfg**
- Leader config: **unchanged from default.cfg**
- OSD config: **unchanged from default.cfg**
- Wheel functions: **unchanged from default.cfg** (unless profile overrides them)

### 2.3 What happens when switching to a window without a profile?

**Decision: Do nothing. Keep the current profile active.**

If the user is in Krita (Krita profile active) and switches to Firefox (no
Firefox profile), the Krita profile **remains active**. The keypad continues
to work as it was.

**Rationale:**
- The user is working in Krita and briefly checks Firefox. They should not
  lose their Krita shortcuts.
- If every unmatched window caused a revert to default, the user would
  constantly fight the profile system.
- The profile should only change when the user moves to a window that **has**
  a defined profile.

**Exception: The default profile.** A profile with `default: true` acts as a
"catch-all". If one exists, it WILL activate when no other profile matches.
This is opt-in -- if the user wants a revert-to-default behavior, they define
a default profile. If they want "sticky" behavior, they do not define one (or
remove the default profile's pattern).

**Recommendation:** Do NOT include a default catch-all profile (`pattern: *`)
by default. Let users add one themselves if they want revert-on-unfocused
behavior. The shipped example should document this choice clearly.

### 2.4 Visual feedback on profile switch

**Decision: Show a brief notification in the OSD when the active profile changes.**

When the profile switches (e.g., Krita -> Blender), the OSD should:
1. Show a brief message: `Profile: Blender` (as a recent action)
2. If in expanded mode, update all key descriptions immediately
3. If auto_show is enabled, briefly show the OSD to confirm the switch

This gives the user clear feedback that their keypad is now in a different
mode. The message format: `Profile: <profile_name>`.

### 2.5 File structure: `apps.profiles.d/` directory

**Decision: Replace the monolithic `profiles.cfg` with an `apps.profiles.d/`
directory containing one `.cfg` file per application.**

**Current (deprecated):**
```
~/.config/KD100/
  default.cfg
  profiles.cfg       <-- all profiles in one file
```

**Proposed:**
```
~/.config/KD100/
  default.cfg         <-- base config (unchanged)
  apps.profiles.d/    <-- directory of per-app profiles
    krita.cfg
    blender.cfg
    gimp.cfg
    inkscape.cfg
    terminal.cfg
```

**File format for `apps.profiles.d/krita.cfg`:**
```
// Profile: Krita
// Matched by window title/class pattern
//
// Only parameters that differ from default.cfg need to be specified.
// Unspecified parameters inherit from default.cfg.

name: Krita
pattern: krita*
priority: 10

// Key binding overrides (only specify buttons that change)
Button 0
type: 0
function: b
description_0: Brush (B)

Button 4
type: 0
function: ctrl+shift+n
description_4: New Paint Layer

// Wheel overrides (optional -- omit to keep default wheel functions)
// Wheel
// function: bracketright
// ...

// Leader description overrides (optional)
leader_description_0: Shift+Brush (B)
```

**Key properties:**
- `name:` -- Profile name (required, used in OSD messages and logs)
- `pattern:` -- Window title/class wildcard (required)
- `priority:` -- Match priority, higher wins (optional, default: 0)
- `default: true` -- Mark as fallback profile (optional)

**Why a directory?**
- Each app's config is self-contained and independently editable.
- Adding/removing an app is adding/removing a file.
- Multiple apps that share the same bindings can have identical button blocks
  without interfering with each other.
- Hot reload can target individual files rather than re-parsing everything.

**Backward compatibility:** If `profiles_file:` is set in `default.cfg`, the
old monolithic format should still work. If `profiles_dir:` is set (new
option), the directory format is used. If both are set, `profiles_dir:` takes
precedence with a warning.

### 2.6 Consistency checking

**Decision: Validate profile configs on load and reject invalid ones with
clear error messages.**

Checks to perform:

1. **Duplicate profile names:** Two files in `apps.profiles.d/` must not
   define the same `name:`. If they do, reject the second with:
   `Error: Duplicate profile name 'Krita' in blender.cfg (already defined in krita.cfg)`

2. **Duplicate patterns:** Two profiles should not have the same `pattern:`
   at the same `priority:`. Warning:
   `Warning: Profiles 'Krita' and 'ArtApp' both match pattern 'krita*' at priority 10`

3. **Button index bounds:** Button numbers must be 0-18. Values outside this
   range are rejected:
   `Error: krita.cfg: Button 25 is out of range (valid: 0-18)`

4. **Description length:** Descriptions exceeding `MAX_DESCRIPTION_LEN` (64)
   are truncated with a warning:
   `Warning: krita.cfg: description_0 truncated to 64 characters`

5. **Type validation:** Button type must be 0, 1, or 2. Invalid types:
   `Error: krita.cfg: Button 3 has invalid type 5 (valid: 0, 1, 2)`

6. **Empty function:** A button with a type but no function:
   `Warning: krita.cfg: Button 3 has type 0 but no function defined`

7. **Parameter consistency within a profile:** If a profile overrides
   `Button N`, it should specify both `type:` and `function:`. If only one
   is given, use the default for the other but warn:
   `Warning: krita.cfg: Button 3 specifies function but not type, inheriting type from default`

### 2.7 Hot reload with `inotify`

**Decision: Monitor the `apps.profiles.d/` directory for file changes using
Linux `inotify`. Reload individual profile configs when their files change.**

**Behavior:**

- On file **modification** (`IN_MODIFY`, `IN_CLOSE_WRITE`):
  Log: `Refreshing configuration for profile krita.cfg`
  Re-parse only that file. Run consistency checks. If valid, replace the
  in-memory profile. If invalid, keep the old config and warn:
  `Error: Failed to reload krita.cfg (keeping previous configuration)`

- On file **creation** (`IN_CREATE`):
  Log: `Loading new profile from krita.cfg`
  Parse the new file. Run consistency checks. If valid, add to the profile
  list. Run a profile match against the current window.

- On file **deletion** (`IN_DELETE`):
  Log: `Removing profile from deleted file krita.cfg`
  Remove the profile from the in-memory list. If this was the active profile,
  fall back to default or keep current config (per 2.3 rules).

- On **directory** changes (`IN_MOVED_TO`, `IN_MOVED_FROM`):
  Treat as creation/deletion.

**Debouncing:** Text editors often write files by creating a temp file and
renaming it. Use a short debounce window (100ms) -- after receiving an
inotify event, wait 100ms for additional events before reloading.

**Thread safety:** The inotify watcher should run in the main event loop
(non-blocking `inotify_init1(IN_NONBLOCK)`) alongside the USB and X11 event
processing. No separate thread needed. Check for inotify events on each
loop iteration, same as the profile check interval.

---

## 3. Migration Path

### Phase 1: Overlay semantics
- Modify `profile_manager_get_config()` to return a merged config (default
  overlaid with profile-specific overrides) instead of a full replacement.
- Keep `profiles.cfg` format working during transition.

### Phase 2: Directory-based profiles
- Add `profiles_dir:` config option pointing to `apps.profiles.d/`.
- Implement per-file loading with the same overlay semantics.
- Keep `profiles_file:` working for backward compatibility.

### Phase 3: Visual feedback
- Add `osd_record_action(osd, -1, "Profile: Krita")` on profile switch.
- Add profile name display to OSD expanded view header.

### Phase 4: Hot reload
- Add `inotify` monitoring for the profiles directory.
- Integrate into the main event loop.
- Add consistency re-checking on reload.

### Phase 5: Deprecate monolithic format
- Log a deprecation warning when `profiles_file:` is used.
- Provide a migration script (`profiles-migrate.sh`) that splits
  `profiles.cfg` into individual files in `apps.profiles.d/`.

---

## 4. Summary of Key Decisions

| Question | Decision |
|----------|----------|
| What gets replaced on profile switch? | Only keys, wheel, and descriptions. Not leader, OSD, or hardware settings. |
| How are profile configs applied? | Overlay on top of default.cfg. Only explicitly set parameters change. |
| Switch to window without a profile? | Do nothing. Keep current profile active. |
| Switch from Krita to Blender? | Switch to Blender profile. OSD shows "Profile: Blender". |
| Switch from Krita to Firefox (no profile)? | Keep Krita profile active (unless a default catch-all profile exists). |
| File structure? | `apps.profiles.d/` directory with one `.cfg` per app. |
| Shared key mappings across apps? | Each app file is independent. Duplication across files is acceptable. |
| Hot reload? | Yes, via `inotify` on the profiles directory. Per-file reload. |
| Consistency checking? | Validate on load: bounds, duplicates, type/function pairing. |
| Visual feedback? | OSD message "Profile: <name>" on switch. |
