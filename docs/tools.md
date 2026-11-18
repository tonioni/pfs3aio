# pfs3aio tools

Three commands change settings of a mounted PFS3 volume through the handler. Without a value each shows the current setting.

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

Sets the maximum length of file names, at least 30 and the current maximum, at most 107. It cannot be made smaller.

Example: `setfnsize DH0: 107`

## setlargefile

```
setlargefile VOLUME/A,ENABLE/S
```

`ENABLE` switches on large-file support for files of 4 GiB and more. It needs the large-file handler (`pfs3aio-largefile`) and a `PFS\2` volume. The normal handler answers "Command not supported by this device" and a `PFS\1` volume "Volume is not large file capable.". It cannot be switched off. Without `ENABLE` it shows whether it is on.

Example: `setlargefile DH0: ENABLE`
