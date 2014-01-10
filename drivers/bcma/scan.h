#ifndef BCMA_SCAN_H_
#define BCMA_SCAN_H_

#define BCMA_ADDR_BASE		0x18000000
#define BCMA_WRAP_BASE		0x18100000

#define SCAN_ER_VALID		0x00000001
#define SCAN_ER_TAGX		0x00000006 
#define SCAN_ER_TAG		0x0000000E
#define  SCAN_ER_TAG_CI		0x00000000
#define  SCAN_ER_TAG_MP		0x00000002
#define  SCAN_ER_TAG_ADDR	0x00000004
#define  SCAN_ER_TAG_END	0x0000000E
#define SCAN_ER_BAD		0xFFFFFFFF

#define SCAN_CIA_CLASS		0x000000F0
#define SCAN_CIA_CLASS_SHIFT	4
#define SCAN_CIA_ID		0x000FFF00
#define SCAN_CIA_ID_SHIFT	8
#define SCAN_CIA_MANUF		0xFFF00000
#define SCAN_CIA_MANUF_SHIFT	20

#define SCAN_CIB_NMP		0x000001F0
#define SCAN_CIB_NMP_SHIFT	4
#define SCAN_CIB_NSP		0x00003E00
#define SCAN_CIB_NSP_SHIFT	9
#define SCAN_CIB_NMW		0x0007C000
#define SCAN_CIB_NMW_SHIFT	14
#define SCAN_CIB_NSW		0x00F80000
#define SCAN_CIB_NSW_SHIFT	17
#define SCAN_CIB_REV		0xFF000000
#define SCAN_CIB_REV_SHIFT	24

#define SCAN_ADDR_AG32		0x00000008
#define SCAN_ADDR_SZ		0x00000030
#define SCAN_ADDR_SZ_SHIFT	4
#define  SCAN_ADDR_SZ_4K	0x00000000
#define  SCAN_ADDR_SZ_8K	0x00000010
#define  SCAN_ADDR_SZ_16K	0x00000020
#define  SCAN_ADDR_SZ_SZD	0x00000030
#define SCAN_ADDR_TYPE		0x000000C0
#define  SCAN_ADDR_TYPE_SLAVE	0x00000000
#define  SCAN_ADDR_TYPE_BRIDGE	0x00000040
#define  SCAN_ADDR_TYPE_SWRAP	0x00000080
#define  SCAN_ADDR_TYPE_MWRAP	0x000000C0
#define SCAN_ADDR_PORT		0x00000F00
#define SCAN_ADDR_PORT_SHIFT	8
#define SCAN_ADDR_ADDR		0xFFFFF000

#define SCAN_ADDR_SZ_BASE	0x00001000	

#define SCAN_SIZE_SZ_ALIGN	0x00000FFF
#define SCAN_SIZE_SZ		0xFFFFF000
#define SCAN_SIZE_SG32		0x00000008

#endif 