# pfs3aio tools

Three commands change settings of a mounted PFS3 volume through the handler. Without a value each shows the current setting. `pfsformat` formats a partition with a reserved area of a chosen size.

## setdeldir

```
setdeldir DEVICE/A,DELDIRSIZE/N
```

Sets the number of blocks of the delete directory (`.DELDIR`) from 0 to 32, and 0 switches it off. Each block holds 31 deleted files. Making it smaller removes the deleted files in the blocks it drops. When the disk is full, it stops at the blocks it could add. The size cannot change while the delete directory or a deleted file is locked, for example open or the current directory of a shell.

Example: `setdeldir DH0: 8`

## setfnsize

```
setfnsize VOLUME/A,FILENAMESIZE/N
```

Sets the limit of file names, which holds names up to one character shorter, at least the current limit and at most 107. A new volume has 32, and the limit cannot be made smaller.

Example: `setfnsize DH0: 107`

## setlargefile

```
setlargefile VOLUME/A,ENABLE/S
```

`ENABLE` switches on large-file support for files of 4 GiB and more. It needs the large-file handler (`pfs3aio-largefile`) and a `PFS\2` volume. The normal handler answers "Command not supported by this device" and a `PFS\1` volume "Volume is not large file capable.". It cannot be switched off. Without `ENABLE` it shows whether it is on.

Example: `setlargefile DH0: ENABLE`

## pfsformat

```
pfsformat DEVICE=DRIVE/K/A,NAME/K/A,NOICONS/S,QUICK/S,DELDIRBLOCKS=DD/K/N,FILENAMESIZE=FNSIZE/K/N,VERIFY/S,RESERVED/K
```

Formats a partition as a PFS3 volume and erases everything on it. It lists the parameters and asks to press Enter first, and Ctrl-C stops it. After the format it prints the size of the reserved area and of the data area. Every failure returns 20, except a missing icon, which it only reports.

| option | default | what it does |
|---|---|---|
| `DEVICE`, `DRIVE` | required | the partition, a device name such as `DH1:` or the name of a volume on it such as `Work:`; a path is refused |
| `NAME` | required | the volume name, 1 to 30 characters without `:` and `/` |
| `QUICK` | off | writes only the new volume; without it every sector of the partition is written first, which takes long on a large partition |
| `VERIFY` | off | with a full format, reads every sector back and asks to continue or break on a mismatch |
| `NOICONS` | off | leaves out the Trashcan and the disk icon |
| `DELDIRBLOCKS`, `DD` | 2 | blocks of the delete directory, 0 to 32, 0 switches it off |
| `FILENAMESIZE`, `FNSIZE` | 32 | file names up to one character less than the value, from 32 to 107 |
| `RESERVED` | from the partition size | size of the reserved area in KiB, or in MB with `m` such as `200m` |

Example: `pfsformat DEVICE=DH1: NAME=Work QUICK RESERVED=200m`

### Delete directory

Each block of the delete directory (`.DELDIR`) is one reserved block and lists 31 files, so the default of 2 blocks keeps up to the last 62 deleted or overwritten files and 32 blocks up to 992. When it is full, the next one takes the place of the oldest entry. A listed file keeps its anodes and its data blocks count as free space. When new data takes any of them, the file leaves the list. The delete directory therefore costs no data space, only its blocks and the anodes of the listed files in the reserved area. `setdeldir` changes the number of blocks later.

### File name length

A directory entry holds a name in as many bytes as the name has, so a larger `FILENAMESIZE` costs nothing until names get longer. Longer names make the entries larger, so a directory needs more directory blocks of the reserved area. A longer name is cut to the limit. `setfnsize` raises the limit later, but it can never be lowered.

### Reserved area

The format splits the partition into the reserved area, which holds the directory blocks, the anodes, the bitmap and the delete directory, and the data area for the contents of files. Neither takes space from the other. Without `RESERVED` the handler sizes the reserved area from the size of the partition:

| partition | reserved block | default reserved area | share of the partition |
|---|---|---|---|
| 100 MB | 1 KiB | 4.8 MB | 5 % |
| 1 GB | 1 KiB | 23.5 MB | 2.5 % |
| 4 GB | 1 KiB | 61.9 MB | 1.6 % |
| 32 GB | 1 KiB | 265.7 MB | 0.9 % |
| 100 GB | 1 KiB | 701.6 MB | 0.7 % |
| 200 GB | 2 KiB | 1140 MB | 0.6 % |
| 1 TB | 4 KiB | 3010 MB | 0.3 % |

The reserved blocks are 1 KiB up to about 101 GiB, 2 KiB up to about 411 GiB and 4 KiB above, and never smaller than the blocks of the volume, so with 4096 byte blocks always 4 KiB.

1000 empty files with 12 character names take about 50 reserved blocks of 1 KiB for their directory blocks and anodes, and a fragmented file takes one anode per fragment. The default area runs out before the data area only when the files average below about 1 KB on 100 MB, rising to about 16 KB on 1 TB. Then creating files and directories and writing to files fail with 221 Disk full although there is free data space.

`RESERVED` sets the size of the reserved area at the format, and the data area changes by as much the other way. The area cannot grow later. It can also be smaller than the default, down to a minimum that holds the bitmap twice, as every commit writes each changed bitmap block to a free reserved block, and the blocks the format makes. On 2 GiB with 512 byte blocks the minimum is 1088 blocks of 1 KiB. A smaller size gives the minimum, and pfsformat says so. Each file and directory takes reserved blocks, so a small area fills up with few files. The handler refuses more than 2044 MB with 1 KiB reserved blocks, 4088 MB with 2 KiB and 8176 MB with 4 KiB, and an area larger than the partition. Without `QUICK` it refuses only after the full format has written every sector. A handler without `RESERVED` answers "RESERVED is not supported by this device" before anything is written.
