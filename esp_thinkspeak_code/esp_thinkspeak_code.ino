#include <ESP8266WiFi.h>
#include <WiFiClient.h>

const char* ssid = "Arnoy";
const char* password = "uxoricide";
const char* apiKey = "6LC25JKW41MM8G9C";  // Write API Key
const char* server = "api.thingspeak.com";
WiFiClient client;

String dataBuffer = "";   // Buffer for data from Arduino

void setup() {
  Serial.begin(9600);  // Communication with Arduino Mega
  Serial.println("ESP8266 is ready");
  connectToWiFi();
}

void loop() {
  // Check Wi-Fi connection
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  // Read data from Arduino
  while (Serial.available()) {
    char c = Serial.read();
    dataBuffer += c;

    // Process and send data when a full packet is received
    if (c == '\n') {
      if (processAndSendData(dataBuffer)) {
        Serial.println("Data sent to ThingSpeak successfully.");
      } else {
        Serial.println("Error: Invalid data format.");
      }
      dataBuffer = "";  // Clear buffer for the next data packet
    }
  }
  delay(100); // Short delay to reduce rapid data polling
  Serial.println("working");
}

void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  int attempt = 0;

  while (WiFi.status() != WL_CONNECTED && attempt < 10) {
    delay(500);
    Serial.print(".");
    attempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi");
  } else {
    Serial.println("\nFailed to connect to Wi-Fi. Retrying...");
    delay(5000);
    connectToWiFi();
  }
}

float estimateBacteriaPresence(float temp, float pH, float turb, float tds) {
  float bacteriaScore = 0.0;

  // Calculate temperature score
  if (temp >= 20 && temp <= 40) {
    bacteriaScore += 30.0;
  } else if (temp < 20) {
    bacteriaScore += (temp / 20.0) * 30.0;  // Scale below 20°C
  } else if (temp > 40 && temp <= 50) {
    bacteriaScore += ((50 - temp) / 10.0) * 30.0;  // Scale from 40-50°C
  }

  // Calculate pH score
  if (pH >= 6.5 && pH <= 8.5) {
    bacteriaScore += 30.0;
  } else if (pH < 6.5) {
    bacteriaScore += (pH / 6.5) * 30.0;  // Scale below 6.5
  } else if (pH > 8.5 && pH <= 10) {
    bacteriaScore += ((10 - pH) / 1.5) * 30.0;  // Scale from 8.5-10
  }

  // Calculate turbidity score
  if (turb >= 1 && turb <= 5) {
    bacteriaScore += 20.0;
  } else if (turb < 1) {
    bacteriaScore += (turb / 1.0) * 20.0;  // Scale below 1 NTU
  } else if (turb > 5 && turb <= 10) {
    bacteriaScore += ((10 - turb) / 5.0) * 20.0;  // Scale from 5-10 NTU
  }

  // Calculate TDS score
  if (tds >= 500 && tds <= 1000) {
    bacteriaScore += 20.0;
  } else if (tds < 500) {
    bacteriaScore += (tds / 500.0) * 20.0;  // Scale below 500 ppm
  } else if (tds > 1000 && tds <= 1500) {
    bacteriaScore += ((1500 - tds) / 500.0) * 20.0;  // Scale from 1000-1500 ppm
  }

  // Ensure bacteria percentage does not exceed 100%
  return min(bacteriaScore, 100.0f);
}


bool processAndSendData(String data) {
  float temperature, ph, turbidity, tds, hydroponicsTemp, humidity, doxygen;

  // Parse data from Arduino in format "Temp:XX|HydroTemp:XX|pH:XX|Turb:XX|TDS:XX|DO:XX|Humidity:XX"
  int result = sscanf(data.c_str(), "Temp:%f|HydroTemp:%f|pH:%f|Turb:%f|TDS:%f|DO:%f|Humidity:%f", 
                      &temperature, &hydroponicsTemp, &ph, &turbidity, &tds, &doxygen, &humidity);
  if (result != 7) {
    return false;  // Return false if data format is invalid
  }

  // Print parsed data for debugging
  Serial.print("Temperature: "); Serial.println(temperature);
  Serial.print("Hydroponics Temperature: "); Serial.println(hydroponicsTemp);
  Serial.print("pH: "); Serial.println(ph);
  Serial.print("Turbidity: "); Serial.println(turbidity);
  Serial.print("TDS: "); Serial.println(tds);
  Serial.print("Dissolved Oxygen: "); Serial.println(doxygen);
  Serial.print("Humidity: "); Serial.println(humidity);

  // Send data to ThingSpeak
  if (client.connect(server, 80)) {
    String postStr = apiKey;
    postStr += "&field1=" + String(ph);
    postStr += "&field2=" + String(tds);
    postStr += "&field3=" + String(turbidity);
    postStr += "&field4=" + String(temperature);
    postStr += "&field5=" + String(hydroponicsTemp);
    postStr += "&field6=" + String(humidity);
    postStr += "&field7=" + String(doxygen);
    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + String(apiKey) + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: " + String(postStr.length()) + "\n\n");
    client.print(postStr);

    // Check for server response
    delay(1000);
    if (client.available()) {
      String response = client.readString();
      Serial.print("ThingSpeak Response: ");
      Serial.println(response);
    } else {
      Serial.println("No response from ThingSpeak");
    }

    client.stop();  // Close connection
    return true;  // Successfully sent
  } else {
    Serial.println("Connection to ThingSpeak failed");
    return false;  // Failed to send
  }
}