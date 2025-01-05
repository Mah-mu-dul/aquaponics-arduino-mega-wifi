#include <OneWire.h>
#include <DallasTemperature.h>
#include "GravityTDS.h"
#include <DHT11.h>

#define TdsSensorPin A8
#define ONE_WIRE_BUS 2
#define pHpin A7
#define TurbSensorPin A5
#define DOPin A2
#define VREF 5000     // VREF(mv) for DO
#define ADC_RES 1024  // ADC Resolution for DO
#define DHT11Pin 5  // Define the pin connected to the DHT11 sensor
#define HydroponicsTempPin 3  // Define the pin connected to the additional temperature sensor

// Calibration and offset values
float OffsetpH = 5.20;
float calibration_value = 21.34 + OffsetpH;

OneWire oneWire(ONE_WIRE_BUS);
GravityTDS gravityTds;
DallasTemperature tempSensor(&oneWire);
DHT11 dht11(DHT11Pin);
OneWire hydroponicsOneWire(HydroponicsTempPin);
DallasTemperature hydroponicsTempSensor(&hydroponicsOneWire);

// Sampling and averaging variables
const unsigned long SAMPLE_INTERVAL = 500;     // Sample every 500ms
const unsigned long REPORTING_INTERVAL = 15000; // Report every 15 seconds

// Sensor data tracking structures
struct SensorData {
  float sum = 0;
  int count = 0;
};

struct {
  SensorData temperature;
  SensorData hydroponicsTemp;
  SensorData tds;
  SensorData ph;
  SensorData turbidity;
  SensorData doxygen;
  SensorData humidity;
} sensorReadings;

// Timing variables
unsigned long lastSampleTime = 0;
unsigned long lastReportTime = 0;

void setup() {
  Serial.begin(9600);        // Debugging output
  Serial3.begin(9600);       // Communication with ESP8266
  
  // Temperature Sensor Initialization
  tempSensor.begin();
  
  // Hydroponics Temperature Sensor Initialization
  hydroponicsTempSensor.begin();
  
  // TDS Sensor Initialization
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);  //reference voltage on ADC, default 5.0V on Arduino UNO
  gravityTds.setAdcRange(1024);  //1024 for 10bit ADC;4096 for 12bit ADC
  gravityTds.begin();  //initialization
  
  // pH Sensor Initialization
  pinMode(pHpin, INPUT);
  
  // Turbidity Sensor Initialization
  pinMode(TurbSensorPin, INPUT);
  
  // DO Sensor Initialization
  pinMode(DOPin, INPUT);
  
  delay(2000);  // Initial stabilization delay
}

// Function to read temperature
float readTemperature() {
  tempSensor.requestTemperatures(); 
  return tempSensor.getTempCByIndex(0);
}

// Function to read TDS
float readTDS(float temperature) {
  gravityTds.setTemperature(temperature);  // set the temperature and execute temperature compensation
  gravityTds.update();  //sample and calculate
  return gravityTds.getTdsValue();
}

