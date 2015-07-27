/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_esp_platform.c
 *
 * Description: The client mode configration.
 *              Check your hardware connection with the host while use this mode.
 *
 * Modification history:
 *     2014/5/09, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "os_type.h"
#include "mem.h"
#include "osapi.h"
#include "user_interface.h"
#include "task_signal.h"

#include "espconn.h"
#include "user_esp_platform.h"
#include "user_iot_version.h"
#include "upgrade.h"



/*********************************************************************
* MACROS
*/

#if HiCling_PLATFORM

#define ESP_DEBUG

#ifdef ESP_DEBUG
#define ESP_DBG CLING_DEBUG
#else
#define ESP_DBG
#endif



#define ACTIVE_FRAME    "{\"nonce\": %d,\"path\": \"/v1/device/activate/\", \"method\": \"POST\", \"body\": {\"encrypt_method\": \"PLAIN\", \"token\": \"%s\", \"bssid\": \""MACSTR"\",\"rom_version\":\"%s\"}, \"meta\": {\"Authorization\": \"token %s\"}}\n"

#define UPGRADE_FRAME  "{\"path\": \"/v1/messages/\", \"method\": \"POST\", \"meta\": {\"Authorization\": \"token %s\"},\
\"get\":{\"action\":\"%s\"},\"body\":{\"pre_rom_version\":\"%s\",\"rom_version\":\"%s\"}}\n"



#if POSITION_DEVICE
#define HTTP_HEAD_POST_SEGMENT "POST %s HTTP/1.0 \r\nHost: %s\r\nContent-Length:%d\r\n"

#define HTTP_HEAD_INFO_SEGMENT "Cache-Control: no-cache\r\n\
User-Agent: Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.1599.101 Safari/537.36 \r\n\
Accept: */*\r\n\
Accept-Encoding: gzip,deflate,sdch\r\n\
Accept-Language: zh-CN,zh;q=0.8\r\n\r\n"
#endif


#define DNS_OUPDATE_PERIOAD  3000000
#define HTTP_STATE_RESET_TIMEOUT  3000


#define VOTE_THREHOLD			20
#define HTTP_DISCONNECT_FAILED_VOTE(x)  x++
#define HTTP_VOTE_RESET(x)  x = 0
#define IS_HTTP_DISCONNECT_VOTE_SUCCESSFULLY(x) (x >= VOTE_THREHOLD)


/*********************************************************************
* TYPEDEFS
*/
struct http_parameter {
    uint8 connection_state;/*indicate curent connection state*/
    uint8 station_mode;/*current station mode*/
    uint8 ssid[32];
    uint8 password[32];
	uint16 data_mode;
    uint16 ip_obtain_reg_task_id;/*register the task id to recieve the ip obtained successfully event*/
    uint16 http_connected_reg_task_id; /*register the task id to recieve the connected successfully event*/
    uint16 http_revieved_reg_task_id;/*register the task id to recieve the message recieved event*/
    void (*reset_callback)(uint8 state);
    void (*start_connect_server_callback)(void);
    struct espconn *conn_ptr;
};




/*********************************************************************
* GLOBAL VARIABLES
*/
#ifdef USE_DNS
ip_addr_t esp_server_ip = { 0, TRUE};
#endif


/*********************************************************************
* LOCAL VARIABLES
*/
LOCAL struct espconn user_conn;
LOCAL struct _esp_tcp user_tcp;
LOCAL os_timer_t client_timer;
LOCAL os_timer_t dns_update_timer;
LOCAL os_timer_t http_statereset_timer;


LOCAL struct esp_platform_saved_param esp_param;
LOCAL uint8 device_status;
LOCAL uint8 device_recon_count = 0;
LOCAL uint32 active_nonce = 0;
//LOCAL uint8 iot_version[20] = {0};
LOCAL CLASS(http_service) *this = NULL;

struct rst_info rtc_info;
void user_esp_platform_check_ip(uint8 reset_flag);

LOCAL uint16 connect_failed_vote = 0;

/*********************************************************************
* EXTERNAL VARIABLES
*/


/*********************************************************************
* FUNCTIONS
*/
LOCAL bool set_http_object_state(uint8 state);

