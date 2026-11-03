# RRFF for Windows v1.1.2

## Portable folder preferences

- Added a first-run Accept / Decline prompt to the portable app.
- Accept saves previously used folder locations and language preferences in `RRFF-preferences` beside `RRFF.exe`. Acceptance is saved there too, so the prompt does not appear again while that file is present.
- Decline continues with session-only folder memory, without saving RRFF preferences or diagnostic logs. It does not save a refusal, so the next launch asks again.
- If the launch folder cannot be written, RRFF explains that folder choices will only be remembered for the current session.
- The prompt is available in English, German, Spanish, French, Italian and Portuguese.
- Installed mode keeps its existing settings location and does not show the portable prompt.

## Updating

Close RRFF. Extract all of the new RRFF.zip to a new folder, keeping App and Licenses beside RRFF.exe. Run RRFF.exe for portable use. Keep your existing RRFF-preferences folder beside the new launcher if you want to retain portable choices on future updates.

For an existing installed copy, back up its folder before replacing its app files. The installer still refuses to overwrite an existing installation.

This is a Windows app update. The bundled RRXD dashboard remains the v1.1.1 build; no Xbox dashboard update is needed.