// Function to read pH
float readPH() {
  int buffer_arr[100];
  int buffer_index = 0;
  unsigned long start_time = millis();
  const unsigned long sampling_interval = 30;  // 30ms between readings
  const unsigned long duration = 5000;         // 5 seconds duration
  unsigned long last_sample_time = 0;

  while (millis() - start_time < duration) {
    unsigned long current_time = millis();
    // Take a reading every 30ms without blocking
    if (current_time - last_sample_time >= sampling_interval) {
      last_sample_time = current_time;
      buffer_arr[buffer_index] = analogRead(pHpin);
      buffer_index++;
      
      if (buffer_index >= 100) break;  // Prevent buffer overflow
    }
  }

  // Sort the buffer
  for (int i = 0; i < buffer_index - 1; i++) {
    for (int j = i + 1; j < buffer_index; j++) {
      if (buffer_arr[i] > buffer_arr[j]) {
        int temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }

  // Calculate the average by discarding outliers (middle 80%)
  int valid_start = buffer_index * 0.1;
  int valid_end = buffer_index * 0.9;
  unsigned long avgval = 0;
  for (int i = valid_start; i < valid_end; i++) {
    avgval += buffer_arr[i];
  }
  avgval /= (valid_end - valid_start);

  // Convert to voltage and then pH value
  float volt = (float)avgval * 5.0 / 1024;
  float ph_act = -5.70 * volt + calibration_value;
  
  // Boundary the pH value between 0 and 14
  return constrain(ph_act, 0.0, 14.0);
}

// Function to read Turbidity
int readTurbidity() {
  int read_ADC = analogRead(TurbSensorPin);
  
  // Clamp the ADC value between the extended cleanest and dirtiest range
  if (read_ADC > 425) read_ADC = 425;
  if (read_ADC < 290) read_ADC = 290;
  
  // Recalibrate NTU mapping: 450 (hypothetical cleanest) -> 0 NTU, 280 (hypothetical dirtiest) -> 300 NTU
  return map(read_ADC, 425, 290, 0, 300);
}

// Function to read Dissolved Oxygen
float readDO() {
  uint32_t raw = analogRead(DOPin);
  float voltage = raw * VREF / ADC_RES;
  // Add any necessary calibration or conversion for DO here
  return voltage;  // Placeholder: replace with actual DO calculation
}

// Function to read Humidity
int readHumidity() {
  int humidity = dht11.readHumidity();
  if (humidity != DHT11::ERROR_CHECKSUM && humidity != DHT11::ERROR_TIMEOUT) {
    return humidity;
  } else {
    Serial.println(DHT11::getErrorString(humidity));
    return -1;  // Return -1 to indicate an error
  }
}

// Function to read Hydroponics Temperature
float readHydroponicsTemperature() {
  hydroponicsTempSensor.requestTemperatures(); 
  return hydroponicsTempSensor.getTempCByIndex(0);
}

void loop() {
  unsigned long currentTime = millis();

  // Sample sensors at regular intervals
  if (currentTime - lastSampleTime >= SAMPLE_INTERVAL) {
    // Read and accumulate sensor values
    float temperature = readTemperature();
    sensorReadings.temperature.sum += temperature;
    sensorReadings.temperature.count++;

    float hydroponicsTemp = readHydroponicsTemperature();
    sensorReadings.hydroponicsTemp.sum += hydroponicsTemp;
    sensorReadings.hydroponicsTemp.count++;

    float tds = readTDS(temperature);
    sensorReadings.tds.sum += tds;
    sensorReadings.tds.count++;

    float ph = readPH();
    sensorReadings.ph.sum += ph;
    sensorReadings.ph.count++;

    int turbidity = readTurbidity();
    sensorReadings.turbidity.sum += turbidity;
    sensorReadings.turbidity.count++;

    float doxygen = readDO();
    sensorReadings.doxygen.sum += doxygen;
    sensorReadings.doxygen.count++;

    int humidity = readHumidity();
    if (humidity != -1) {  // Only accumulate if no error
      sensorReadings.humidity.sum += humidity;
      sensorReadings.humidity.count++;
    }

    lastSampleTime = currentTime;
  }

  // Report averaged data every 15 seconds
  if (currentTime - lastReportTime >= REPORTING_INTERVAL) {
    // Calculate averages
    float avgTemperature = sensorReadings.temperature.count > 0 ? 
      sensorReadings.temperature.sum / sensorReadings.temperature.count : 0;
    float avgHydroponicsTemp = sensorReadings.hydroponicsTemp.count > 0 ? 
      sensorReadings.hydroponicsTemp.sum / sensorReadings.hydroponicsTemp.count : 0;
    float avgTDS = sensorReadings.tds.count > 0 ? 
      sensorReadings.tds.sum / sensorReadings.tds.count : 0;
    float avgPH = sensorReadings.ph.count > 0 ? 
      sensorReadings.ph.sum / sensorReadings.ph.count : 0;
    float avgTurbidity = sensorReadings.turbidity.count > 0 ? 
      sensorReadings.turbidity.sum / sensorReadings.turbidity.count : 0;
    float avgDO = (sensorReadings.doxygen.count > 0 ? 
      sensorReadings.doxygen.sum / sensorReadings.doxygen.count : 0)/100;
    float avgHumidity = sensorReadings.humidity.count > 0 ? 
      sensorReadings.humidity.sum / sensorReadings.humidity.count : 0;

    // Format data packet for transmission
    String dataPacket = "Temp:" + String(avgTemperature) +
                        "|HydroTemp:" + String(avgHydroponicsTemp) +
                        "|pH:" + String(avgPH) +
                        "|Turb:" + String(avgTurbidity) +
                        "|TDS:" + String(avgTDS) +
                        "|DO:" + String(avgDO) +
                        "|Humidity:" + String(avgHumidity) + "\n";
    
    // Send data packet to ESP8266
    Serial3.print(dataPacket);
    
    // Print data to Serial monitor for debugging
    Serial.println(dataPacket);
    
    // Reset accumulated data
    sensorReadings = {};
    lastReportTime = currentTime;
  }
}