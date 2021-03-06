

#ifndef __IO_ASSIGNMENT_H__
#define __IO_ASSIGNMENT_H__



#define CLING_KEY_NUM            1
/*key number*/
#define CLING_KEY_0_IO_MUX     PERIPHS_IO_MUX_MTCK_U
#define CLING_KEY_0_IO_NUM     13
#define CLING_KEY_0_IO_FUNC    FUNC_GPIO13
#define CLING_KEY_0_ACTIVE_LEVEL  0

#define CLING_KEY_SMARTCONFIG_IO_MUX     PERIPHS_IO_MUX_GPIO5_U
#define CLING_KEY_SMARTCONFIG_IO_NUM     5
#define CLING_KEY_SMARTCONFIG_IO_FUNC    FUNC_GPIO5
#define CLING_KEY_SMARTCONFIG_ACTIVE_LEVEL  0


#if 1
#define CLING_KEY_UPGRADE_IO_MUX    PERIPHS_IO_MUX_GPIO5_U 
#define CLING_KEY_UPGRADE_IO_NUM     5
#define CLING_KEY_UPGRADE_IO_FUNC     FUNC_GPIO5
#define CLING_KEY_UPGRADE_ACTIVE_LEVEL  0

#define BLE_KEY_UPGRADE_IO_MUX    PERIPHS_IO_MUX_GPIO5_U  
#define BLE_KEY_UPGRADE_IO_NUM     5
#define BLE_KEY_UPGRADE_IO_FUNC   FUNC_GPIO5
#define BLE_KEY_UPGRADE_ACTIVE_LEVEL  0

#else

#define CLING_KEY_UPGRADE_IO_MUX    PERIPHS_IO_MUX_GPIO5_U 
#define CLING_KEY_UPGRADE_IO_NUM     5
#define CLING_KEY_UPGRADE_IO_FUNC     FUNC_GPIO5
#define CLING_KEY_UPGRADE_ACTIVE_LEVEL  0


#define BLE_KEY_UPGRADE_IO_MUX    PERIPHS_IO_MUX_MTCK_U  
#define BLE_KEY_UPGRADE_IO_NUM    13
#define BLE_KEY_UPGRADE_IO_FUNC   FUNC_GPIO13
#define BLE_KEY_UPGRADE_ACTIVE_LEVEL  0

#endif/*#if 0*/



#define CLING_WIFI_LED_IO_MUX     PERIPHS_IO_MUX_GPIO0_U
#define CLING_WIFI_LED_IO_NUM     0
#define CLING_WIFI_LED_IO_FUNC    FUNC_GPIO0

#define CLING_SMARTCONFIG_LED_IO_MUX     PERIPHS_IO_MUX_GPIO0_U
#define CLING_SMARTCONFIG_LED_IO_NUM     0
#define CLING_SMARTCONFIG_LED_IO_FUNC    FUNC_GPIO0

#define CLING_FOTA_LED_IO_MUX     PERIPHS_IO_MUX_GPIO0_U
#define CLING_FOTA_LED_IO_NUM     0
#define CLING_FOTA_LED_IO_FUNC    FUNC_GPIO0
#define CLING_FOTA_LED_ACTIVE_LEVEL  0

#define CLING_DATA_FLOW_LED_IO_MUX     PERIPHS_IO_MUX_GPIO0_U
#define CLING_DATA_FLOW_LED_IO_NUM     0
#define CLING_DATA_FLOW_LED_IO_FUNC    FUNC_GPIO0
#define CLING_DATA_FLOW_LED_ACTIVE_LEVEL  0


#define CLING_RELAY_LED_IO_MUX     PERIPHS_IO_MUX_MTDO_U
#define CLING_RELAY_LED_IO_NUM     15
#define CLING_RELAY_LED_IO_FUNC    FUNC_GPIO15

#define CLING_FLASH_CS_IO_MUX     PERIPHS_IO_MUX_GPIO0_U
#define CLING_FLASH_CS_IO_NUM     0
#define CLING_FLASH_CS_IO_FUNC    FUNC_GPIO0



#define CLING_SPI_SYNC_INPUT_IO_MUX    PERIPHS_IO_MUX_GPIO4_U 
#define CLING_SPI_SYNC_INPUT_IO_NUM     4
#define CLING_SPI_SYNC_INPUT_IO_FUNC     FUNC_GPIO4
#define CLING_SPI_SYNC_INPUT_ACTIVE_LEVEL  0
#define CLING_SPI_SYNC_INPUT_INTERRUPT_TRIGGER_LEVEL  0



#define CLING_STATUS_OUTPUT(pin, on)     GPIO_OUTPUT_SET(pin, on)



#define CLING_WIFI_INDICATOR_LED_INSTALL()  wifi_status_led_install(CLING_WIFI_LED_IO_NUM, CLING_WIFI_LED_IO_MUX, CLING_WIFI_LED_IO_FUNC)

#endif
