# RRFF for Windows + RRXD v1.1.1

## Language support

- Added English, German, Spanish, French, Italian and Portuguese interface options to RRFF and XDASH.
- RRFF follows the Windows display language by default. Choose another language on the opening selection screen; your choice is remembered. Unsupported system languages fall back to English.
- The native Windows launcher, .NET prerequisite checker and installer now have translated prompts and buttons too. They use the system language or your saved choice and do not need .NET to run the prerequisite check.
- Added **Settings → Dashboard language** in XDASH. Its saved interface language is separate from the console game-language setting. **System default** follows the console language without changing it.
- Translated setup choices, file-operation controls, confirmation prompts, common errors and progress text.
- The complete Windows game-compatibility panel now follows the selected language, including its table and explanatory notes. Game titles and technical identifiers remain unchanged.

## Navigation

- Added **Back** to **Explore an existing FATX HDD**, returning to the opening selection screen.
- Back is disabled during operations, matching the formatter’s behaviour.

## Updating

**Windows:** Close RRFF and back up your existing installation. Extract all of RRFF.zip, then replace the files in your RRFF folder or run the extracted copy portably. Language preferences are stored separately.

**Existing Xbox dashboard:** Back up C. From RRXD-C.zip, replace `C:\evoxdash.xbe` and `C:\RRXD\default.xbe`. Keep your existing configuration and assets to retain settings. Restart the Xbox to load the update; restart XDASH after changing its interface language to refresh all cached text.

For a new Xbox setup, install the complete dashboard layout as described in README.md.

Windows-owned security prompts, Windows file-dialog controls and Microsoft’s own .NET setup use their Windows/Microsoft language settings. Protocol replies and machine status identifiers are not translated.
