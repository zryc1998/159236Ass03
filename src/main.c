//20008378 Chao Yue

#include <driver/adc.h>
#include <driver/gpio.h>
#include <esp_adc_cal.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <nvs_flash.h>

#include "fonts.h"
#include "graphics.h"

// put your wifi ssid name and password in here
#define WIFI_SSID "Chao's iPhone Xs"
#define WIFI_PASSWORD "12345678"
//set to '1' when use wifi
#define USE_WIFI 1

const char *tag = "T Display";
static int alarm_h;
static int alarm_m;
static bool set_alarm;
static bool alarm_triger;

static time_t time_now;
static struct tm *tm_info;

extern image_header alarm_off;
extern image_header alarm_on;
extern image_header clock_alarm_1;
extern image_header clock_alarm_2;

// for button inputs
QueueHandle_t inputQueue;
uint64_t lastkeytime=0;

// interrupt handler for button presses on GPIO0 and GPIO35
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    static int pval[2]={1,1};
    uint32_t gpio_num = (uint32_t) arg;
    int gpio_index=(gpio_num==35);
    int val=(1-pval[gpio_index]);
    
    uint64_t time=esp_timer_get_time();
    uint64_t timesince=time-lastkeytime;
    //ets_printf("gpio_isr_handler %d %d %lld\n",gpio_num,val, timesince);
    // the buttons can be very bouncy so debounce by checking that it's been .5ms since the last
    // change and that it's pressed down
    if(timesince>500 && val==0) {
        xQueueSendFromISR(inputQueue,&gpio_num,0); 
        
    }
    pval[gpio_index]=val;
    lastkeytime=time;   
    gpio_set_intr_type(gpio_num,val==0?GPIO_INTR_HIGH_LEVEL:GPIO_INTR_LOW_LEVEL);
    
}

// get a button press, returns -1 if no button has been pressed
// otherwise the gpio of the button. 
int get_input() {
    int key;
    if(xQueueReceive(inputQueue,&key,0)==pdFALSE)
        return -1;
    return key;
}

#if USE_WIFI
static EventGroupHandle_t wifi_event_group;
#define DEFAULT_SCAN_LIST_SIZE 16
/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = 0x00000001;
static esp_err_t event_handler(void *ctx, system_event_t *event) {
    //    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    //    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    //    uint16_t ap_count = 0;

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            ESP_LOGI(tag, "Connected:%s", event->event_info.connected.ssid);
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

//-------------------------------

static void initialise_wifi(void) {
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = WIFI_SSID,
                .password = WIFI_PASSWORD,
            },
    }; 
    ESP_LOGI(tag, "Setting WiFi configuration SSID %s...",
             wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*
    esp_wifi_scan_start(NULL,true);
    #define DEFAULT_SCAN_LIST_SIZE 16
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_records(&number, ap_info);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++) {
        ESP_LOGI(tag, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(tag, "RSSI \t\t%d", ap_info[i].rssi);
        ESP_LOGI(tag, "Channel \t\t%d\n", ap_info[i].primary);
    }*/
}