/******************************************************************************
 * FunctionName : user_esp_platform_get_token
 * Description  : get the espressif's device token
 * Parameters   : token -- the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_get_token(uint8_t *token)
{
    if (token == NULL) {
        return;
    }

    os_memcpy(token, esp_param.token, sizeof(esp_param.token));
}

/******************************************************************************
 * FunctionName : user_esp_platform_set_token
 * Description  : save the token for the espressif's device
 * Parameters   : token -- the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_set_token(uint8_t *token)
{
    if (token == NULL) {
        return;
    }

    esp_param.activeflag = 0;
    os_memcpy(esp_param.token, token, os_strlen(token));
    // user_esp_platform_save_param(&esp_param);
}

/******************************************************************************
 * FunctionName : user_esp_platform_set_active
 * Description  : set active flag
 * Parameters   : activeflag -- 0 or 1
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_set_active(uint8 activeflag)
{
    esp_param.activeflag = activeflag;
    // user_esp_platform_save_param(&esp_param);
}

void ICACHE_FLASH_ATTR
user_esp_platform_set_connect_status(uint8 status)
{
    device_status = status;
}

/******************************************************************************
 * FunctionName : user_esp_platform_get_connect_status
 * Description  : get each connection step's status
 * Parameters   : none
 * Returns      : status
*******************************************************************************/
#if 0
enum {
    STATION_IDLE = 0,
    STATION_CONNECTING,
    STATION_WRONG_PASSWORD
    STATION_NO_AP_FOUND,
    STATION_CONNECT_FAIL,
    STATION_GOT_IP
};
#endif

LOCAL bool ICACHE_FLASH_ATTR
get_station_state(CLASS(http_service) *arg, uint8 *phttp_state)
{
    *phttp_state = wifi_station_get_connect_status();

    if (*phttp_state == STATION_GOT_IP) {
        *phttp_state = (device_status == 0) ? DEVICE_CONNECTING : device_status;
    } else {
        /*this means the station lose connection with router thus  reset connection state here*/
        set_http_object_state(HTTP_DISCONNECT);
        os_timer_disarm(&client_timer);
        os_timer_disarm(&dns_update_timer);
        os_timer_disarm(&http_statereset_timer);
        /*reset http connection state*/
#ifdef USE_DNS
        esp_server_ip.addr= 0;
        esp_server_ip.outofdate = true;
#endif
    }

    ESP_DBG("status %d\n",*phttp_state);
    return *phttp_state;
}

/******************************************************************************
 * FunctionName : user_esp_platform_parse_nonce
 * Description  : parse the device nonce
 * Parameters   : pbuffer -- the recivce data point
 * Returns      : the nonce
*******************************************************************************/
int ICACHE_FLASH_ATTR
user_esp_platform_parse_nonce(char *pbuffer)
{
    char *pstr = NULL;
    char *pparse = NULL;
    char noncestr[11] = {0};
    int nonce = 0;
    pstr = (char *)os_strstr(pbuffer, "\"nonce\": ");

    if (pstr != NULL) {
        pstr += 9;
        pparse = (char *)os_strstr(pstr, ",");

        if (pparse != NULL) {
            os_memcpy(noncestr, pstr, pparse - pstr);
        } else {
            pparse = (char *)os_strstr(pstr, "}");

            if (pparse != NULL) {
                os_memcpy(noncestr, pstr, pparse - pstr);
            } else {
                pparse = (char *)os_strstr(pstr, "]");

                if (pparse != NULL) {
                    os_memcpy(noncestr, pstr, pparse - pstr);
                } else {
                    return 0;
                }
            }
        }

        nonce = atoi(noncestr);
    }

    return nonce;
}

