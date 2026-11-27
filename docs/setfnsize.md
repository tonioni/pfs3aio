# setfnsize

Changes the file name limit of a mounted PFS3 volume. Without a value it shows the current one.

```
setfnsize VOLUME/A,FILENAMESIZE/N
```

Sets the limit of file names, which holds names up to one character shorter, at least the current limit and at most 107. A new volume has 32, and the limit cannot be made smaller.

Example: `setfnsize DH0: 107`
