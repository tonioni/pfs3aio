# setdeldir

Changes the size of the delete directory of a mounted PFS3 volume. Without a size it shows the current one.

```
setdeldir DEVICE/A,DELDIRSIZE/N
```

Sets the number of blocks of the delete directory (`.DELDIR`) from 0 to 32, and 0 switches it off. Each block holds 31 deleted files. Making it smaller removes the deleted files in the blocks it drops. When the disk is full, it stops at the blocks it could add. The size cannot change while the delete directory or a deleted file is locked, for example open or the current directory of a shell.

Example: `setdeldir DH0: 8`