/******************************************************************************
 * FunctionName : user_esp_platform_get_info
 * Description  : get and update the espressif's device status
 * Parameters   : pespconn -- the espconn used to connect with host
 *                pbuffer -- prossing the data point
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_get_info(struct espconn *pconn, uint8 *pbuffer)
{
    char *pbuf = NULL;
    int nonce = 0;

    pbuf = (char *)os_zalloc(packet_size);

    nonce = user_esp_platform_parse_nonce(pbuffer);

    if (pbuf != NULL) {
#if 0
#if PLUG_DEVICE
        os_sprintf(pbuf, RESPONSE_FRAME, user_plug_get_status(), nonce);
#elif LIGHT_DEVICE
        os_sprintf(pbuf, RESPONSE_FRAME, nonce, user_light_get_freq(),
                   user_light_get_duty(LIGHT_RED), user_light_get_duty(LIGHT_GREEN),
                   user_light_get_duty(LIGHT_BLUE), 50);
#endif
#endif

        ESP_DBG("%s\n", pbuf);
#ifdef CLIENT_SSL_ENABLE
        espconn_secure_sent(pconn, pbuf, os_strlen(pbuf));
#else
        espconn_sent(pconn, pbuf, os_strlen(pbuf));
#endif
        os_free(pbuf);
        pbuf = NULL;
    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_reconnect
 * Description  : reconnect with host after get ip
 * Parameters   : pespconn -- the espconn used to reconnect with host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_reconnect(struct espconn *pespconn)
{
    ESP_DBG("user_esp_platform_reconnect\n");

    user_esp_platform_check_ip(0);
}

/******************************************************************************
 * FunctionName : user_esp_platform_discon_cb
 * Description  : disconnect successfully with the host
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_discon_cb(void *arg)
{
    struct espconn *pespconn = arg;
    struct ip_info ipconfig;
    struct dhcp_client_info dhcp_info;
    ESP_DBG("user_esp_platform_discon_cb\n");


    if (pespconn == NULL) {
        return;
    }

    pespconn->proto.tcp->local_port = espconn_port();

    set_http_object_state(HTTP_DISCONNECT);
    //user_link_led_output(1);
#if 0
    os_timer_disarm(&client_timer);
    os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_reconnect, pespconn);
    os_timer_arm(&client_timer, 1000, 0);
#endif

}

/******************************************************************************
 * FunctionName : user_esp_platform_discon
 * Description  : A new incoming connection has been disconnected.
 * Parameters   : espconn -- the espconn used to disconnect with host
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_discon(struct espconn *pespconn)
{
    ESP_DBG("user_esp_platform_discon\n");
    set_http_object_state(HTTP_DISCONNECT);
#if (POSITION_DEVICE)
    //user_link_led_output(1);
#endif

#ifdef CLIENT_SSL_ENABLE
    espconn_secure_disconnect(pespconn);
#else
    espconn_disconnect(pespconn);
#endif
}





/******************************************************************************
 * FunctionName : user_esp_platform_sent_cb
 * Description  : Data has been sent successfully and acknowledged by the remote host.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_sent_cb(void *arg)
{
    struct espconn *pespconn = arg;
    set_http_object_state(HTTP_SENDDED_SUCCESSFULLY);
    ESP_DBG("user_esp_platform_sent_cb\n");
}




/******************************************************************************
 * FunctionName : user_esp_platform_recv_cb
 * Description  : Processing the received data from the server
 * Parameters   : arg -- Additional argument to pass to the callback function
 *                pusrdata -- The received data (or NULL when the connection has been closed!)
 *                length -- The length of received data
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_recv_cb(void *arg, char *pusrdata, unsigned short length)
{
    char *pstr = NULL;
    char *pbuffer = NULL;
    char *pmsgbuffer = NULL;
    struct espconn *pespconn = arg;
    struct http_parameter *p = this->user_data;
    uint16 json_lenth = 0;


    ESP_DBG("user_esp_platform_recv_cb\n");
    pusrdata[length] = 0;
    ESP_DBG("%s\n", pusrdata);
    /*find the start positon of json body*/
    pbuffer = (char*)os_strstr(pusrdata, "\r\n\r\n");

    if(pbuffer != NULL) {
        /*skip "/r/n/r/n" charactors to json body*/
        pbuffer += 4;
        json_lenth = length - (pbuffer - pusrdata);
        pmsgbuffer = (char*)os_malloc(json_lenth + 1);

        if (pmsgbuffer == NULL) {
            CLING_DEBUG("pmsgbuffer  v   failed!!\n");
            return;
        }
        /*copy json data to message buffer as a string*/
        os_memcpy(pmsgbuffer, pbuffer, json_lenth);
        pmsgbuffer[json_lenth] = 0;
        /*change current http object state*/
        set_http_object_state(HTTP_RECIEVED_SUCCESSFULLY);
        /*send message if any has registered*/
        if (IS_TASK_VALID(p->http_revieved_reg_task_id)) {
            system_os_post(p->http_connected_reg_task_id, HTTP_EVENT(EVENT_SERVER_RECIEVED), (uint32)pmsgbuffer);
        } else {
            /*if no task registered free the memory*/
            os_free(pmsgbuffer);
        }
        /*connect only once to server,so when recieved the reply  from server disconnected it*/
        user_esp_platform_discon(pespconn);

    }


}

/******************************************************************************
 * FunctionName : user_esp_platform_recon_cb
 * Description  : reset all state to initiate state if connect timeout or dns find timeout
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/

LOCAL bool ICACHE_FLASH_ATTR
user_esp_platform_reset_mode(void)
{
    if (wifi_get_opmode() == STATION_MODE) {
        struct http_parameter *http_data = this->user_data;
        CLING_DEBUG("elose connection for a long time reset system state\n");
        /*when tcp is disconnect but staion is still in connection with ap */
        if (wifi_station_get_connect_status() != STATION_GOT_IP) {
            os_timer_disarm(&client_timer);
            os_timer_disarm(&dns_update_timer);
            os_timer_disarm(&http_statereset_timer);
            wifi_station_disconnect();
        } else { /*station lose connection with ap*/

            /*reset http state*/
            set_http_object_state(HTTP_DISCONNECT);

        }
        if (http_data->reset_callback != NULL) {
            http_data->reset_callback(wifi_station_get_connect_status());
        }
        //wifi_set_opmode(STATIONAP_MODE);
    }
    return TRUE;
}

