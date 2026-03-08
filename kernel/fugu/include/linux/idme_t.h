#ifndef _LINUX_IDME_T
#define _LINUX_IDME_T
#include <linux/types.h>

#define IDME_FOOTER_OFFSET 0x8000
#define IDME_MAX_SIZE 0x4000
#define IDME_MAGIC 0xE1C6C2D5

#define ALIGMENT_STRING_LENTH 32

#define IDME_BOARD_ID_LEN       ALIGMENT_STRING_LENTH
#define IDME_BT_MAC_ADDR_LEN    ALIGMENT_STRING_LENTH
#define IDME_DEVICE_TYPE_ID_LEN ALIGMENT_STRING_LENTH
#define IDME_MAC_ADDR_LEN       ALIGMENT_STRING_LENTH
#define IDME_MAC_SEC_LEN        ALIGMENT_STRING_LENTH
#define IDME_MANUFACTURING_LEN  ALIGMENT_STRING_LENTH
#define IDME_PANELCAL_LEN       ALIGMENT_STRING_LENTH
#define IDME_PRODUCTID_LEN      ALIGMENT_STRING_LENTH
#define IDME_PRODUCTID2_LEN     ALIGMENT_STRING_LENTH
#define IDME_SERIAL_LEN         ALIGMENT_STRING_LENTH
#define IDME_UNLOCK_CODE_LEN    ALIGMENT_STRING_LENTH


#define IDME_BOARD_ID       "board_id"
#define IDME_BOOT_COUNT     "boot_count"
#define IDME_BOOT_MODE      "boot_mode"
#define IDME_DEVICE_TYPE_ID "device_type_id"
#define IDME_POSTMODE       "postmode"
#define IDME_TIME_OFFSET    "time_offset"
#define IDME_BT_MAC_ADDR    "bt_mac_addr"
#define IDME_MAC_ADDR       "mac_addr"
#define IDME_MAC_SEC        "mac_sec"
#define IDME_MANUFACTURING  "manufacturing"
#define IDME_PANELCAL       "panelcal"
#define IDME_PRODUCTID      "productid"
#define IDME_PRODUCTID2     "productid2"
#define IDME_SERIAL         "serial"
#define IDME_UNLOCK_CODE    "unlock_code"
#define IDME_FOS_FLAGS      "fos_flags"
#define IDME_DEV_FLAGS      "dev_flags"
#define IDME_USR_FLAGS      "usr_flags"

struct idme_ftr {
  __le32 magic;
  __le32 boot_count;
  __le16 bootmode;
  __le16 postmode;
  __le16 major_version;
  __le16 minor_version;
  __le32 time_offset;
  
  unsigned char board_id[IDME_BOARD_ID_LEN];
  unsigned char bt_mac_addr[IDME_BT_MAC_ADDR_LEN];
  unsigned char device_type_id[IDME_DEVICE_TYPE_ID_LEN];
  unsigned char mac_addr[IDME_MAC_ADDR_LEN];
  unsigned char mac_sec[IDME_MAC_SEC_LEN];
  unsigned char manufacturing[IDME_MANUFACTURING_LEN];
  unsigned char panelcal[IDME_PANELCAL_LEN];
  unsigned char productid[IDME_PRODUCTID_LEN];
  unsigned char productid2[IDME_PRODUCTID2_LEN];
  unsigned char serial[IDME_SERIAL_LEN];
  unsigned char unlock_code[IDME_UNLOCK_CODE_LEN];

  __le64 fos_flags;
  __le64 dev_flags;
  __le64 usr_flags;
};

#endif