//-------------------------------
static void initialize_sntp(void) {
    ESP_LOGI(tag, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

//--------------------------
static int obtain_time(void) {
    static char tmp_buff[64];
    int res = 1;
    ESP_LOGI(tag, "Wifi Init");
    initialise_wifi();
    ESP_LOGI(tag, "Wifi Initialised");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true,
                        portMAX_DELAY);

    initialize_sntp();
    ESP_LOGI(tag, "SNTP Initialised");

    // wait for time to be set
    int retry = 0;
    const int retry_count = 20;

    time(&time_now);
    tm_info = localtime(&time_now);

    while (tm_info->tm_year < (2016 - 1900) && ++retry < retry_count) {
        // ESP_LOGI(tag, "Waiting for system time to be set... (%d/%d)", retry,
        // retry_count);
        sprintf(tmp_buff, "Wait %0d/%d", retry, retry_count);
        cls(0);
        print_xy(tmp_buff, CENTER, LASTY);
        flip_frame();
        vTaskDelay(500 / portTICK_RATE_MS);
        time(&time_now);
        tm_info = localtime(&time_now);
    }
    if (tm_info->tm_year < (2016 - 1900)) {
        ESP_LOGI(tag, "System time NOT set.");
        res = 0;
    } else {
        ESP_LOGI(tag, "System time is set.");
    }

    ESP_ERROR_CHECK(esp_wifi_stop());
    return res;
}
#endif
//----------------------------------------------

void app_main() {
    //vaiables needed to run outside the loop
    char day[80];
    char buff_alarm[128];
    char buff[128];
    int select=0;
    int frame=0;
    bool time_changed=true;
    
    setenv("TZ", "	NZST-24", 0);
    tzset();
    time(&time_now);
    tm_info = localtime(&time_now);

    int chr=tm_info->tm_hour;//0;
    int cmin=tm_info->tm_min;//0;
    int cmon=tm_info->tm_mon;//0;
    int cday=tm_info->tm_mday;//1;
    int cwd=tm_info->tm_wday;//0;

    // for fps calculation
    int64_t current_time;
    int64_t last_time = esp_timer_get_time();
    // queue for button presses
    inputQueue = xQueueCreate(4,4);
    ESP_ERROR_CHECK(nvs_flash_init());
    // interrupts for button presses
    graphics_init();
    set_orientation(LANDSCAPE);
    gpio_set_direction(0, GPIO_MODE_INPUT);
    gpio_set_direction(35, GPIO_MODE_INPUT);
    gpio_set_intr_type(0, GPIO_INTR_LOW_LEVEL);
    gpio_set_intr_type(35, GPIO_INTR_LOW_LEVEL);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(0, gpio_isr_handler, (void*) 0);
    gpio_isr_handler_add(35, gpio_isr_handler, (void*) 35);

    while(1){
        // ===== Set time zone ======
        setenv("TZ", "	NZST-24", 0);
        tzset();
        time(&time_now);
        tm_info = localtime(&time_now);
        int sel=0;
        int diff_hour=chr-tm_info->tm_hour;
        int diff_min=cmin-tm_info->tm_min;
        int diff_wday=cwd-tm_info->tm_wday;
        int diff_mon=cmon-tm_info->tm_mon;
        int diff_day=cday-tm_info->tm_mday;
        
        //main interface
        while(1) {
            frame++;
            cls(rgbToColour(0,0,0));
            setenv("TZ", "	NZST-24", 0);
            tzset();
            time(&time_now);
            tm_info = localtime(&time_now);

            //show alarm on/off and alarm time
            if(set_alarm==false){
                setFont(FONT_SMALL);
                setFontColour(199,217,38);
                //print_xy("OFF", 50,96);
                draw_image(&alarm_off, 56,100);
            }
            if(set_alarm==true){
                setFont(FONT_SMALL);
                setFontColour(224,74,31);
                //print_xy("ON-", 50,96);
                draw_image(&alarm_on, 56,97);
                print_xy(buff_alarm, 75, 96);
            }
            //print out time as 12 hour format with AM/PM 
            if ((tm_info->tm_hour+diff_hour) == 0){
                setFont(FONT_SMALL);
                setFontColour(200,200,200);
                print_xy("AM", 49, 57);
                snprintf(buff, 128, "12:%02d:%02d",tm_info->tm_min+diff_min, tm_info->tm_sec);
            } else if ((tm_info->tm_hour+diff_hour) == 12){
                setFont(FONT_SMALL);
                setFontColour(200,200,200);
                print_xy("PM", 49, 67);
                snprintf(buff, 128, "12:%02d:%02d",tm_info->tm_min+diff_min, tm_info->tm_sec);
            } else if (((tm_info->tm_hour+diff_hour) >=1) && ((tm_info->tm_hour+diff_hour) <12)){
                setFont(FONT_SMALL);
                setFontColour(200,200,200);
                print_xy("AM", 49, 57);
                snprintf(buff, 128, "%02d:%02d:%02d",tm_info->tm_hour+diff_hour, tm_info->tm_min+diff_min, tm_info->tm_sec);
            } else {
                setFont(FONT_SMALL);
                setFontColour(200,200,200);
                print_xy("PM", 49, 67);
                snprintf(buff, 128, "%02d:%02d:%02d",tm_info->tm_hour+diff_hour-12, tm_info->tm_min+diff_min, tm_info->tm_sec);
            }
            setFont(FONT_DEJAVU24);
            setFontColour(0, 0, 0);
            print_xy(buff, CENTER, CENTER);
            setFontColour(200, 200, 200);
            print_xy(buff, CENTER, CENTER);
            setFont(FONT_UBUNTU16);
            snprintf(day, 80, "%d",tm_info->tm_mday+diff_day);
            print_xy(day, 125, 90);
            //print day of week
            setFontColour(0, 128, 192);
            if ((tm_info->tm_wday+diff_wday)==0) print_xy("Sun", 70, 30);
            if ((tm_info->tm_wday+diff_wday)==1) print_xy("Mon", 70, 30);
            if ((tm_info->tm_wday+diff_wday)==2) print_xy("Tue", 70, 30);
            if ((tm_info->tm_wday+diff_wday)==3) print_xy("Wed", 70, 30);
            if ((tm_info->tm_wday+diff_wday)==4) print_xy("Thu", 70, 30);
            if ((tm_info->tm_wday+diff_wday)==5) print_xy("Fri", 70, 30);
            if ((tm_info->tm_wday+diff_wday)==6) print_xy("Sat", 70, 30);
            //print month
            setFontColour(200, 200, 200);
            if ((tm_info->tm_mon+diff_mon)==0)print_xy("Jan", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==1)print_xy("Feb", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==2)print_xy("Mar", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==3)print_xy("Apr", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==4)print_xy("May", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==5)print_xy("Jun", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==6)print_xy("Jul", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==7)print_xy("Aug", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==8)print_xy("Sep", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==9)print_xy("Oct", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==10)print_xy("Nov", 150, 90);
            if ((tm_info->tm_mon+diff_mon)==11)print_xy("Dec", 150, 90);

            //configeration menu
            setFont(FONT_SMALL);
            draw_rectangle(200,select*55+5,40,15,rgbToColour(100,100,100));
            if (select==0) {
                setFontColour(0, 0, 0);
                print_xy("time", 208, 7);
                setFontColour(200, 200, 200);
                print_xy("alarm",204,118);
                print_xy("WIFI", 208, 63);
            }else if (select==1) {
                setFontColour(0, 0, 0);
                print_xy("WIFI", 208, 63);
                setFontColour(200, 200, 200);
                print_xy("time", 208, 7);
                print_xy("alarm",204,118);
            }else{
                setFontColour(200, 200, 200);
                print_xy("WIFI", 208, 63);
                print_xy("time", 208, 7);
                setFontColour(0, 0, 0);
                print_xy("alarm",204,118);
            }
            send_frame();
            wait_frame();
            current_time = esp_timer_get_time();
            if ((frame % 10) == 0) {
                printf("FPS:%f %d\n", 1.0e6 / (current_time - last_time),frame);
                vTaskDelay(1);
            }
            last_time = current_time;
            int key=get_input();
            if(key==0) select=(select+1)%3;
            if(key==35) sel=select+2;
            if(sel==2){
                break;
            }
            if(sel==3){
                break;
            }
            if(sel==4){
                break;
            }
            if (((tm_info->tm_hour+diff_hour)==alarm_h) && ((tm_info->tm_min+diff_min)==alarm_m) && (set_alarm==true) && (tm_info->tm_sec==0)){
                alarm_triger=true;
                break;
            }
        }
        
        //set off alarm
        if (alarm_triger==true){
            while(1) {
                cls(rgbToColour(255,0,0));
                draw_image(&clock_alarm_2, 120, 70);
                vTaskDelay(300/portTICK_PERIOD_MS);
                flip_frame();
                cls(rgbToColour(0,128,0));//draw_rectangle(0, 0, display_width, display_height, rgbToColour(128,255,128));
                draw_image(&clock_alarm_1, 120, 70);
                vTaskDelay(300/portTICK_PERIOD_MS);
                //flip_frame();
                send_frame();
                wait_frame();
                current_time = esp_timer_get_time();
                if ((frame % 10) == 0) {
                    printf("FPS:%f %d\n", 1.0e6 / (current_time - last_time),frame);
                    vTaskDelay(1);
                }
                last_time = current_time;
                int key=get_input();
                if(key==0) break;
                if(key==35) break;
            }
            alarm_triger=false;
        }

        //config alarm
        int select_alarm=0;
        int frame_alarm=0;
        int adj_alarm=0;

        chr=tm_info->tm_hour+diff_hour;//0;
        cmin=tm_info->tm_min+diff_min;//0;
        cmon=tm_info->tm_mon+diff_mon;//0;
        cday=tm_info->tm_mday+diff_day;//1;
        cwd=tm_info->tm_wday+diff_wday;//0;
        while(sel==4) {
            frame_alarm++;
            cls(rgbToColour(0,0,0));
            setFont(FONT_DEJAVU24);
            draw_rectangle(select_alarm*30+30, 21, 30, 15, rgbToColour(100,100,100));
            setFontColour(200,200,200);
            snprintf(buff_alarm, 128, "%02d:%02d", alarm_h, alarm_m);
            print_xy(buff_alarm, CENTER, CENTER);
            setFont(FONT_UBUNTU16);
            if (set_alarm==false){
                setFontColour(199,217,38);
                print_xy("off",CENTER, 90);
            }
            if (set_alarm==true){
                setFontColour(224,74,31);
                print_xy("on",CENTER, 90);
            }
            setFont(FONT_SMALL);
            print_xy("hour  min   on   off   SET  ", 32, 25);
            send_frame();
            wait_frame();
            current_time = esp_timer_get_time();
            if ((frame_alarm % 10) == 0) {
                printf("FPS:%f %d\n", 1.0e6 / (current_time - last_time),frame_alarm);
                vTaskDelay(1);
                //vTaskDelay(200/portTICK_RATE_MS);
                //2 200ms delay (usually 100Hz ticks and portTICK_RATE_MS=10)
            }
            last_time = current_time;
            int key=get_input();
            if(key==0) select_alarm=(select_alarm+1)%5;
            if(key==35) adj_alarm=select_alarm;
            if (adj_alarm==0){
                if (key==35){
                    alarm_h++;
                    if (alarm_h>23){
                        alarm_h=0;
                    }
                }
            }
            if (adj_alarm==1){
                if (key==35){
                    alarm_m++;
                    if (alarm_m>59){
                        alarm_m=0;
                    }
                }
            }
            if (adj_alarm==2){
                if (key==35){
                    set_alarm = true;
                }
            }
            if (adj_alarm==3){
                if (key==35){
                    set_alarm = false;
                }
            }
            if(adj_alarm==4){
                break;
            }
        }
        
        //setup WIFI
#if USE_WIFI
        if (sel==3){
            cls(0);
            // once manual configuration has been used, disable wifi function.
            if (time_changed==false){
                while(1){
                    setFontColour(200, 200, 0);
                    setFont(FONT_UBUNTU16);
                    print_xy("Manual configuration", CENTER, 20);
                    print_xy("has been used", CENTER, LASTY + getFontHeight() + 2);
                    print_xy("WIFI is no longer", CENTER, LASTY + getFontHeight() + 2);
                    print_xy("available", CENTER, LASTY + getFontHeight() + 2);
                    setFontColour(200, 200, 200);
                    setFont(FONT_SMALL);
                    print_xy("Press any button to quit", CENTER, LASTY + getFontHeight() + 20);
                    send_frame();
                    wait_frame();
                    current_time = esp_timer_get_time();
                    if ((frame % 10) == 0) {
                        printf("FPS:%f %d\n", 1.0e6 / (current_time - last_time),frame);
                        vTaskDelay(1);
                    }
                    last_time = current_time;
                    int key=get_input();
                    if(key==0) break;
                    if(key==35) break;
                }
            }else{
                ESP_LOGI(tag,
                        "Time is not set yet. Connecting to WiFi and getting time "
                        "over NTP.");
                setFontColour(0, 200, 200);
                setFont(FONT_UBUNTU16);
                print_xy("Time is not set yet", CENTER, 20);
                print_xy("Connecting to WiFi", CENTER, LASTY + getFontHeight() + 2);
                print_xy("Getting time over NTP", CENTER, LASTY + getFontHeight() + 2);
                setFontColour(200, 200, 0);
                print_xy("Please Wait", CENTER, LASTY + getFontHeight() + 10);
                setFontColour(200, 200, 200);
                setFont(FONT_SMALL);
                print_xy("Press reset to quit", CENTER, LASTY + getFontHeight() + 15);
                flip_frame();
                if (obtain_time()) {
                    cls(0);
                    setFontColour(0, 200, 0);
                    print_xy("System time is set.", CENTER, LASTY);
                    flip_frame();
                } else {
                    cls(0);
                    setFontColour(200, 0, 0);
                    print_xy("ERROR.", CENTER, LASTY);
                    flip_frame();
                }
                time(&time_now);
                vTaskDelay(200);
                //	update_header(NULL, "");
                //	Wait(-2000);
            }
        }
#endif

        //config time
        int select_time=0;
        int frame_time=0;
        int adj_time=0;
        char buff_time[128];
        char buff2_time[128];
        while(sel==2) {
            frame_time++;
            cls(rgbToColour(0,0,0));
            setFont(FONT_DEJAVU24);
            draw_rectangle(select_time*30+30, 21, 30, 15, rgbToColour(100,100,100));
            setFontColour(200,200,200);
            snprintf(buff_time, 128, "%02d:%02d", chr, cmin);
            snprintf(buff2_time, 128, "%02d/%02d", cday, cmon+1);
            print_xy(buff_time, 30, 70);
            setFont(FONT_UBUNTU16);
            print_xy(buff2_time, 160, 75);
            if(cwd == 0) print_xy("Sun", 120, 75);
            if(cwd == 1) print_xy("Mon", 120, 75);
            if(cwd == 2) print_xy("Tue", 120, 75);
            if(cwd == 3) print_xy("Wed", 120, 75);
            if(cwd == 4) print_xy("Thu", 120, 75);
            if(cwd == 5) print_xy("Fri", 120, 75);
            if(cwd == 6) print_xy("Sat", 120, 75);
            setFont(FONT_SMALL);
            setFontColour(0, 128, 192);
            print_xy("hour  min  dow  day  mon SET  ", 32, 25);
            send_frame();
            wait_frame();
            current_time = esp_timer_get_time();
            if ((frame_time % 10) == 0) {
                printf("FPS:%f %d\n", 1.0e6 / (current_time - last_time),frame_time);
                vTaskDelay(1);
                //vTaskDelay(200/portTICK_RATE_MS);
                //2 200ms delay (usually 100Hz ticks and portTICK_RATE_MS=10)
            }
            last_time = current_time;
            int key=get_input();
            if(key==0) select_time=(select_time+1)%6;
            if(key==35) adj_time=select_time;
            if (adj_time==0){
                if (key==35){
                    (chr)++;
                    if (chr>23){
                        chr=0;
                    }
                }
            }
            if (adj_time==1){
                if (key==35){
                    (cmin)++;
                    if (cmin>59){
                        cmin=0;
                    }
                }
            }
            if (adj_time==2){
                if (key==35){
                    (cwd)++;
                    if (cwd>6){
                        cwd=0;
                    }
                }
            }
            if (adj_time==3){
                if (key==35){
                    (cday)++;
                    if (cday>31){
                        cday=1;
                    }
                }
            }
            if (adj_time==4){
                if (key==35){
                    (cmon)++;
                    if (cmon>11){
                        cmon=0;
                    }
                }
            }
            if(adj_time==5){
                time_changed=false;
                break;
            }
        }
    }
}