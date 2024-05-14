#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"
#include <string.h>

#define TEXT_CHAR_SIZE 1024 

#define MOUNT_POINT "/sdcard"
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5

/*
#define UART_TX_PIN 1
#define UART_RX_PIN 3*/

typedef enum edit_mode {
    command,
    chain,
    edit,
    open,
    destination,
    save
}eEditorMode_t;

eEditorMode_t mode = command;
static char str[100];

static const char *TAG = "sd_card";                      
static char text[TEXT_CHAR_SIZE] = {0};
//static int cursor_pos = 0;
static char nombre_archivo[TEXT_CHAR_SIZE] = "default.txt";

/*
static uint16_t new_line_pos[100];
static uint8_t new_line_index;*/

#define RIGHT 0
#define LEFT 1

static esp_err_t s_example_write_file(const char *path, const char *data);
static esp_err_t s_example_read_file(const char *path);
void UART_move_cursor(uint8_t direction, uint16_t *cursor_pos);
void UART_write_dest(char *file_to_save);
void UART_editor(void);
void UART_print_text(void);

static esp_err_t s_example_write_file(const char *path, const char *data) // Escribir
{
    ESP_LOGI(TAG, "Abriendo archivo %s", path);
    FILE *f = fopen(path, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Fallo abrir el archivo para escritura");
        return ESP_FAIL;
    }
    fprintf(f, "%s", data);
    fclose(f);
    ESP_LOGI(TAG, "Archivo escrito");
    return ESP_OK;
}

static esp_err_t s_example_read_file(const char *path) // leer
{
    ESP_LOGI(TAG, "Abriendo archivo %s", path);
    FILE *f = fopen(path, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Fallo abrir el archivo para lectura");
        return ESP_FAIL;
    }
    memset(text, 0, TEXT_CHAR_SIZE);       // Limpia el buffer de texto
    fread(text, 1, TEXT_CHAR_SIZE - 1, f); // Lee el contenido del archivo en el buffer de texto
    fclose(f);
    cursor_pos = strlen(text);
    ESP_LOGI(TAG, "Contenido del archivo: '%s'", text);
    return ESP_OK;
}

//print on screen data from the text buffer on RAM
void UART_print_text(void)
{
    for(int i=0; i < strlen(text); i++)
    {
        if(text[i] == 13)//if enter
        {
            SKIP_LINE(1, str)
        }
        else
            UART_transfer_char(UART_1, text[i]);
    }
}

void UART_move_cursor(uint8_t direction, uint16_t *cursor_pos)
{

    if(direction == RIGHT)
    {    
        (*cursor_pos)++;
        if(text[*cursor_pos] == '\0')//if null / end of text
            (*cursor_pos)--;
        else if(text[*cursor_pos] == 13)//check if enter
        {    MOVE_BEGINNING_DOWN(1, str) (*cursor_pos)++;} 
        else
        {    MOVE_RIGHT(1, str)}
                    
    }
    else if(direction == LEFT)
    {
        (*cursor_pos)--;
        if(*cursor_pos == 65535)//if past begining
            *cursor_pos=0;
        else if(text[*cursor_pos] == 13)//check if enter
        {   
            (*cursor_pos)--;
            for(int i=0; i < strlen(text); i++)//check for next start of line
            {
                if(text[*cursor_pos-i] == 13 || *cursor_pos-i <= 0)
                {
                    MOVE_BEGINNING_UP(1, str)
                    MOVE_RIGHT(i, str)
                    break;
                }
            }
        }
        else
        {    MOVE_LEFT(1, str)} 
    }
}

void UART_write_dest(char *file_to_save)
{
    SAVE_POS(str)
    MOVE_TO_POS(1, 40, str)
    COLOR_GREEN(str)
    TRANSFER_STRING("Destino: ", str)
    UART_transfer(UART_1,file_to_save, 0);
    COLOR_DEFAULT(str)
    RESTORE_POS(str)
}