/******************************************************************************
 * FunctionName : user_esp_platform_recon_cb
 * Description  : The connection had an error and is already deallocated.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_recon_cb(void *arg, sint8 err)
{
    struct espconn *pespconn = (struct espconn *)arg;

    ESP_DBG("user_esp_platform_recon_cb\n");



    if (++device_recon_count%5 == 0) {
        device_status = DEVICE_CONNECT_SERVER_FAIL;

        if (user_esp_platform_reset_mode()) {
            return;
        }
    }


    os_timer_disarm(&client_timer);
    os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_reconnect, pespconn);
    os_timer_arm(&client_timer, 1000, 0);

}

/******************************************************************************
 * FunctionName : cling_platform_posting_timeout
 * Description  : this function works when the task failed to recieve the server connected
 *					event
 * Parameters   : espconn -- the espconn used to connect the connection
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
cling_platform_posting_timeout(struct espconn *pespconn)
{
    ESP_DBG("cling_platform_posting timeout\n");
    os_timer_disarm(&client_timer);
    user_esp_platform_discon(pespconn);
    /*after timeout reset http state*/
    set_http_object_state(HTTP_DISCONNECT);
}


/******************************************************************************
 * FunctionName : user_esp_platform_connect_cb
 * Description  : A new incoming connection has been connected.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_connect_cb(void *arg)
{
    struct espconn *pespconn = arg;
    struct http_parameter *p = this->user_data;
    /*disarm connecting callback timer*/
    //os_timer_disarm(&client_timer);
    ESP_DBG("user_esp_platform_connect_cb\n");

    if (wifi_get_opmode() ==  STATIONAP_MODE ) {
        wifi_set_opmode(STATION_MODE);
    }

    device_recon_count = 0;
    espconn_regist_recvcb(pespconn, user_esp_platform_recv_cb);
    espconn_regist_sentcb(pespconn, user_esp_platform_sent_cb);
    //user_esp_platform_discon(pespconn);

    if (IS_TASK_VALID(p->http_connected_reg_task_id)) {
        system_os_post(p->http_connected_reg_task_id, HTTP_EVENT(EVENT_SERVER_CONNECTED), 'a');
    }
    /*set task recieved timeout*/
    os_timer_disarm(&client_timer);
    os_timer_setfn(&client_timer, (os_timer_func_t *)cling_platform_posting_timeout, pespconn);
    os_timer_arm(&client_timer, HTTP_STATE_RESET_TIMEOUT, 0);
}

/******************************************************************************
 * FunctionName : cling_platform_connecting_timeout
 * Description  : this function works when connecting is out of time
 * Parameters   : espconn -- the espconn used to connect the connection
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
cling_platform_connecting_timeout(struct espconn *pespconn)
{
    ESP_DBG("cling_platform_connecting_timeout timeout\n");
    os_timer_disarm(&client_timer);
    user_esp_platform_discon(pespconn);
    /*after timeout reset http state*/
    set_http_object_state(HTTP_DISCONNECT);


}

/******************************************************************************
 * FunctionName : cling_platform_connecting_timeout
 * Description  : this function works when connecting is out of time
 * Parameters   : espconn -- the espconn used to connect the connection
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
cling_platform_state_reset_timeout(struct espconn *pespconn)
{
    ESP_DBG("cling_platform_connecting_timeout timeout\n");
    os_timer_disarm(&client_timer);
    user_esp_platform_discon(pespconn);
    /*after timeout reset http state*/
    set_http_object_state(HTTP_DISCONNECT);


}


