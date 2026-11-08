# Larger Linux data disks

Decision: retain the existing 16 MiB seed and accept already-expanded ext2 disks up to
1 GiB. No automatic expansion, reseeding, guest-image change, or deployment in this
increment. In-place growth risks the only copy; a second guest disk adds device and
mount complexity. An offline verified copy preserves rollback and uses existing vdb.

1. Extend `test_linux_data_disk.cpp`: reopen a larger disk, exercise its last sector,
   preserve existing contents, refuse a filesystem larger than its backing file.
2. `LinuxDataDisk::open`: derive capacity from the locked existing file, bound it,
   check ext2 block geometry before exposure. Keep seed validation and seed size.
3. Add `tools/dev/grow_linux_disk.py`: require distinct source/output, lock source,
   copy into private output staging, fsck, grow, fsck again, publish without overwrite.
   Integration tests use real e2fsprogs and verify data plus unchanged original.
4. Document stop/download/grow/upload workflow, capacity and physical-test limits.
   Run focused tests, then existing terminal and Python checks. Preserve installed build.

Acceptance: no source mutation; no existing output replacement; failed resize leaves
no final output; larger disk reopens and exposes its true size; old disk still opens.
Hardware acceptance and an app Settings resize flow remain separate work.
