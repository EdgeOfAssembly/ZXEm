# Zip in-place load

ZXEm never extracts zip archives to disk. Members are read into RAM.

## Path syntax

```text
zxem games.zip --list
zxem games.zip#folder/title.z80
zxem games.zip --member 'title'
```

`#` is the canonical member separator. `zip:member` is also accepted when
the prefix is an existing zip file.

Implementation: `emulator/src/vfs.cpp` via libzip (`zip_open` / `zip_fopen_index`
/ `zip_fread`) into a RAM blob, then `media_load`.
