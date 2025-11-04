---
apply: always
---

This is a Windows x64 application implementing a disk analyser and defragmenter for 
Windows. This is a simple GUI application with top menu, all disks information panel, 
current drive map panel and controls and info panel in the bottom.

The supported functions are:
- Analysis of a drive (file map, fragmentation of files and free space, etc.)
- Selecting a move strategy and moving files. 
- Multiple move strategies supported such as: defragment a file, optimize hot zones 
  (frequently accessed files moved to faster disk zone), optimize cold zones (rarely
  accessed files moved to slower zone), directory clustering files together, file type
  and size segregation, access time based sorting, size-based placement, free space 
  optimization, packing files in sequential zones to optimize read-ahead caching.
- All the above while trying to maintain minimal amount of moved data.
