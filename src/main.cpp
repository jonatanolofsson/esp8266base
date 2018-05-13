// Copyright 2018 Jonatan Olofsson
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <mDNSResolver.h>

#include <cstdlib>

namespace wifi {
using mDNSResolver::Resolver;
char dnsname[12] = { 0 };

WiFiUDP udp;
Resolver resolver(udp);

void ICACHE_FLASH_ATTR setup() {
    snprintf(dnsname, sizeof(dnsname), "ESP%d", ESP.getChipId());
    WiFiManager wifi_manager;
    wifi_manager.autoConnect();
    resolver.setLocalIP(WiFi.localIP());
}

void ICACHE_FLASH_ATTR loop() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin();
    }
    resolver.loop();
}

void ICACHE_FLASH_ATTR print_info() {
    static uint64_t last = 0;
    uint64_t now = millis();
    if (now - last > 30000) {
        last = now;
        Serial.print(dnsname);
        Serial.print(".local");
        Serial.print(" @ ");
        Serial.println(WiFi.localIP());
    }
}
}  // namespace wifi

namespace mqtt {
const int reconnect_interval = 5000;
const char* mqtt_hostname = "mqtt.local";

WiFiClient wifi_client;
PubSubClient mqtt(wifi_client);

boolean ICACHE_FLASH_ATTR reconnect() {
    static int last = -2 * reconnect_interval;
    uint64_t  now = millis();
    if (now - last > reconnect_interval) {
        static IPAddress old_ip;
        IPAddress ip = wifi::resolver.search(mqtt_hostname);
        if (ip != INADDR_NONE) {
            Serial.print("MQTT broker: ");
            Serial.println(ip);
            mqtt.setServer(ip, 1883);
        } else {
            Serial.println("MQTT broker not found");
        }

        last = now;
        if (mqtt.connect(wifi::dnsname)) {
            mqtt.resubscribe();
        }
    }
    return mqtt.connected();
}

void ICACHE_FLASH_ATTR setup() {}

void ICACHE_FLASH_ATTR loop() {
    if (!mqtt.connected()) {
        reconnect();
    }
    if (mqtt.connected()) {
        mqtt.loop();
    }
}
}  // namespace mqtt

namespace ota {
void ICACHE_FLASH_ATTR setup() {
    ArduinoOTA.begin();
}

void ICACHE_FLASH_ATTR loop() {
    ArduinoOTA.handle();
}
}  // namespace ota

namespace app {
enum STATE {
    HALTED = 0,
    MOVING_UP = 1,
    MOVING_DOWN = 2
};
struct Motor {
    const char* name;
    int eepromaddress, out1, out2, pwm;
    uint64_t  scheduled_stop = 0;
    struct ShadeState {
        int32_t current_level = 0;
        int32_t full_time = 10000;
    };
    ShadeState shadestate;
    uint64_t  action_started;
    uint64_t  speed;
    STATE state;

