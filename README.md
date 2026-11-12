****** STM32F4X Discovery Board Generate Sound wave ******
This project was create to generate a random sound wave at CN4 speaker port
U4 - STM32F407VGT6
U7 - CS43L22 (I2C address: 0x94)
CN4 - Speaker port

Operation:
1 - Set up resisters of U7 - CS43L22: U4 - STM32F407VGT6 --> I2C --> U7 - CS43L22
2 - U4 (STM32F407VGT6) generate sound wave at 48 sample rate and 1 kHz tone
3 - Stream sound wave from U4 --> U7: U4 - STM32F407VGT6 --> I2S3 --> U7 - CS43L22
4 - Duplicate left and right channel
5 - Output sound at speaker CN4
