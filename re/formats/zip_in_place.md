# Zip in-place load

`/mnt/Games.zip` is a 1.7 GiB PKZIP archive, compression method **store**
(no deflate on members). ZXEm never extracts it to disk.

## Path syntax

```text
zxem /mnt/Games.zip --list
zxem /mnt/Games.zip#Games/Manic Miner/foo.z80
zxem /mnt/Games.zip --member 'Manic Miner'
```

`#` is the canonical member separator. `zip:member` is also accepted when
the prefix is an existing zip file.

Implementation: `emulator/src/vfs.cpp` via libzip (`zip_open` / `zip_fopen_index`
/ `zip_fread`) into a RAM blob, then `media_load`.
