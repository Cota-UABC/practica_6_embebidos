#include "uart.h"

const uart_port_t UART_1 = UART_NUM_0, UART_2 = UART_NUM_2;
QueueHandle_t uart_queue;
const char *tag_u = "UART", *sync_code = SYNC_CODE;
volatile uint8_t u1_rx_buff_data[BUFFER_SIZE], u2_rx_buff_data[BUFFER_SIZE];
volatile int u1_rx_buff_data_index = 0, u2_rx_buff_data_index = 0;
uint8_t rx1_f=0, enter_f=0, esc_f=0, echo_f=0, sync_f=0, any_char_f=0;
int cursor_pos = 0;
static char str[STR_SIZE];

void init_UART(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_1, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_1, RING_BUFFER_SIZE, RING_BUFFER_SIZE, 10, &uart_queue, 0));

    /*
    ESP_ERROR_CHECK(uart_param_config(UART_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_2, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_2, RING_BUFFER_SIZE, RING_BUFFER_SIZE, 10, &uart_queue, 0));
    */

    ESP_LOGI(tag_u,"init uart completed");
}

void UART_transfer(const uart_port_t uart_n,char* str, int len)
{
    if(len == 0)
        uart_write_bytes(uart_n, (const char*)str, strlen(str));
    else if(len > 0)
        uart_write_bytes(uart_n, (const char*)str, len);
}

void UART_transfer_char(const uart_port_t uart_n,char ch)
{
    uart_write_bytes(uart_n, &ch, 1);
}

//not used 
void UART_receive(uint8_t* data_rx)
{
    int length = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_1, (size_t*)&length));
    uart_read_bytes(UART_1, data_rx, length, 100);
}

esp_err_t create_uart_tasks(void)
{
    xTaskCreate(
        uart1_rx_task,
        "uart1_rx_task",
        TASK_MEMORY,
        NULL,
        1,
        NULL);
    ESP_LOGI(tag_u,"UART 1 rx task created");

    /*xTaskCreate(
        uart2_rx_task,
        "uart2_rx_task",
        TASK_MEMORY,
        NULL,
        1,
        NULL);
    ESP_LOGI(tag_u,"UART 2 rx task created");*/

    return ESP_OK;
}

void uart1_rx_task(void *pvParameters)
{
    uint8_t *ring_data = (uint8_t *) malloc(RING_BUFFER_SIZE);
    
    while(true)
    {
        bzero(ring_data,RING_BUFFER_SIZE);

        int len = uart_read_bytes(UART_1,ring_data,RING_BUFFER_SIZE,pdMS_TO_TICKS(100));

        //if length 0, rx buffer process 0, or esc flag True, no proccess
        if(len == 0 || esc_f == 1 || rx1_f == 0)
            continue;

        //ESP_LOGI(tag_u, "Data received %s",ring_data);

        //take data from ring buffer and put it in the data buffer
        for(int i=0;i<len;i++)
        {
            if(any_char_f)
            {
                u1_rx_buff_data[u1_rx_buff_data_index] = ring_data[i];
                u1_rx_buff_data_index++;

                if(ring_data[i] == 27 && ring_data[i+1] == '\0')//if esc
                {    
                    esc_f=1;
                    u1_rx_buff_data_index--; //ignore esc
                    u1_rx_buff_data[u1_rx_buff_data_index] = '\0';
                }
            }
            else
            //only accepts characters, numbers, space, enter, esc, colon, period and backspace
            if( (ring_data[i] >= 65 && ring_data[i] <= 90) || 
                (ring_data[i] >= 97 && ring_data[i] <= 122) ||  
                (ring_data[i] >= 48 && ring_data[i] <= 57) ||  
                ring_data[i]==13 || ring_data[i]==8 || ring_data[i]==32 
                || ring_data[i]==27 || ring_data[i]==58 || ring_data[i]==46)
            {
                u1_rx_buff_data[u1_rx_buff_data_index] = ring_data[i];
                u1_rx_buff_data_index++;

                //error max buffer size
                if(u1_rx_buff_data_index >= BUFFER_SIZE)
                {
                    ESP_LOGE(tag_u, "MAX BUFFER SIZE. CLEARING UART 1 BUFFER");
                    bzero((uint8_t *)u1_rx_buff_data,BUFFER_SIZE);
                    u1_rx_buff_data_index=0;
                    break;
                }

                //if backspace
                if(ring_data[i] == 8)
                {
                    u1_rx_buff_data_index--; //ignore backspace
                    u1_rx_buff_data[u1_rx_buff_data_index] = '\0';
                    if(u1_rx_buff_data_index != 0) //avoids pointing to negative
                    {
                        u1_rx_buff_data_index--; //ignore last char
                        u1_rx_buff_data[u1_rx_buff_data_index] = '\0';
                    }
                }
                    
                //if enter
                if(ring_data[i] == 13) 
                    enter_f=1;
                
                //if esc
                if(ring_data[i] == 27) 
                {    
                    esc_f=1;
                    u1_rx_buff_data_index--; //ignore esc
                    u1_rx_buff_data[u1_rx_buff_data_index] = '\0';
                }

                //echo back received ring_data
                if(echo_f)
                {
                    //if backspace
                    if(ring_data[i] == 8 && cursor_pos > 0)
                    {
                        TRANSFER_STRING("\b \b",str);
                        cursor_pos--;
                    }
                    //if enter
                    else if(ring_data[i] == 13)
                    {
                        SKIP_LINE(1, str)
                    }
                    else if(!(ring_data[i]==13 || ring_data[i] == 8 || ring_data[i] == 27))
                    {
                        UART_transfer_char(UART_1,ring_data[i]);
                        cursor_pos++;
                    }
                }
            }
        }
    }

    free(ring_data);//doesn´t get here
}

