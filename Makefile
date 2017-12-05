ESPTOOL = esptool
ESP_UPLOAD_PORT = /dev/ttyUSB0
ESP_UPLOAD_SPEED = 115200

ESP_UPLOAD = $(ESPTOOL) --baud=$(ESP_UPLOAD_SPEED) --port $(ESP_UPLOAD_PORT)

FW1_START = 0x00000
FW2_START = 0x01010  # FIXME
#SPIFFS_START = 0x02000  # FIXME

FW1_IMAGE = app.elf-0x00000.bin
FW2_IMAGE = app.elf-0x01010.bin

ota:
	tup && python tools/espota.py

upload:
	tup && \
		$(ESPTOOL) --port $(ESP_UPLOAD_PORT) write_flash 0 app.bin
