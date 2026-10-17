; assemble using 'nasm' assembler
; Created by Rocky5

;-------------------------------------------------------------------------------
; These point to the XBE to be loaded after the loader.
; Note that F: drive (Partition6) is unavailable at this time.
;
; Valid Partitions
;	E = \Device\Harddisk0\Partition1\
;	C = \Device\Harddisk0\Partition2\
;-------------------------------------------------------------------------------

; This is the main dashboard
%define DASH_PATH '\Device\Harddisk0\Partition2\Ind-Bios\Dashloader.xbe'