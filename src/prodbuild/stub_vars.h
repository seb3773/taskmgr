#include <stdint.h>
#include <stddef.h>

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

typedef struct {
    uint64_t page_base;
    uint64_t map_size;
    uint32_t prot;
    uint32_t padding;       // Padding explicite pour alignement
    uint64_t page_offset;
    uint64_t data_offset;
    uint64_t filesz;
} __attribute__((packed)) SegmentDesc;

#define ELF_PHOFF 64
#define SEGMENT_COUNT 4
static const SegmentDesc segments[4] = {
  {0x0, 0x2000, 1, 0, 0x0, 0x0, 0x1720}, // vaddr=0x0 flags=R--
  {0x2000, 0x5000, 5, 0, 0x0, 0x1720, 0x4da9}, // vaddr=0x2000 flags=R-X
  {0x7000, 0x3000, 1, 0, 0x0, 0x64c9, 0x20e8}, // vaddr=0x7000 flags=R--
  {0xa000, 0x2000, 3, 0, 0xc30, 0x85b1, 0x650}, // vaddr=0xac30 flags=RW-
};

// Adresse des données compressées (générée dynamiquement)
#define PACKED_DATA_ADDR 0x104000
