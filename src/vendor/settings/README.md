# Native INI settings saves

`dbce_ini_settings_win.hpp` supplies `dbce::settings::save(path, section, changes)` for existing native INI files. Changes are an ordered list of ASCII key/value pairs. One atomic replacement saves a whole camera draft; untouched bytes, whitespace, comments, BOM and line endings survive. UTF-8/ANSI and UTF-16LE are supported. UTF-16BE, embedded NULs, oversized files, missing/duplicate target sections, duplicate owned keys and malformed lines are refused. An absent key is added inside the existing section. The function returns false on failure; callers show a save failure and keep the live draft.

The caller serializes writers and excludes external editors while running. The file handle denies content writes during read/replace; an identity check refuses an externally replaced target. This does not provide a transaction across independently renaming writers. Intended first consumer: Gravel, whose external editor refuses a running game. No device, game, force or display APIs are used.

For native mods whose first F6 save creates their INI, `save(path, section, changes, true)` explicitly allows an absent/empty file or missing target section. Existing bytes and encoding are retained when adding a section. Malformed/duplicate input still refuses. Initial installation uses a non-overwriting atomic move, so a file appearing during creation is not replaced. The default remains existing-file/existing-section only; no directory is created.

For F6 and force saves, `dbce_ini_single_key_win.hpp` supplies `saveKey(path, section, key, value)` and `saveKeys(path, section, changes)` (STD-044). Both create the INI or section on first use (never its folder) and share one mutex for the whole linked module, so route every in-process batch through `saveKeys`; other processes and external editors are not serialized. The shared panel's `[Panel]` and the FFB layer's `[FFB]` saves use `saveKey`. For plain mod INIs it writes the same bytes `WritePrivateProfileStringA` did; it keeps spacing and a BOM instead of rewriting them, refuses malformed files, and writes LF when it creates a new file. `eraseKey(path, section, key)` removes a key's line as `WritePrivateProfileStringA(section, key, nullptr)` does (a missing file, section or key is already removed), under the same mutex.

Offline fixture (MSVC x64 and x86; files are written under `build\`):

```powershell
.\native\settings\build_test.ps1
```

The fixture checks exact UTF-8/BOM/UTF-16LE output, unknown/comment retention, absent-key placement, batched updates, duplicate/malformed refusal, disk replacement/locked-file preservation, and golden `saveKey` output checked against `WritePrivateProfileStringA`. It does not establish game UI behavior.