/******************************************************************************
 * FunctionName : user_esp_platform_connect
 * Description  : The function given as the connect with the host
 * Parameters   : espconn -- the espconn used to connect the connection
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_connect(struct espconn *pespconn)
{
    ESP_DBG("user_esp_platform_connect\n");

    espconn_connect(pespconn);
    set_http_object_state(HTTP_CONNECTING);
    //device_status = STATION_CONNECTING;
#if 0
    os_timer_disarm(&client_timer);
    os_timer_setfn(&client_timer, (os_timer_func_t *)cling_platform_connecting_timeout, pespconn);
    os_timer_arm(&client_timer, HTTP_STATE_RESET_TIMEOUT, 0);
#endif
}

#ifdef USE_DNS
/******************************************************************************
 * FunctionName : cling_dns_update_callback
 * Description  : notify station to update dns every specific period
 * Parameters   : none
 * Returns      : status
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
cling_dns_outofdate_callback(void)
{
    esp_server_ip.outofdate == TRUE;
}

/******************************************************************************
 * FunctionName : user_esp_platform_dns_found
 * Description  : dns found callback
 * Parameters   : name -- pointer to the name that was looked up.
 *                ipaddr -- pointer to an ip_addr_t containing the IP address of
 *                the hostname, or NULL if the name could not be found (or on any
 *                other error).
 *                callback_arg -- a user-specified callback argument passed to
 *                dns_gethostbyname
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_dns_found(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;

    if (ipaddr == NULL) {
        ESP_DBG("user_esp_platform_dns_found NULL\n");

        if (++device_recon_count%5 == 0) {
            device_status = DEVICE_CONNECT_SERVER_FAIL;
            user_esp_platform_reset_mode();
        }

        return;
    }

    ESP_DBG("user_esp_platform_dns_found %d.%d.%d.%d\n",
            *((uint8 *)&ipaddr->addr), *((uint8 *)&ipaddr->addr + 1),
            *((uint8 *)&ipaddr->addr + 2), *((uint8 *)&ipaddr->addr + 3));

    if (esp_server_ip.addr == 0 && ipaddr->addr != 0) {
        os_timer_disarm(&client_timer);
        esp_server_ip.addr = ipaddr->addr;

        /*arm the uodate timer  to update dns every DNS_OUPDATE_PERIOAD*/
        os_timer_setfn(&dns_update_timer, (os_timer_func_t *)cling_dns_outofdate_callback, NULL);
        os_timer_arm(&dns_update_timer, DNS_OUPDATE_PERIOAD, 0);
        esp_server_ip.outofdate = FALSE;

        os_memcpy(pespconn->proto.tcp->remote_ip, &ipaddr->addr, 4);

        pespconn->proto.tcp->local_port = espconn_port();

#ifdef CLIENT_SSL_ENABLE
        pespconn->proto.tcp->remote_port = 8443;
#else
        pespconn->proto.tcp->remote_port = HICLING_SERVER_PORT;
#endif

#if (PLUG_DEVICE || LIGHT_DEVICE)
        ping_status = 1;
#endif
        if (1) {
            struct http_parameter *i = this->user_data;
            if (IS_TASK_VALID(i->ip_obtain_reg_task_id)) {
                struct espconn *p = (struct espconn*)(os_malloc(sizeof(struct espconn)));
                if (p != NULL) {
                    *p = *pespconn;
                    /*send  valid esponn infoemation to the task registered*/
                    system_os_post(i->ip_obtain_reg_task_id, HTTP_EVENT(EVENT_FOTA_CONFIG), (uint32)p);
                }

            }
        }

        espconn_regist_connectcb(pespconn, user_esp_platform_connect_cb);
        espconn_regist_disconcb(pespconn, user_esp_platform_discon_cb);
        espconn_regist_reconcb(pespconn, user_esp_platform_recon_cb);
        user_esp_platform_connect(pespconn);
    }
}


/******************************************************************************
 * FunctionName : user_esp_platform_dns_check_cb
 * Description  : 1s time callback to check dns found
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_dns_check_cb(void *arg)
{
    struct espconn *pespconn = arg;

    ESP_DBG("user_esp_platform_dns_check_cb\n");

    CLING_DEBUG("get hosted name state = %d\n", espconn_gethostbyname(pespconn, HICLING_DOMAIN, &esp_server_ip, user_esp_platform_dns_found));

    os_timer_arm(&client_timer, 1000, 0);
}


LOCAL void ICACHE_FLASH_ATTR
user_esp_platform_start_dns(struct espconn *pespconn)
{
    os_timer_disarm(&client_timer);
    /*this means dns result has been out of date*/
    if (esp_server_ip.outofdate == TRUE) {
        esp_server_ip.addr = 0;
        espconn_gethostbyname(pespconn, HICLING_DOMAIN, &esp_server_ip, user_esp_platform_dns_found);

        os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_dns_check_cb, pespconn);
        os_timer_arm(&client_timer, 1000, 0);
    } else {

        if (esp_server_ip.addr != 0) {

            os_memcpy(pespconn->proto.tcp->remote_ip, &esp_server_ip.addr, 4);

            pespconn->proto.tcp->local_port = espconn_port();

#ifdef CLIENT_SSL_ENABLE
            pespconn->proto.tcp->remote_port = 8443;
#else
            pespconn->proto.tcp->remote_port = HICLING_SERVER_PORT;
#endif

            espconn_regist_connectcb(pespconn, user_esp_platform_connect_cb);
            espconn_regist_disconcb(pespconn, user_esp_platform_discon_cb);
            espconn_regist_reconcb(pespconn, user_esp_platform_recon_cb);
            /*reconnect if connected failed*/
            user_esp_platform_connect(pespconn);
        }

    }
}
#endif