//not used
void uart2_rx_task(void *pvParameters)
{
    uint8_t *ring_data = (uint8_t *) malloc(RING_BUFFER_SIZE);
    
    while(true)
    {
        bzero(ring_data,RING_BUFFER_SIZE);

        int len = uart_read_bytes(UART_2,ring_data,RING_BUFFER_SIZE,pdMS_TO_TICKS(100));
        if(len == 0)
            continue;
    
        //ESP_LOGI(tag_u, "Data received in UART 2: %s",ring_data);

        //take data from ring buffer and put it in the data buffer
        for(int i=0;i<len;i++)
        {
            u2_rx_buff_data[u2_rx_buff_data_index] = ring_data[i];
            u2_rx_buff_data_index++;

            //error max buffer size
            if(u2_rx_buff_data_index >= BUFFER_SIZE)
            {
                ESP_LOGE(tag_u, "MAX BUFFER SIZE. CLEARING UART 2 BUFFER");
                bzero((uint8_t *)u2_rx_buff_data,BUFFER_SIZE);
                u2_rx_buff_data_index=0;
                break;
            }
        }

        //check sync code
        if(u2_rx_buff_data[u2_rx_buff_data_index-1] == sync_code[strlen(sync_code)-1])
        {
            char temp[sizeof(sync_code)];
            for(int i=strlen(sync_code);i>0;i--)
            {
                temp[strlen(sync_code)-i] = u2_rx_buff_data[u2_rx_buff_data_index-i];
            }
            temp[strlen(sync_code)] = '\0';//null terminator

            if(strcmp(temp,sync_code) == 0)
            {
                SAVE_POS(str)
                COLOR_GREEN(str)
                TRANSFER_STRING("\033[2ESYNC CODE RECEIVED. UART 2 BUFFER CLEARED.", str)
                RESTORE_POS(str)
                COLOR_DEFAULT(str)
                //ESP_LOGI(tag_u, "Sync code received. Buffer cleared.");
                //clear buffer
                u2_rx_buff_data_index=0;
                bzero((uint8_t *)u2_rx_buff_data,BUFFER_SIZE);
                sync_f++;
            }
            else 
                if(!sync_f)
                    ESP_LOGE(tag_u, "Sync code failed. TEMP: %s  CODE: %s",temp,sync_code);   
        }
    }
    
    free(ring_data);//doesn´t get here
}

void clear_buffer(volatile uint8_t *buffer, volatile int *index)
{
    *index = 0;//reset index
    bzero((uint8_t *)buffer,BUFFER_SIZE);
}

void activateInput(uint8_t echo)
{
    if(echo)
        echo_f = 1;
    else 
        echo_f = 0;
    rx1_f = 1; //activate rx buffer process
    cursor_pos = 0;//reset pos
    enter_f = 0;//reset if enter key was pressed
    esc_f = 0; //reset if esc key was pressed
    any_char_f=0; //not any char
    clear_buffer(u1_rx_buff_data, &u1_rx_buff_data_index);//clear rx1 array
    //SKIP_LINE(3,str) //skip line
}