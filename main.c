#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hw_config.h"
#include "f_util.h"
#include "ff.h"

#define LED_PIN 25
#define I2C_PORT i2c1
#define I2C_SDA 6
#define I2C_SCL 7
#define DS3231_ADDR 0x68
#define SHT3x_ADDR 0x44
#define BUFFER_SIZE 12  // 12 samples x 10 seconds = 120 seconds (2 minutes)

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day_of_week;
    uint8_t date;
    uint8_t month;
    uint8_t year;
} ds3231_time_t;

typedef struct {
    float temperature;
    float humidity;
    bool valid;
} sht3x_reading_t;

typedef struct {
    uint32_t epoch;
    float temperature;
    float humidity;
} data_point_t;

static ds3231_time_t current_time;
static data_point_t data_buffer[BUFFER_SIZE];
static int buffer_index = 0;

void blink_led(int times) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    for (int i = 0; i < times; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(200);
        gpio_put(LED_PIN, 0);
        sleep_ms(200);
    }
}

void blink_brief(int duration_ms) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
    sleep_ms(duration_ms);
    gpio_put(LED_PIN, 0);
}

void blink_double() {
    blink_brief(5);
    sleep_ms(50);
    blink_brief(5);
}

uint8_t bcd_to_decimal(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void read_ds3231_time(ds3231_time_t *time) {
    uint8_t buffer[7];
    uint8_t reg = 0x00;
    
    i2c_write_blocking(I2C_PORT, DS3231_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, DS3231_ADDR, buffer, 7, false);
    
    time->seconds = bcd_to_decimal(buffer[0]);
    time->minutes = bcd_to_decimal(buffer[1]);
    time->hours = bcd_to_decimal(buffer[2] & 0x3F);
    time->day_of_week = buffer[3];
    time->date = bcd_to_decimal(buffer[4]);
    time->month = bcd_to_decimal(buffer[5] & 0x1F);
    time->year = bcd_to_decimal(buffer[6]);
}

// Convert DS3231 UTC time to Unix epoch (seconds since 1970-01-01)
static uint32_t ds3231_to_epoch(ds3231_time_t *time) {
    uint32_t days = 0;
    
    // Years from 2000 to current year
    for (int y = 0; y < time->year; y++) {
        days += (y % 4 == 0) ? 366 : 365;
    }
    
    // Days in months
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (time->year % 4 == 0) days_in_month[2] = 29;
    
    for (int m = 1; m < time->month; m++) {
        days += days_in_month[m];
    }
    
    days += time->date - 1;
    
    // Convert to Unix epoch (1970 to 2000 is 10957 days)
    uint32_t epoch = (days + 10957) * 86400;
    epoch += time->hours * 3600;
    epoch += time->minutes * 60;
    epoch += time->seconds;
    
    return epoch;
}

sht3x_reading_t read_sht3x(void) {
    sht3x_reading_t result = {0, 0, false};
    uint8_t cmd[2] = {0x2C, 0x06};
    uint8_t data[6];
    
    if (i2c_write_blocking(I2C_PORT, SHT3x_ADDR, cmd, 2, false) == PICO_ERROR_GENERIC) {
        return result;
    }
    
    sleep_ms(15);
    
    if (i2c_read_blocking(I2C_PORT, SHT3x_ADDR, data, 6, false) == PICO_ERROR_GENERIC) {
        return result;
    }
    
    uint16_t temp_raw = ((uint16_t)data[0] << 8) | data[1];
    result.temperature = -45.0f + 175.0f * (temp_raw / 65535.0f);
    
    uint16_t humidity_raw = ((uint16_t)data[3] << 8) | data[4];
    result.humidity = 100.0f * (humidity_raw / 65535.0f);
    
    result.valid = true;
    return result;
}

void list_directory(const char *path, int indent) {
    FRESULT fr;
    DIR dir;
    FILINFO fno;
    
    fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        printf("Failed to open directory: %s\n", path);
        return;
    }
    
    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        
        for (int i = 0; i < indent; i++) printf("  ");
        
        if (fno.fattrib & AM_DIR) {
            printf("[DIR]  %s\n", fno.fname);
            char subpath[256];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, fno.fname);
            list_directory(subpath, indent + 1);
        } else {
            printf("[FILE] %s (%lu bytes)\n", fno.fname, fno.fsize);
        }
    }
    
    f_closedir(&dir);
}

