ISO Kernel Patcher source - GPL version 2

Copyright 2004 rmenhal; modifications by xman954. Existing Rocky5 path-file attribution is retained. These third-party files are independent of the RRFF/RRXD project licences and are not subject to the original-project redistribution restrictions.

This source reproduces the entire 5,336-byte code/data section of the supplied 9,432-byte kernel-patcher.xbe using NASM 0.98.38, default optimisation. The configuration retains kernels 5003 and 5838, disables BOOT_TO_DASH and uses LED value 0x10, matching the distributed code. Other kernel table entries and their unused patcher blocks were removed from the upstream source configuration.

Build: nasm -o rebuilt.xbe xboxapp.asm
Then: python finish-xbe.py rebuilt.xbe
NASM 0.98.38 is required for identical instruction encodings; newer NASM optimises some instructions differently. NASM and Python are build tools and are not included. An Xbox-compatible signing tool may be used for the XBE security signature. This does not modify executable instructions.

Code/data SHA-256 (file bytes from offset 4096 to end):
33ceb2d22c2f6c81b48f61bcd98615eee4741415f84e436b5ac274411045c33d

The shipped binary's XBE security signature differs from an unsigned assembly output. Its section digest is SHA-1 of the little-endian section length followed by the section bytes. finish-xbe.py restores that digest and verifies the code/data hash. No BIOS or Xbox kernel image is included.

Upstream source archive: ISO-Kernel-Patcher.7z, original ISO Kernel Patcher/CDROM modchip/My Setup/src source. The distributed reference was obtained from Rocky5/Dashloader's legacy kernelpatcher.h. Source and signature verification details are provided for independent inspection.