void UART_editor(void)
{
    static uint8_t cmmd_f = 0, chn_f = 0, edt_f = 0, open_f = 0, dest_f = 0, save_f = 0;
    static uint16_t cursor_pos = 0;
    static char temp[40], file_to_save[40] = "default.txt";

    switch(mode)
    {
        case command:
            if(!cmmd_f) //if text not in screen
            {
                CLEAR_LINE(str)
                TRANSFER_STRING("Modo comando-> ", str)
                activateInput(1);
                cmmd_f = 1;
            }
            if(u1_rx_buff_data_index == 2)//if command length
            {
                if(strcmp(u1_rx_buff_data, ":u") == 0)
                    mode = chain;
                else if(strcmp(u1_rx_buff_data, ":e") == 0)
                    mode = edit;
                else if(strcmp(u1_rx_buff_data, ":o") == 0)
                    mode = open;
                else if(strcmp(u1_rx_buff_data, ":n") == 0)
                    mode = destination;
                else if(strcmp(u1_rx_buff_data, ":s") == 0)
                    mode = save;
                else //comando invalido
                {
                    SAVE_POS(str)
                    SKIP_LINE(2, str)
                    COLOR_RED(str)
                    TRANSFER_STRING("Comando invalido: ", str)
                    UART_transfer(UART_1,u1_rx_buff_data, 0);
                    COLOR_DEFAULT(str)
                    RESTORE_POS(str)
                    cmmd_f = 0;
                    break;
                }
                NO_INPUT_PROCESS
            }
            else if(u1_rx_buff_data_index > 2)//si se sobrepasa la longitud
                cmmd_f = 0;
            break;
        case chain:
            if(!chn_f) //if text not in screen
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                TRANSFER_STRING("Modo recepcion de cadena:", str)
                UART_write_dest(file_to_save);
                SKIP_LINE(1, str)
                if(strlen(text) > 0) //print text on RAM
                    UART_print_text();
                activateInput(1);
                chn_f = 1;
            }
            if(esc_f)
            {
                if(u1_rx_buff_data_index > 0)//save text on uart buffer
                    strcat(text, u1_rx_buff_data);
                SKIP_LINE(2, str)
                mode = command;
                chn_f = 0;
                cmmd_f = 0;
            }
            break;
        case edit:
            if(!edt_f) //if text not in screen
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                TRANSFER_STRING("Modo edition de texto:", str)
                UART_write_dest(file_to_save);
                SKIP_LINE(1, str)
                SAVE_POS(str)
                if(strlen(text) > 0) //print text on RAM
                    UART_print_text();
                RESTORE_POS(str)
                edt_f = 1;
                cursor_pos = 0;
                activateInput(0);
                any_char_f=1;
            }
            if(u1_rx_buff_data_index && strlen(text) > 0)//if key pressed and there is text in RAM
            {
                if(u1_rx_buff_data[0] == 0x1b && u1_rx_buff_data[1] == 0x5b)//if arrow key
                {
                    if(u1_rx_buff_data[2] == 0x43)//if right
                    {    
                        UART_move_cursor(RIGHT,&cursor_pos);
                    }
                    else if(u1_rx_buff_data[2] == 0x44)//if left
                    {    
                        UART_move_cursor(LEFT,&cursor_pos); 
                    }
                    
                }
                else if(u1_rx_buff_data[0] == 8)//if backspace
                {
                    for(int i=cursor_pos; i < (strlen(text) - 1); i++)//recorrer todos caracteres
                    {
                        text[i] = text[i+1];
                    }
                    text[strlen(text) - 1] = '\0';//borrar ultimo caracter
                    UART_move_cursor(LEFT,&cursor_pos);//mover cursor
                    MOVE_RIGHT(1, str)//mueve uno demas

                    SAVE_POS(str)//escribir todo de nuevo
                    CLEAR_SCREEN(str)
                    HOME_POS(str)
                    TRANSFER_STRING("Modo edition de texto:", str)
                    //UART_write_dest(file_to_save);
                    SKIP_LINE(1, str)
                    UART_print_text();
                    RESTORE_POS(str)
                    MOVE_LEFT(1, str)
                }
                clear_buffer(u1_rx_buff_data, &u1_rx_buff_data_index);
            }
            if(esc_f)
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                mode = command;
                edt_f = 0;
                cmmd_f = 0;
            }
            break;
        case open:
            if(!open_f) //if text not in screen
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                TRANSFER_STRING("Ingresa archivo a abrir y presiona enter: ", str)
                activateInput(1);
                open_f = 1;
            }
            if(enter_f)
            {
                u1_rx_buff_data_index--;
                u1_rx_buff_data[u1_rx_buff_data_index] = '\0';//delete enter

                strcpy(temp, u1_rx_buff_data);

                //abrir archivo
                /*
                char file_path[TEXT_CHAR_SIZE];
                strncpy(file_path, MOUNT_POINT, sizeof(file_path) - 1);
                strncat(file_path, "/", sizeof(file_path) - strlen(file_path) - 1);
                strncat(file_path, nombre_archivo, sizeof(file_path) - strlen(file_path) - 1);
                s_example_read_file(file_path);*/

                //ESP_LOGI(TAG, "Data received %s",temp);
                //enter_f = 0;
                esc_f=1;
            }
            if(esc_f)
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                mode = command;//edit
                open_f = 0;
                cmmd_f = 0;
            }
            break;
        case destination:
            if(!dest_f) //if text not in screen
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                TRANSFER_STRING("Ingresa destino y presiona enter: ", str)
                activateInput(1);
                dest_f = 1;
            }
            if(enter_f)
            {
                u1_rx_buff_data_index--;
                u1_rx_buff_data[u1_rx_buff_data_index] = '\0';//delete enter

                strcpy(file_to_save, u1_rx_buff_data);
                esc_f = 1;
            }
            if(esc_f)
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                mode = command;
                dest_f = 0;
                cmmd_f = 0;
            }
            break;
        case save:
            if(!save_f)
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                TRANSFER_STRING("Guradando archivo...", str)
                UART_write_dest(file_to_save);
                SKIP_LINE(1, str)
                save_f = 1;
                /*
                char file_path[EXAMPLE_MAX_CHAR_SIZE]; // Buffer para almacenar la ruta completa del archivo

                strncpy(file_path, MOUNT_POINT, sizeof(file_path) - 1);
                strncat(file_path, "/", sizeof(file_path) - strlen(file_path) - 1);
                strncat(file_path, file_to_save, sizeof(file_path) - strlen(file_path) - 1);
                
                s_example_write_file(file_path, text); // Llama a la funcion para escribir el nombre del archivo
                */
                TRANSFER_STRING("Archivo guardado, presiona esc.", str)
                activateInput(1);
            }
            if(esc_f)
            {
                CLEAR_SCREEN(str)
                HOME_POS(str)
                mode = command;
                save_f = 0;
                cmmd_f = 0;
            }
            break;
    }
}