/******************************************************************************
 * FunctionName : user_esp_platform_check_ip
 * Description  : espconn struct parame init when get ip addr
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_check_ip(uint8 reset_flag)
{
    struct ip_info ipconfig;

    os_timer_disarm(&client_timer);

    wifi_get_ip_info(STATION_IF, &ipconfig);

    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0) {

        user_conn.proto.tcp = &user_tcp;
        user_conn.type = ESPCONN_TCP;
        user_conn.state = ESPCONN_NONE;

        device_status = DEVICE_CONNECTING;

        if (reset_flag) {
            device_recon_count = 0;
        }



#ifdef USE_DNS
        user_esp_platform_start_dns(&user_conn);
#else
        const char esp_server_ip[4] = {HICLING_IP};
        os_memcpy(user_conn.proto.tcp->remote_ip, esp_server_ip, 4);
        user_conn.proto.tcp->local_port = espconn_port();

#ifdef CLIENT_SSL_ENABLE
        user_conn.proto.tcp->remote_port = 8443;
#else
        user_conn.proto.tcp->remote_port = HICLING_SERVER_PORT;


        struct http_parameter *i = this->user_data;
        if (IS_TASK_VALID(i->ip_obtain_reg_task_id)) {
            struct espconn *p = (struct espconn*)(os_malloc(sizeof(struct espconn)));
            if (p != NULL) {
                *p = user_conn;
                /*send  valid esponn infoemation to the task registered*/
                system_os_post(i->ip_obtain_reg_task_id, HTTP_EVENT(EVENT_FOTA_CONFIG), (uint32)p);
            }
        }

#endif
        /*so that when dns is not  in use ,disconnect callback can be called */
        espconn_regist_disconcb(&user_conn, user_esp_platform_discon_cb);
        espconn_regist_connectcb(&user_conn, user_esp_platform_connect_cb);
        espconn_regist_reconcb(&user_conn, user_esp_platform_recon_cb);
        user_esp_platform_connect(&user_conn);
#endif
    } else {
        /* if there are wrong while connecting to some AP, then reset mode */
        if ((wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
             wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
             wifi_station_get_connect_status() == STATION_CONNECT_FAIL)) {
            os_printf("check ip connection error\n");
            user_esp_platform_reset_mode();
        } else {
            os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_check_ip, NULL);
            os_timer_arm(&client_timer , 100, 0);
        }
    }
}





/******************************************************************************
 * FunctionName : data_send
 * Description  : processing the data as http format and send to the client or server
 * Parameters   : arg -- argument to set for client or server
 *                responseOK -- true or false
 *                psend -- The send data
 * Returns      :
********************************** *********************************************/
LOCAL void ICACHE_FLASH_ATTR
data_send_http(struct espconn *arg, char *psend, size_t size)
{
    uint16 length = 0;
    char *pbuf = NULL;
    char httphead[1024];

    /*disarm posting timeout timer ,run to this function only means signal has been recieved*/
    os_timer_disarm(&client_timer);
    os_memset(httphead, 0, 256);

    /*full fill first header post segment*/
    os_sprintf(httphead, HTTP_HEAD_POST_SEGMENT, ((void*)os_strstr(psend, "btrssi") == NULL)?HICLING_HEALTH_PATH:HICLING_TIME_PATH, HICLING_DOMAIN, psend ? os_strlen(psend) : 0);
    os_sprintf(httphead + os_strlen(httphead), HTTP_HEAD_INFO_SEGMENT);
    if (psend) {
        /*
        os_sprintf(httphead + os_strlen(httphead),
                   "Content-type: application/json\r\nExpires: Fri, 10 Apr 2008 14:00:00 GMT\r\nPragma: no-cache\r\n\r\n");
           */
        length = os_strlen(httphead) + size;//os_strlen(psend);
        pbuf = (char *)os_zalloc(length + 1);
        os_memcpy(pbuf, httphead, os_strlen(httphead));
        os_memcpy(pbuf + os_strlen(httphead), psend, os_strlen(psend));
    } else {
        os_sprintf(httphead + os_strlen(httphead), "\n");
        length = os_strlen(httphead);
    }//if (psend)


    if (psend) {
        /*send data to server*/
        pbuf[length] =0;
        CLING_DEBUG("%s",pbuf);
        espconn_sent(arg, pbuf, length);

    }

    if (pbuf) {
        os_free(pbuf);
        pbuf = NULL;
    }
}


/******************************************************************************
 * FunctionName : init_http_service
 * Description  : Processing the message about timer from the server
 * Parameters   : pbuffer -- The received data from the server

 * Returns      : none
*******************************************************************************/
bool delete_http_service(CLASS(http_service) *arg);
bool config_http_service(CLASS(http_service) *arg, const char *ssid, const char *password);
bool connect_to_router_http_service(CLASS(http_service) *arg);
bool send_data_http_service(CLASS(http_service) *arg, uint8 *pbuf, size_t size);
LOCAL bool enter_deep_sleep_http(CLASS(http_service) *arg);
LOCAL bool get_http_state(CLASS(http_service) *arg, uint8 *phttp_state);
LOCAL bool http_object_register(CLASS(http_service) *arg, uint16 ip_obtain_task, uint16 http_connect_task, uint16 http_recieved_task);
LOCAL bool set_reset_callback(CLASS(http_service) *arg, void (*call_back)(uint8 state));
LOCAL bool set_start_connect_to_server_callback(CLASS(http_service) *arg, void (*call_back)(void));

