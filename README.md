# Rich's Retro FATX Formatter (RRFF)

A Windows FATX formatter and file manager for original Xbox HDDs, with **Rich's Retro XDASH (RRXD)** included.

**[Download RRFF.zip](https://github.com/RichsRetro/RRFF/releases/latest/download/RRFF.zip)** — Windows 10/11 x64. Extract everything, then run **Install RRFF.exe** or use **RRFF.exe** portably.

[All downloads and release notes](https://github.com/RichsRetro/RRFF/releases/latest) · [Existing Android installer and RRXD project](https://github.com/RichsRetro/RRXD)

You can format an Xbox drive, choose what goes on it, add your own game library and browse it afterwards. The dashboard includes the file manager, DVD copying and FTP, plus the new XISO game support.

## Downloads

- **RRFF.zip** - Windows formatter and FATX file manager, with RRXD included.
- **RRXD-C.zip** - dashboard files for your Xbox C drive. Extract its contents directly to C; keep the RRXD folder together.
- **RRFF-Source.zip / RRXD-Source.zip** - source and build information.
- **LICENCES.zip** - original and third-party licence notices. Windows licence notices are also included in RRFF.zip and do not get copied into the Xbox C drive.
- **SHA256SUMS.txt** - download checksums.

The Android/Fire installer remains in the separate [RRXD repository](https://github.com/RichsRetro/RRXD/releases). This repository is for RRFF on Windows.

## Getting started

### Prepare a drive with RRFF

1. Connect your Xbox HDD to the PC using a suitable adapter or a direct connection.
2. Extract ALL files from RRFF.zip. For a desktop installation, run Install RRFF.exe. To run it portably, open RRFF.exe. Both native entry points check for .NET Framework 4.8 or newer before starting the app. If needed, they offer to download and open Microsoft's installer; an internet connection and administrator approval are required. A restart may be required. Accept RRFF's administrator prompt when opening the app.
3. Choose **Format a new HDD**, then carefully check the selected drive. Formatting erases it.
4. Choose RRXD or your own C files, then select your E and F/G contents. Read the destination preview: selecting a folder normally copies its contents. Games-folder placement is explained before you proceed.
5. Review any supported game fixes and the final erase confirmation, then let the installation finish.
6. Safely disconnect the drive and install it in your compatible hardmodded Xbox.

### Browse an existing drive

Choose **Explore an existing FATX HDD**, select the drive and open its partition. Add files/folders or drag them in from Windows. Use **Copy to PC** to export a selection. **Ctrl+A** selects everything in the current list.

If Windows asks to initialise or format an Xbox FATX drive, cancel that Windows prompt.

### Install the dashboard separately

Back up your existing C drive first. Extract RRXD-C.zip and copy its contents to the Xbox C drive, keeping the RRXD folder intact. Your BIOS must be configured to launch the supplied dashboard boot file. Licence and source downloads are separate from the clean C package.

## RRFF - Rich's Retro FATX Formatter

### Format and install

- Format an original Xbox HDD and install RRXD, your own dashboard, or RRXD alongside custom C files.
- Create C, E, X, Y, Z and F partitions.
- On drives exceeding the single-F limit, use F up to **1 TiB**, with the remainder on G.
- Choose automatic F-to-G placement, separate F/G source folders, or cancel.
- Automatic placement fills F first, then continues on G, keeping each file and game/app folder whole.
- Choose default or custom C, E, F and G contents. The selected folder normally supplies its contents; Games-folder handling shows where recognised games will be placed.
- Preserve an existing Games layout, retain a selected Games folder, and offer to put loose game folders under Games. Loose source-root XISOs go under Games.
- Resolve conflicting custom C files before erasing: keep RRXD and skip conflicts, use your C without RRXD, or cancel.
- Modern folder picker remembers your last locations.
- Select plain XISO files directly, or include them in custom F/G source folders. Ordinary games extract directly to the HDD under cleaned folder names, with patch offers for recognised executable versions. Supported image-only Tony Hawk games retain their image and get a launcher. Explorer Add files and drag/drop also accept XISOs.
- Check source files, available partition space and the planned layout before erasing.
- Exclude Windows, system, boot and page-file disks; recheck target identity before writing.
- Request administrator access on opening.

### Browse and transfer

- Browse existing FATX partitions and folders, including F and G.
- Import Windows files/folders, including drag and drop into RRFF, and export selections using Copy to PC.
- Dragging out into Windows is not included in this release.
- Ctrl+A selects all entries in the file list.
- Create folders and delete items after confirmation, with batched deletion and verified metadata updates.
- Cut, copy and paste with menus and keyboard shortcuts.
- Drag into a folder to move within a partition; hold Ctrl to copy.
- Buffered imports and queued exports overlap reading and writing while keeping memory use bounded.
- Visible preparation/scanning stages before Copy to PC begins.
- Progress, counts, data copied, Mbps, elapsed time and estimated time remaining.
- Completion summaries and saved operation results.

### Check and prepare games

- Quick verification of partition structure and C/E by default, with optional full F/G file read-back.
- Preserve empty directories in imported XISO images, including padded empty-directory tables.
- Read-only FATX integrity checking, with the report inside the explorer window.
- Check partition geometry, directory entries, file sizes, cluster chains, cross-links and orphaned clusters.
- Show supported game fixes together in one approval list.
- Match game versions using title IDs and executable fingerprints.
- Apply supported fixes to the destination HDD copy, leaving your Windows originals alone.
- Convert recognised Tony Hawk's American Wasteland and Project 8 folders into XISO images while copying.
- Stream image creation directly to the HDD without a large temporary image and second copy pass.
- Install launchers for the tested M8+ RAM-hook and Cerbios mounting routes.
- Branded Windows shortcut and Buy Me a Coffee link.

## RRXD - Rich's Retro XDASH

### Dashboard

- Browse and launch games and applications, with game artwork and manual library refresh.
- Scan F/G for Games and E/F/G for Apps.
- Disc status, free space, temperature and system information.
- Rich's Retro theme and splash artwork.
- Outlined text for TV readability, safe-area controls and date/time settings.
- Organised C drive: evoxdash.xbe plus the RRXD folder, with separate assets and configuration.

### Network and FTP

- Built-in FTP with two simultaneous transfers.
- Improved large-file and small-file transfer performance.
- Recover when Ethernet is connected after startup; FTP starts when an address becomes available.
- Default FTP login: xbox / xbox, port 21. Plain FTP is intended for your trusted local network.

### File manager

- Browse Xbox drives and folders; copy, move and delete.
- Separate delete confirmation and readable menus, with long names shortened for display.
- Operation progress, current item, file counts and available byte totals.
- Clear completion and failure messages.

### DVD to HDD

- DVD copying built into the dashboard.
- Game-size and available-space checks before copying.
- Progress, elapsed time and completion totals.
- Cancel confirmation and incomplete-copy cleanup.
- Automatic Games refresh after copying.
- Menu navigation remains responsive while a disc initially loads.

### XISO launching

- Load the tested M8+ kernel hooks automatically when XDASH starts.
- Hooks stay in RAM after the dashboard closes; RRXD installs them again on its next compatible startup.
- Use Cerbios's native mounting interface on Cerbios instead of applying the M8+ patch.
- Mount supported images as DVD media and launch them.
- No BIOS flashing by the dashboard.

## What's been tested

- Tony Hawk XISO launching on M8+ with automatic RAM hooks, and on Cerbios.
- Virtual 2 TB formatting, F/G geometry, whole-game overflow planning, file import/export and clean FATX metadata checks.
- A scaled F/G format on the 60 GB Hitachi, full copied-data read-back and zero filesystem errors/warnings.
- Both F and G test applications launched on the real Xbox.

**A physical 2 TB HDD has not yet been tested. Large-drive support remains experimental until that final hardware test.** Virtual tests do not establish full-size hardware or adapter compatibility.

- My latest test library contains **66 games, and all 66 worked on the Xbox**. This covers those game copies and versions, rather than every release or a full playthrough of each game.
- Direct XISO import checked with real images, patched-executable read-back and clean virtual FATX metadata. Empty-directory handling was verified against the seven supplied images and malformed-image checks.

### Example hardware results

On a 500 GB Seagate HDD, a complete format/install copied **169.73 GB across 147,924 files in 53 minutes 49 seconds**, including quick verification. This run did not include full F/G file read-back.

The Windows explorer also completed a separate HDD-to-PC library export in **23 minutes 59 seconds**. A game deletion reported **14 seconds** on the test HDD. These are individual test results; speed depends on your drive, adapter, source files and file count.

## Requirements and limits

Windows 10/11 x64, .NET Framework 4.8 and administrator access. Xbox formatting currently requires 512-byte logical sectors and a supported total size below 2 TiB, including standard advertised 2 TB drives. F's maximum 1 TiB is 1,099.51 decimal GB before filesystem overhead; usable capacity is lower. No retail/softmod HDD locking or EEPROM extraction is provided.

Use a suitable hardmodded Xbox and BIOS supporting the on-disk LBA48 partition table. Back up your existing dashboard and confirm its boot path before replacing C files. Do not install this hardmod C package over a retail or softmod setup.

Formatting erases the selected drive. Existing-drive exploration does not format it, but adding/deleting files changes its contents. The integrity checker is read-only: it is not SMART, a full media scan or an automatic repair tool. Cross-partition clipboard transfers and automatic overflow on later explorer imports are not included; choose the destination partition explicitly.

Game fixes are version-specific. Unknown unsupported names are reported rather than blindly shortened. FATX still has a 42-byte filename limit and files below 4 GiB; current automatic Tony Hawk conversion supports single-file XISOs below 4 GiB. Successful launch testing is not a full-playthrough guarantee.

No commercial games, BIOS images, EEPROMs, saves or private logs are included.

## Licences

RRFF and RRXD have separate project licence files covering their original code and assets. Read these before using, modifying or redistributing them; source availability does not mean unrestricted redistribution. Bundled third-party components retain their own licences and notices. Keep the supplied licence documents with your download.

## Known limits

- Physical 2 TB testing remains outstanding; larger-drive support is experimental.
- The FATX checker reports problems but does not repair them.
- Cross-partition clipboard transfers and automatic F-to-G overflow on later explorer imports are not included.
- Windows drag-out is not included; use Copy to PC.
- M8+ image hooks require starting RRXD. Booting directly into another dashboard does not install them. Cerbios uses its own native image support.
- Single-file XISO size limits still apply; split/compressed/full-disc image imports are not supported.

---
If this saved you a headache, you can [buy me a coffee](https://buymeacoffee.com/richsretro). Completely optional, and appreciated!


## Source layout

WindowsApp contains the managed formatter and embedded dashboard payloads. NativeSetup contains the native launcher and installer. Dashboard contains the matching dashboard source and assets. Full dashboard source including nxdk is supplied in **RRXD-Source.zip** on the release page; see BUILDING.txt and Dashboard/README.txt.