void app_main(void)
{
    init_UART();
    create_uart_tasks();
    
    esp_err_t ret;
    sdmmc_card_t *card;

    /*
    ESP_LOGI(TAG, "Inicializando SD card");
    ESP_LOGI(TAG, "Usando periférico SPI");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 5000;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo al inicializar el bus.");
        return;
    }
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;
    ESP_LOGI(TAG, "Montando el filesystem");
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &(esp_vfs_fat_sdmmc_mount_config_t){
                                                                        .format_if_mount_failed = true,
                                                                        .max_files = 5,
                                                                        .allocation_unit_size = 16 * 1024
                                                                    },
                                  &card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo al montar el filesystem");
        return;
    }
    ESP_LOGI(TAG, "Montando el filesystem");

    // UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    */

    
    char cmd = 0; // variable para almacenar el comando recibido por UART

    CLEAR_SCREEN(str);
    HOME_POS(str)
    while (1)
    {
        UART_editor();
        vTaskDelay(pdMS_TO_TICKS(10));

        /*if (uart_read_bytes(UART_NUM_0, &cmd, 1, 1000 / portTICK_PERIOD_MS) > 0) // espera y lee un byte del UART0 con un tiempo de espera de 1 segundo
        {
            switch (cmd) // comando recibido
            {
            case 0x1B: // ESC
                ESP_LOGI(TAG, "Modo comando");
                while (1)
                {
                    if (uart_read_bytes(UART_NUM_0, &cmd, 1, 1000 / portTICK_PERIOD_MS) > 0) // Espera y lee un byte del UART0 con un tiempo de espera de 1 segundo
                    {
                        switch (cmd)
                        {
                        case 'u':  // Comando 'u' para recibir texto por UART
                            ESP_LOGI(TAG, "Recibiendo texto");
                            memset(text, 0, EXAMPLE_MAX_CHAR_SIZE); // Limpia el buffer de texto
                            cursor_pos = 0; // posicion del cursor
                            while (1)
                            {
                                if (uart_read_bytes(UART_NUM_0, (uint8_t *)text + cursor_pos, 1, 1000 / portTICK_PERIOD_MS) > 0)
                                {
                                    if (text[cursor_pos] == 0x1B) // Verifica si recibio la tecla ESC
                                    {
                                        text[cursor_pos] = '\0'; // Agrega el caracter nulo al final del texto
                                        uart_write_bytes(UART_NUM_0, "\r\n", 2); // nueva linea
                                        ESP_LOGI(TAG, "Texto recibido: '%s'", text);
                                        break;
                                    }
                                    cursor_pos++; // se incrementa posicion del cursor
                                    uart_write_bytes(UART_NUM_0, text + cursor_pos - 1, 1); // envia el ultimo caracter recibido de vuelta al UART0
                                }
                            }
                            break;
                        case 'o':   // Comando 'o' para abrir un archivo
                            ESP_LOGI(TAG, "Abra un archivo:");
                            memset(nombre_archivo, 0, EXAMPLE_MAX_CHAR_SIZE);
                            cursor_pos = 0;
                            while (1)
                            {
                                if (uart_read_bytes(UART_NUM_0, (uint8_t *)nombre_archivo + cursor_pos, 1, 1000 / portTICK_PERIOD_MS) > 0)
                                {
                                    if (nombre_archivo[cursor_pos] == 0x1B)//enter
                                    {
                                        nombre_archivo[cursor_pos] = '\0';
                                        uart_write_bytes(UART_NUM_0, "\r\n", 2);
                                        ESP_LOGI(TAG, "Abriendo archivo: %s", nombre_archivo);
                                        char file_path[EXAMPLE_MAX_CHAR_SIZE];
                                        strncpy(file_path, MOUNT_POINT, sizeof(file_path) - 1);
                                        strncat(file_path, "/", sizeof(file_path) - strlen(file_path) - 1);
                                        strncat(file_path, nombre_archivo, sizeof(file_path) - strlen(file_path) - 1);
                                        s_example_read_file(file_path);
                                        break;
                                    }
                                    cursor_pos++;
                                    uart_write_bytes(UART_NUM_0, nombre_archivo + cursor_pos - 1, 1); // envia el ultimo caracter recibido de vuelta al UART0
                                }
                            }
                            break;
                        case 'e':   // Comando 'e' para entrar en el modo de edicin
                            ESP_LOGI(TAG, "Entrando al modo edicion.");
                            while (1)
                            {
                                uint8_t input_buffer[32];
                                int num_bytes = uart_read_bytes(UART_NUM_0, input_buffer, sizeof(input_buffer), 100 / portTICK_PERIOD_MS);
                                if (num_bytes > 0) // Verifica si se recibieron bytes
                                {
                                    for (int i = 0; i < num_bytes; i++)
                                    {
                                        uint8_t c = input_buffer[i]; // Obtiene el caracter actual
                                        if (c == 0x1B) // verifica si recibio ESC
                                        {
                                            if (i + 2 < num_bytes && input_buffer[i + 1] == 0x5B) // verifica si recibio '[' 
                                            {
                                                switch (input_buffer[i + 2])
                                                {
                                                case 0x44: // Flecha izquierda
                                                    if (cursor_pos > 0) // si el cursor no esta al inicio del texto
                                                    {
                                                        cursor_pos--; // mueve el cursor hacia la izquierda
                                                    }
                                                    i += 2; // salta los caracteres de la secuencia de la flecha
                                                    break;
                                                case 0x43: // Flecha derecha
                                                    if (cursor_pos < strlen(text)) // verifica si el cursor no esta al final del texto
                                                    {
                                                        cursor_pos++; // mueve el cursor hacia la derecha
                                                    }
                                                    i += 2; // Salta los caracteres de la secuencia de la flecha
                                                    break;
                                                default:
                                                    uart_write_bytes(UART_NUM_0, "\r\n", 2); // nueva linea
                                                    ESP_LOGI(TAG, "Saliendo del modo edicion...");
                                                    goto salir_edicion;                // Salta a la etiqueta
                                                }
                                            }
                                            else
                                            {
                                                uart_write_bytes(UART_NUM_0, "\r\n", 2); // nueva linea
                                                ESP_LOGI(TAG, "Saliendo del modo edicion...");
                                                goto salir_edicion;
                                            }
                                        }
                                        else if (c == 0x08) // Verifica si recibe Backspace
                                        {
                                            if (cursor_pos > 0) // Si el cursor no esta al inicio del texto
                                            {
                                                memmove(text + cursor_pos - 1, text + cursor_pos, strlen(text + cursor_pos) + 1); // Mueve los caracteres a la izquierda del cursor hacia la izquierda
                                                cursor_pos--;                                                                     // Mueve el cursor hacia la izquierda
                                            }
                                        }
                                        else
                                        {
                                            if (cursor_pos < EXAMPLE_MAX_CHAR_SIZE - 1) // Si el cursor no esta al final del buffer de texto
                                            {
                                                memmove(text + cursor_pos + 1, text + cursor_pos, strlen(text + cursor_pos) + 1); // Mueve los caracteres a la derecha del cursor hacia la derecha
                                                text[cursor_pos] = c;                                                             // Inserta el caracter en la posicion del cursor
                                                cursor_pos++;                                                                     // Mueve el cursor hacia la derecha
                                            }
                                        }
                                    }
                                    uart_write_bytes(UART_NUM_0, "\r\x1B[0K", 5);
                                    uart_write_bytes(UART_NUM_0, text, strlen(text));
                                    if (cursor_pos < strlen(text))
                                    {
                                        char cursor_move[16];
                                        snprintf(cursor_move, sizeof(cursor_move), "\x1B[%dD", (int)strlen(text) - cursor_pos);
                                        uart_write_bytes(UART_NUM_0, cursor_move, strlen(cursor_move));
                                    }
                                }
                            }
                        salir_edicion:
                            break;
                        case 'n':  // Comando 'n' para ingresar el nombre del archivo
                            uart_write_bytes(UART_NUM_0, "\r\n", 2); // nueva linea
                            ESP_LOGI(TAG, "Ingresa el nombre del archivo");
                            memset(nombre_archivo, 0, EXAMPLE_MAX_CHAR_SIZE); // Limpia el bufer de nombre de archivo
                            cursor_pos = 0;
                            while (1)
                            {
                                if (uart_read_bytes(UART_NUM_0, (uint8_t *)nombre_archivo + cursor_pos, 1, 1000 / portTICK_PERIOD_MS) > 0)
                                {
                                    if (nombre_archivo[cursor_pos] == 0x1B) // Si recibio la tecla ESC
                                    {
                                        nombre_archivo[cursor_pos] = '\0';
                                        uart_write_bytes(UART_NUM_0, "\r\n", 2); // nueva linea
                                        ESP_LOGI(TAG, "Nombre del archivo: %s", nombre_archivo);
                                        break;
                                    }
                                    cursor_pos++;
                                    uart_write_bytes(UART_NUM_0, nombre_archivo + cursor_pos - 1, 1);
                                }
                            }
                            break;
                        case 's':   // Comando 's' para guardar el archivo
                            ESP_LOGI(TAG, "Guardando el archivo");
                            char file_path[EXAMPLE_MAX_CHAR_SIZE]; // Buffer para almacenar la ruta completa del archivo
                            if (strlen(nombre_archivo) == 0) // si no se ingresa nombre del archivo crea 'default.txt' por defecto
                            {
                                strncpy(file_path, MOUNT_POINT, sizeof(file_path) - 1);
                                strncat(file_path, "/", sizeof(file_path) - strlen(file_path) - 1);
                                strncat(file_path, "default.txt", sizeof(file_path) - strlen(file_path) - 1);
                            }
                            else
                            {
                                strncpy(file_path, MOUNT_POINT, sizeof(file_path) - 1);
                                strncat(file_path, "/", sizeof(file_path) - strlen(file_path) - 1);
                                strncat(file_path, nombre_archivo, sizeof(file_path) - strlen(file_path) - 1);
                            }
                            uart_write_bytes(UART_NUM_0, "\r\n", 2); // nueva linea
                            s_example_write_file(file_path, text); // Llama a la funcion para escribir el nombre del archivo
                            break;
                        default:
                            ESP_LOGI(TAG, "Comando desconocido.");
                            break;
                        }
                    }
                }
                break;
            default:
                ESP_LOGI(TAG, "Entrada desconocida.");
                break;
            }
        }*/
    }
}