    Motor(const char* name_, int eepromaddress_, int out1_, int out2_, int pwm_, int speed_ = PWMRANGE)
    : name(name_),
      eepromaddress(eepromaddress_ * sizeof(ShadeState)),
      out1(out1_),
      out2(out2_),
      pwm(pwm_),
      speed(speed_),
      state(HALTED) {
        EEPROM.get(eepromaddress, shadestate);
        pinMode(out1, OUTPUT);
        pinMode(out2, OUTPUT);
        pinMode(pwm, OUTPUT);
    }
};

void ICACHE_FLASH_ATTR send_state(Motor& motor) {
    StaticJsonBuffer<50> jsonbuffer;
    char jsontopic[20];
    char jsoncbuffer[50];

    JsonObject& encoder = jsonbuffer.createObject();
    encoder["level"] = static_cast<float>(motor.shadestate.current_level / motor.shadestate.full_time);
    encoder.printTo(jsoncbuffer, sizeof(jsoncbuffer));
    strcpy(jsontopic, motor.name);
    strcat(jsontopic, "/state");
    mqtt::mqtt.publish(jsontopic, jsoncbuffer);
}

void ICACHE_FLASH_ATTR stop(Motor& motor) {
    if (motor.state == HALTED) {
        return;
    }
    digitalWrite(motor.out1, LOW);
    digitalWrite(motor.out2, LOW);
    analogWrite(motor.pwm, 0);
    motor.shadestate.current_level += (millis() - motor.action_started) * (motor.state == MOVING_DOWN ? 1 : -1);
    EEPROM.put(motor.eepromaddress, motor.shadestate);
    motor.state = HALTED;
    send_state(motor);
}

void ICACHE_FLASH_ATTR up(Motor& motor) {
    stop(motor);
    digitalWrite(motor.out1, HIGH);
    digitalWrite(motor.out2, LOW);
    analogWrite(motor.pwm, motor.speed);
    motor.action_started = millis();
    motor.state = MOVING_UP;
}

void ICACHE_FLASH_ATTR down(Motor& motor) {
    stop(motor);
    digitalWrite(motor.out1, LOW);
    digitalWrite(motor.out2, HIGH);
    analogWrite(motor.pwm, motor.speed);
    motor.action_started = millis();
    motor.state = MOVING_DOWN;
}

void ICACHE_FLASH_ATTR schedule_stop(Motor& motor, uint64_t  stoptime) {
    motor.scheduled_stop = stoptime;
}

void ICACHE_FLASH_ATTR move(Motor& motor, float level) {
    stop(motor);
         if (level < 0) { level = 0; }
    else if (level > 1) { level = 1; }
    Serial.print("Move to ");
    Serial.println(level);

    int64_t  diff = uint64_t (level * motor.shadestate.full_time) - motor.shadestate.current_level;
    if (diff < 0) { up(motor); diff = -diff; } else { down(motor); }
    schedule_stop(motor, millis() + diff);
}

void ICACHE_FLASH_ATTR set_up(Motor& motor) {
    motor.shadestate.current_level = 0;
    EEPROM.put(motor.eepromaddress, motor.shadestate);
    Serial.println("Set up level");
}

void ICACHE_FLASH_ATTR set_down(Motor& motor) {
    motor.shadestate.full_time = motor.shadestate.current_level;
    EEPROM.put(motor.eepromaddress, motor.shadestate);
    Serial.print("Set down level: ");
    Serial.println(motor.shadestate.full_time);
}

void ICACHE_FLASH_ATTR motorloop(Motor& motor) {
    if (motor.scheduled_stop != 0 && millis() > motor.scheduled_stop) {
        motor.scheduled_stop = 0;
        stop(motor);
    }
}

void ICACHE_FLASH_ATTR command(Motor& motor, char* topic, byte* payload) {
    StaticJsonBuffer<200> json_buffer;
    JsonObject& root = json_buffer.parseObject(payload);
    if (root.success()) {
        if (root.containsKey("level")) {
            auto level = root["level"].as<float>();
            move(motor, level);
        } else if (root.containsKey("action")) {
            const char* actionstr = root["action"];
            const char action = actionstr[0];
            Serial.print("Received action: ");
            Serial.println(action);
                 if (action == 'u') { up(motor); }
            else if (action == 'd') { down(motor); }
            else if (action == 's') { stop(motor); }
            else if (action == '0') { set_up(motor); }
            else if (action == '1') { set_down(motor); }
        }
    } else {
        Serial.print("Parsing failed on topic ");
        Serial.println(topic);
    }
}

Motor a("blinds1", 0, D0, D2, D1);
Motor b("blinds0", 1, D5, D6, D7);

void ICACHE_FLASH_ATTR blinds_command_a(char* topic, byte* payload, unsigned int) { command(a, topic, payload); }
void ICACHE_FLASH_ATTR blinds_command_b(char* topic, byte* payload, unsigned int) { command(b, topic, payload); }
void ICACHE_FLASH_ATTR send_state_a(char* topic, byte* payload, unsigned int) { send_state(a); }
void ICACHE_FLASH_ATTR send_state_b(char* topic, byte* payload, unsigned int) { send_state(b); }

void ICACHE_FLASH_ATTR setup() {
    // Turn off LED
    pinMode(D4, OUTPUT);
    digitalWrite(D4, LOW);

    EEPROM.begin(2 * sizeof(Motor::ShadeState));

    mqtt::mqtt.subscribe("blinds1/command", blinds_command_a);
    mqtt::mqtt.subscribe("blinds0/command", blinds_command_b);
    mqtt::mqtt.subscribe("blinds1/send_state", send_state_a);
    mqtt::mqtt.subscribe("blinds0/send_state", send_state_b);
}

void ICACHE_FLASH_ATTR loop() {
    motorloop(a);
    motorloop(b);
}
}  // namespace app

void setup() {
    Serial.begin(9600);
    wifi::setup();
    mqtt::setup();
    ota::setup();
    app::setup();
}

void loop() {
    wifi::loop();
    mqtt::loop();
    ota::loop();
    app::loop();
    wifi::print_info();
}
