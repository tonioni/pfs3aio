# pfsdoctor

pfsdoctor checks and repairs PFS3 volumes (`PFS\1`, `PFS\2`). It works on the partition of a device, not on a mounted volume. The device is inhibited while pfsdoctor runs and comes back when it ends.

## Usage

```
pfsdoctor DEVICE/A,INFO/S,CHECK/S,REPAIR/S,SEARCH/S,UNFORMAT/S,VERBOSE/S,NONINTERACTIVE/S,LOGFILE/K,DIRECTSCSI/S
```

`DEVICE` is the device name, with or without the colon, for example `DH0:`. Exactly one of `INFO`, `CHECK`, `REPAIR`, `SEARCH` and `UNFORMAT` is required.

Example: `pfsdoctor DH0: CHECK`

## Commands

| command | what it does | writes |
|---|---|---|
| `INFO` | shows the rootblock and its extension with the options, the reserved area, the free blocks and the PFS version that formatted the disk | no |
| `CHECK` | checks the volume once and reports each error | no |
| `REPAIR` | checks the volume and fixes what it finds, in passes until a pass finds nothing, at most 7 | yes |
| `SEARCH` | has no function of its own in this version and scans like `CHECK`, but without the write protection of `CHECK`, so use `CHECK` instead | possibly |
| `UNFORMAT` | undoes a quick format and repairs the volume. Back up the data and format the partition afterwards. | yes |

Run `CHECK` first and `REPAIR` only after a backup of what can still be read.

## Options

| option | effect |
|---|---|
| `VERBOSE` | more messages |
| `NONINTERACTIVE` | answers every question with its first choice (ok, continue), which suits scripts |
| `LOGFILE` | creates the file, but this version writes nothing to it, so redirect the output instead (`pfsdoctor DH0: CHECK >RAM:check.log`) |
| `DIRECTSCSI` | tries direct SCSI access first and falls back to detecting the access mode (trackdisk, TD64, NSD, direct SCSI) |

## Questions

Without `NONINTERACTIVE`, pfsdoctor asks before it changes the volume in a way that cannot be undone. When the rootblock is not found, it offers to search for a misplaced rootblock and then to build a new one, which destroys a partition that is not PFS. When it finds a misplaced partition, it offers to write a mountlist for it. Answer `1` for the first choice and `0` to cancel.

## Return code

pfsdoctor returns 0 also when it finds or fixes errors, so read its output.
