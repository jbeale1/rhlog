This simple datalogger code for Pi Pico board uses Sensiron SHT3x sensor and DS3231 real-time clock set to UTC time 
so the FAT32 file can have the correct created-on time. 
The SD card slot is connected to Pico pins GP16-GP19.

example output file "09230624.csv"
```
epoch,degC,RH
# START: 2026-09-23 06:24:31
1790144680,23.46,68.68
1790144690,23.45,68.46
1790144700,23.46,67.59
1790144710,23.48,69.83
1790144720,23.49,70.13
1790144730,23.50,68.02
1790144740,23.49,67.16
1790144750,23.49,66.90
1790144760,23.52,67.05
1790144770,23.52,67.23
1790144780,23.52,66.80
1790144790,23.53,66.74
1790144800,23.54,66.84
```
