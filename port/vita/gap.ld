/* vita-elf-create appends the SCE module info and import tables to the end of the text segment.
 * When the text segment ends just below a 64 KiB boundary, ld starts the data segment right at that
 * boundary and the appended tables overlap it (vita-elf-create then corrupts its heap and crashes).
 * Keep at least 32 KiB free after the text segment. */
SECTIONS
{
  . = (ALIGN(0x10000) - . < 0x8000) ? . + 0x8000 : .;
}
INSERT AFTER .eh_frame;