#if 1
bool ICACHE_FLASH_ATTR
init_http_service(CLASS(http_service) *arg)
{
    assert(NULL != arg);

    /*malloc corresponed parameter buffer*/
    struct http_parameter *http_data = (struct http_parameter*)os_malloc(sizeof(struct http_parameter));
    assert(NULL != http_data);
    /*record current */
    http_data->http_connected_reg_task_id = NO_TASK_ID_REG;
    http_data->http_revieved_reg_task_id = NO_TASK_ID_REG;
    http_data->ip_obtain_reg_task_id = NO_TASK_ID_REG;
    http_data->connection_state = HTTP_DISCONNECT;

    http_data->conn_ptr = &user_conn;
    http_data->reset_callback = NULL;

    arg->user_data = (void*)http_data;

    arg->init = init_http_service;

    arg->de_init = delete_http_service;

    arg->connect_ap = connect_to_router_http_service;

    arg->config = config_http_service;

    arg->send = send_data_http_service;

    arg->sleep = enter_deep_sleep_http;

    arg->get_http_state = get_http_state;

    arg->get_station_state = get_station_state;

    arg->msgrev_task_register = http_object_register;

    arg->set_reset_callback = set_reset_callback;
    arg->set_start_connect_to_server_callback = set_start_connect_to_server_callback;
    /*reset wifi to station mode*/
    wifi_set_opmode(STATION_MODE);
    this = arg;
}
#endif


/******************************************************************************
 * FunctionName : get_http_state
 * Description	: get current http object state
 * Parameters	: arg -- object pointer
 * Returns		: ture: sucess fail :error
*******************************************************************************/
LOCAL bool ICACHE_FLASH_ATTR
get_http_state(CLASS(http_service) *arg, uint8 *phttp_state)
{

    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    *phttp_state = http_data->connection_state;

}

/******************************************************************************
 * FunctionName : get_http_state
 * Description	: get current http object state
 * Parameters	: arg -- object pointer
 * Returns		: ture: sucess fail :error
*******************************************************************************/
LOCAL bool ICACHE_FLASH_ATTR
http_object_register(CLASS(http_service) *arg, uint16 ip_obtain_task, uint16 http_connect_task, uint16 http_recieved_task)
{
    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    assert(NULL != http_data);
    http_data->ip_obtain_reg_task_id = ip_obtain_task;
    http_data->http_connected_reg_task_id = http_connect_task;
    http_data->http_revieved_reg_task_id = http_recieved_task;
}
/******************************************************************************
 * FunctionName : set_reset_callback
 * Description	: get current http object state
 * Parameters	: arg -- object pointer
 * Returns		: ture: sucess fail :error
*******************************************************************************/
LOCAL bool ICACHE_FLASH_ATTR
set_reset_callback(CLASS(http_service) *arg, void (*call_back)(uint8 state))
{
    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    assert(NULL != http_data && NULL != call_back);
    http_data->reset_callback = call_back;

}
LOCAL bool ICACHE_FLASH_ATTR
set_start_connect_to_server_callback(CLASS(http_service) *arg, void (*call_back)(void))
{
    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    assert(NULL != http_data && NULL != call_back);
    http_data->start_connect_server_callback = call_back;

}

/******************************************************************************
 * FunctionName : enter_deep_sleep_http
 * Description	: notice device to enter deep sleep
 * Parameters	: arg -- object pointer
 * Returns		: ture: sucess fail :error
*******************************************************************************/
LOCAL bool ICACHE_FLASH_ATTR
enter_deep_sleep_http(CLASS(http_service) *arg)
{

#if  LOCATION_DEVICE
    system_deep_sleep(LOCATION_DEEP_SLEEP_TIME);
#endif

}

/******************************************************************************
 * FunctionName : enter_deep_sleep_http
 * Description	: notice device to enter deep sleep
 * Parameters	: arg -- object pointer
 * Returns		: ture: sucess fail :error
*******************************************************************************/
LOCAL bool ICACHE_FLASH_ATTR
set_http_object_state(uint8 state)
{
    struct http_parameter *http_data = ((struct http_parameter*)(this->user_data));
    http_data->connection_state = state;

}



/******************************************************************************
 * FunctionName : deinit_http_service
 * Description  : Processing the message about timer from the server
 * Parameters   : pbuffer -- The received data from the server

 * Returns      : none
*******************************************************************************/