int main() {
    blink_led(3);
    
    stdio_init_all();
    sleep_ms(2000);
    
    // Initialize I2C
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    printf("Initializing SD card...\n");
    
    FATFS fs;
    FRESULT fr = f_mount(&fs, "", 1);
    if (FR_OK != fr) {
        panic("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    
    blink_led(2);
    
    printf("SD card mounted successfully\n");
    
    // Get free space
    DWORD free_clusters;
    FATFS *pfs;
    fr = f_getfree("", &free_clusters, &pfs);
    if (FR_OK == fr) {
        uint64_t free_bytes = (uint64_t)free_clusters * pfs->csize * 512;
        printf("Free space: %llu bytes (%.2f MB)\n", free_bytes, free_bytes / 1024.0 / 1024.0);
    }
    
    printf("Directory listing:\n");
    printf("==================\n");
    
    list_directory("", 0);
    
    printf("==================\n");
    
    // Read RTC and open/create CSV file for appending
    read_ds3231_time(&current_time);
    
    char filename[32];
    snprintf(filename, sizeof(filename), "%02d%02d%02d%02d.csv",
             current_time.month, current_time.date, current_time.hours, current_time.minutes);
    
    FIL fil;
    fr = f_open(&fil, filename, FA_OPEN_APPEND | FA_WRITE);
    if (FR_OK != fr) {
        printf("Error opening file: %s (%d)\n", filename, fr);
    } else {
        // Check if file is empty (new file)
        if (f_size(&fil) == 0) {
            f_printf(&fil, "epoch,degC,RH\n");
            f_printf(&fil, "# START: 20%02d-%02d-%02d %02d:%02d:%02d\n",
                     current_time.year, current_time.month, current_time.date,
                     current_time.hours, current_time.minutes, current_time.seconds);
        }
        printf("File: %s (appending)\n", filename);
    }
    
    printf("Reading data every 10 seconds, writing to SD every 2 minutes...\n");
    
    uint8_t last_seconds = 0xFF;
    uint32_t last_write_time = 0;
    uint32_t epoch = 0;
    
    while (1) {
        read_ds3231_time(&current_time);
        
        // Double blink and collect data at top of minute
        if ((current_time.seconds == 0) && (current_time.seconds != last_seconds)) {
            blink_double();
            sht3x_reading_t sht = read_sht3x();
            
            epoch = ds3231_to_epoch(&current_time);
            printf("UTC: 20%02d-%02d-%02d %02d:%02d:%02d | Epoch: %lu | T=%.2f°C RH=%.2f%%\n",
                   current_time.year, current_time.month, current_time.date,
                   current_time.hours, current_time.minutes, current_time.seconds,
                   epoch, sht.temperature, sht.humidity);
            
            // Add to buffer
            if (buffer_index < BUFFER_SIZE) {
                data_buffer[buffer_index].epoch = epoch;
                data_buffer[buffer_index].temperature = sht.temperature;
                data_buffer[buffer_index].humidity = sht.humidity;
                buffer_index++;
            }
        }
        // Single blink and collect data at even multiples of 10 seconds
        else if ((current_time.seconds % 10 == 0) && (current_time.seconds != last_seconds)) {
            blink_brief(5);
            sht3x_reading_t sht = read_sht3x();
            
            epoch = ds3231_to_epoch(&current_time);
            printf("UTC: 20%02d-%02d-%02d %02d:%02d:%02d | Epoch: %lu | T=%.2f°C RH=%.2f%%\n",
                   current_time.year, current_time.month, current_time.date,
                   current_time.hours, current_time.minutes, current_time.seconds,
                   epoch, sht.temperature, sht.humidity);
            
            // Add to buffer
            if (buffer_index < BUFFER_SIZE) {
                data_buffer[buffer_index].epoch = epoch;
                data_buffer[buffer_index].temperature = sht.temperature;
                data_buffer[buffer_index].humidity = sht.humidity;
                buffer_index++;
            }
        }
        
        // Write buffer to SD card every 120 seconds (2 minutes)
        if (buffer_index > 0 && (epoch - last_write_time) >= 120) {
            if (FR_OK == fr) {
                for (int i = 0; i < buffer_index; i++) {
                    f_printf(&fil, "%lu,%.2f,%.2f\n",
                             data_buffer[i].epoch,
                             data_buffer[i].temperature,
                             data_buffer[i].humidity);
                }
                f_sync(&fil);
                printf("Wrote %d samples to SD card\n", buffer_index);
            }
            buffer_index = 0;
            last_write_time = epoch;
        }
        
        last_seconds = current_time.seconds;
        sleep_ms(500);
    }
    
    return 0;
}