#!/bin/bash
set -e
cd "$(dirname "$0")"
arm-none-eabi-gcc -mcpu=arm7tdmi -marm -O2 -ffreestanding -nostdlib -Wall -T gba.ld -o grimlight.elf src/crt0.s src/main.c -lgcc
arm-none-eabi-objcopy -O binary grimlight.elf grimlight.gba
python3 fix_header.py grimlight.gba
mkdir -p releases
cp grimlight.gba releases/grimlight-v13.gba
ls -l releases/grimlight-v13.gba
