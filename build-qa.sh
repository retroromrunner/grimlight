#!/bin/bash
set -e
cd "$(dirname "$0")"
arm-none-eabi-gcc -mcpu=arm7tdmi -marm -O2 -ffreestanding -nostdlib -Wall -DQA_BUILD -T gba.ld -o grimlight-qa.elf src/crt0.s src/main.c -lgcc
arm-none-eabi-objcopy -O binary grimlight-qa.elf grimlight-qa.gba
python3 fix_header.py grimlight-qa.gba
ls -l grimlight-qa.gba
