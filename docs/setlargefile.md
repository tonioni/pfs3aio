# setlargefile

```
setlargefile VOLUME/A,ENABLE/S
```

`ENABLE` switches on large-file support for files of 4 GiB and more. It needs the large-file handler (`pfs3aio-largefile`) and a `PFS\2` volume. The normal handler answers "Command not supported by this device" and a `PFS\1` volume "Volume is not large file capable.". It cannot be switched off. Without `ENABLE` it shows whether it is on.

Example: `setlargefile DH0: ENABLE`
