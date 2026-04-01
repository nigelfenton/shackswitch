#!/bin/bash
echo "Restarting arduino-router first..."
systemctl restart arduino-router

echo "Waiting for router socket and STM32 reset..."
until [ -S /var/run/arduino-router.sock ]; do
  sleep 1
  done
  sleep 5

  echo "Now loading sketch into STM32 RAM..."
  /opt/openocd/bin/openocd \
    -s /opt/openocd \
      -f /opt/openocd/openocd_gpiod.cfg \
        -c "set filename /home/arduino/shackswitch-flash/sketch.ino.bin-zsk.bin" \
          -c "source /home/arduino/shackswitch-flash/flash_sketch_ram.cfg"

          echo "Waiting 15s for Bridge registration..."
          sleep 15

          echo "Starting container..."
          docker compose -f /home/arduino/ArduinoApps/first-app/.cache/app-compose.yaml up
