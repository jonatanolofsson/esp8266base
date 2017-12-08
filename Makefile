ESPTOOL = ./tools/esptool
ESPOTA = python ./tools/espota.py
ESP_UPLOAD_PORT = /dev/ttyUSB0
ESP_UPLOAD_SPEED = 115200

ESP_UPLOAD = $(ESPTOOL) --baud=$(ESP_UPLOAD_SPEED) --port $(ESP_UPLOAD_PORT)

ota:
	tup && python tools/espota.py -i 192.168.1.66 -f app.bin

upload:
	tup && \
		$(ESPTOOL) -cd nodemcu -cb $(ESP_UPLOAD_SPEED) -cp $(ESP_UPLOAD_PORT) -cf app.bin
