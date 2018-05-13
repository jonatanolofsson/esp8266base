#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <mDNSResolver.h>

#include <cstdlib>

namespace wifi {
    using namespace mDNSResolver;
    char dnsname[12] = { 0 };

    WiFiUDP udp;
    Resolver resolver(udp);

    void dns_announce() {
        if(MDNS.begin(dnsname)) {
            MDNS.addService("http", "tcp", 80);
        }
    }

    void setup() {
        snprintf(dnsname, sizeof(dnsname), "ESP%d", ESP.getChipId());
        WiFiManager wifi_manager;
        wifi_manager.autoConnect();
        dns_announce();
        resolver.setLocalIP(WiFi.localIP());
    }

    void loop() {
        if(WiFi.status() != WL_CONNECTED) {
            WiFi.begin();
            dns_announce();
        }
        resolver.loop();
    }

    void print_info() {
        static long last = 0;
        long now = millis();
        if(now - last > 30000) {
            last = now;
            Serial.print(dnsname);
            Serial.print(".local");
            Serial.print(" @ ");
            Serial.println(WiFi.localIP());
        }
    }
}

namespace http {
    const int port = 80;
    ESP8266WebServer server(port);

    void root() {
        char temp[400];
        int sec = millis() / 1000;
        int min = sec / 60;
        int hr = min / 60;

        snprintf(temp, 400,
    "<html>\
      <head>\
        <meta http-equiv='refresh' content='5'/>\
        <title>ESP8266 Demo</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
        </style>\
      </head>\
      <body>\
        <h1>Hello from ESP8266!</h1>\
        <p>Uptime: %02d:%02d:%02d</p>\
        <img src=\"/test.svg\" />\
      </body>\
    </html>",
            hr, min % 60, sec % 60
        );
        server.send(200, "text/html", temp);
    }

    void draw_graph() {
        String out = "";
        char temp[100];
        out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"150\">\n";
        out += "<rect width=\"400\" height=\"150\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
        out += "<g stroke=\"black\">\n";
        int y = rand() % 130;
        for(int x = 10; x < 390; x+= 10) {
            int y2 = rand() % 130;
            sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", x, 140 - y, x + 10, 140 - y2);
            out += temp;
            y = y2;
        }
        out += "</g>\n</svg>\n";

        server.send(200, "image/svg+xml", out);
    }

    void setup() {
        server.on("/", root);
        server.on("/test.svg", draw_graph);
        server.on("/inline", []() { server.send(200, "text/plain", "this works as well"); });
        server.begin();
        Serial.print("HTTP server started on port ");
        Serial.println(port);
    }

    void loop() {
        server.handleClient();
    }
}

namespace mqtt {
    const long reconnect_interval = 5000;
    const char* mqtt_hostname = "mqtt.local";

    WiFiClient wifi_client;
    PubSubClient mqtt(wifi_client);

    void blinds_command(char* topic, byte* payload, unsigned int length) {
        Serial.println("Got callback: ");
        StaticJsonBuffer<200> json_buffer;
        JsonObject& root = json_buffer.parseObject(payload);
        if(root.success() && root.containsKey("body")) {
            Serial.println(root["body"].as<int>());
        } else {
            Serial.print("Parsing failed on topic ");
            Serial.println(topic);
        }
    }

    boolean reconnect() {
        static long last = -2 * reconnect_interval;
        long now = millis();
        if(now - last > reconnect_interval) {
            static IPAddress old_ip;
            IPAddress ip = wifi::resolver.search(mqtt_hostname);
            if(ip != INADDR_NONE) {
                Serial.print("MQTT broker: ");
                Serial.println(ip);
                mqtt.setServer(ip, 1883);
            } else {
                Serial.println("MQTT broker not found");
            }

            last = now;
            if(mqtt.connect(wifi::dnsname)) {
                mqtt.resubscribe();
            }
        }
        return mqtt.connected();
    }

    void setup() {
        mqtt.subscribe("bedroom/blinds", blinds_command);
    }

    void loop() {
        if(!mqtt.connected()) {
            reconnect();
        }
        if(mqtt.connected()) {
            mqtt.loop();
        }
    }
}

namespace ota {
    void setup() {
        ArduinoOTA.begin();
    }

    void loop() {
        ArduinoOTA.handle();
    }
}

namespace app {
    void setup() {
    }

    void loop() {
    }
}

void setup() {
    Serial.begin(9600);
    wifi::setup();
    http::setup();
    mqtt::setup();
    ota::setup();
    app::setup();
}

void loop() {
    wifi::loop();
    http::loop();
    mqtt::loop();
    ota::loop();
    app::loop();
    wifi::print_info();
}