#if 1
bool ICACHE_FLASH_ATTR
delete_http_service(CLASS(http_service) *arg)
{
    assert(NULL != arg);

    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    /*free private data*/
    if (NULL != http_data) {
        os_free(http_data);
    }

    /*delete object*/
    os_free(arg);
    this = NULL;
    return TRUE;

}
#endif
/******************************************************************************
 * FunctionName : config_http_service
 * Description  : delete object from memory ,,internally used only
 * Parameters   : CLASS(http_service) *arg :object pointer

 * Returns      : none
*******************************************************************************/

#if 1
bool ICACHE_FLASH_ATTR
config_http_service(CLASS(http_service) *arg, const char *ssid, const char *password)
{

    assert(NULL != arg);
    /*once config we take it as the recoonecttion to a  new ap*/
    wifi_station_disconnect();

    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    /*check private date*/
    assert(NULL != http_data);

    os_memcpy(http_data->ssid, ssid, 32);
    os_memcpy(http_data->password, password, 32);

    return TRUE;

}
#endif

/******************************************************************************
 * FunctionName : connect_http_service
 * Description  : connect stattion to ap ,dns finding procedure followed
 * Parameters   : CLASS(http_service) *arg :object pointer

 * Returns      : none
*******************************************************************************/

#if 1
bool ICACHE_FLASH_ATTR
connect_to_router_http_service(CLASS(http_service) *arg)
{


    struct station_config stationConf;
    //need not mac address
    stationConf.bssid_set = 0;

    assert(NULL != arg);

    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    /*check private date*/
    assert(NULL != http_data);

    if (wifi_station_get_connect_status() != STATION_GOT_IP) {
        //Set station config settings
        os_memcpy(&stationConf.ssid, http_data->ssid, 32);
        os_memcpy(&stationConf.password, http_data->password, 32);

        /*set station to  station mode*/
        wifi_set_opmode(STATION_MODE);
        wifi_station_set_config(&stationConf);
        wifi_station_connect();
        /*full fill esponn area in parameter structor*/
        //wifi_station_get_config(http_data.ptr);
        //set a timer to check whether got ip from router succeed or not.
        os_timer_disarm(&client_timer);
        os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_check_ip, NULL);
        os_timer_arm(&client_timer, 100, 0);
    } else {

        uint8 state = 0;
        assert(NULL != http_data->conn_ptr);
        arg->get_station_state(arg, &state);
        if (state >= DEVICE_CONNECTING) {

            arg->get_http_state(arg, &state);
            CLING_DEBUG("get_http_state = %d\n", state);
            if (state == HTTP_DISCONNECT) {
                CLING_DEBUG("http disconnect entered!\n");

                if (http_data->start_connect_server_callback != NULL) {
                    http_data->start_connect_server_callback();
                }
                user_esp_platform_reconnect(http_data->conn_ptr);
                //cling_uart_obj->disable_recieving(cling_uart_obj);

                CLING_DEBUG("VOTE SYSTEM RESET\n");
                HTTP_VOTE_RESET(connect_failed_vote);
            } else {
                CLING_DEBUG("VOTE SYSTEM CONNECTION FAILED ONCE\n");
                HTTP_DISCONNECT_FAILED_VOTE(connect_failed_vote);
            }
            /*connected only when http object is under disconnect state*/
        }
        /*if disconnect state vote successfully then reset the vote and disconnect the platform from the server*/
        if (IS_HTTP_DISCONNECT_VOTE_SUCCESSFULLY(connect_failed_vote)) {
			CLING_DEBUG("VOTE SYSTEM CONNECTION FAILED ONCE\n");
            HTTP_VOTE_RESET(connect_failed_vote);
            user_esp_platform_discon(http_data->conn_ptr);
        }

    }

    return TRUE;

}
#endif
/******************************************************************************
 * FunctionName : send_data_http_service
 * Description  : send data to server once connected
 * Parameters   : CLASS(http_service) *arg :object pointer

 * Returns      : none
*******************************************************************************/

#if 1
bool ICACHE_FLASH_ATTR
send_data_http_service(CLASS(http_service) *arg, uint8 *pbuf, size_t size)
{


    struct station_config stationConf;
    //need not mac address
    stationConf.bssid_set = 0;

    assert(NULL != arg);

    struct http_parameter *http_data = ((struct http_parameter*)(arg->user_data));
    /*check private date*/
    assert(NULL != http_data);
    //Set station config settings
    if (wifi_station_get_connect_status() == STATION_GOT_IP) {
        if (http_data->conn_ptr->recv_callback != NULL) {
            /*send data by http*/
            data_send_http(http_data->conn_ptr, pbuf, size);

        } else {
            return FALSE;

        }

    } else {

        return FALSE;

    }

    return TRUE;

}
#endif







#endif
