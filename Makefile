ESPTOOL = ./tools/esptool
ESPOTA = python ./tools/espota.py
ESP_UPLOAD_PORT = /dev/ttyUSB0
ESP_UPLOAD_SPEED = 115200
OTA_IP = 192.168.0.109

ESP_UPLOAD = $(ESPTOOL) --baud=$(ESP_UPLOAD_SPEED) --port $(ESP_UPLOAD_PORT)

ota:
	tup && python tools/espota.py -i $(OTA_IP) -f app.bin

upload:
	tup && \
		$(ESPTOOL) -cd nodemcu -cb $(ESP_UPLOAD_SPEED) -cp $(ESP_UPLOAD_PORT) -cf app.bin
