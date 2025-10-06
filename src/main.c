#include <Arduino.h>

volatile byte analogBuffer[400];
volatile byte bufferIndex = 0;
const byte prescalers[] = {2, 4, 8, 16, 32, 64, 128};
const float samplingRates[] = {615.4, 307.7, 153.8, 76.9, 38.5, 19.2, 9.6}; // kSPS
volatile boolean samplingActive = true;
volatile byte adcPrescaler = 2; // Текущий предделитель

// Настройки sampling
struct SamplingSettings {
  byte prescalerIndex;
  byte inputChannel;    // Аналоговый вход (A0-A5)
  boolean useVref;      // Использовать внутренний опорный
  boolean triggerMode;  // Режим триггера
  byte triggerLevel;    // Уровень триггера
};

boolean samplingPaused = false;

SamplingSettings settings = {3, 0, false, false, 128};

void waitForTrigger() {
  // Простой софтверный триггер
  if (!settings.triggerMode) return;
  
  boolean triggered = false;
  while (!triggered) {
    // Ждем когда сигнал пересечет уровень триггера
    if (ADCH >= settings.triggerLevel) {
      triggered = true;
    }
  }
}

void setInputChannel(byte channel) {
  if (channel > 5) return;
  ADMUX = (ADMUX & 0xF0) | channel;
  settings.inputChannel = channel;
}

void setADCPrescaler(byte prescalerIndex) {
  if (prescalerIndex >= 7) return;
  
  ADCSRA &= ~(1 << ADEN);
  ADCSRA = 0;
  
  byte ps = prescalers[prescalerIndex];
  if (ps & 1) ADCSRA |= (1 << ADPS0);
  if (ps & 2) ADCSRA |= (1 << ADPS1);
  if (ps & 4) ADCSRA |= (1 << ADPS2);
  
  ADCSRA |= (1 << ADEN) | (1 << ADATE) | (1 << ADIE);
  settings.prescalerIndex = prescalerIndex;
}

void setVoltageReference(boolean useInternal) {
  if (useInternal) {
    ADMUX |= (1 << REFS0); // AVcc
  } else {
    ADMUX |= (1 << REFS0) | (1 << REFS1); // Внутренний 1.1V
  }
  settings.useVref = useInternal;
}

void initializeADC() {
  ADCSRA = 0;
  setInputChannel(settings.inputChannel);
  setADCPrescaler(settings.prescalerIndex);
  setVoltageReference(settings.useVref);
  
  ADCSRA |= (1 << ADATE) | (1 << ADIE);
  ADCSRA |= (1 << ADSC);
}


void setup() {
  Serial.begin(115200);
  initializeADC();
}


void sendDataFrame() {
  // Отправляем заголовок кадра
  Serial.write(0xFF); // Синхробайт 1
  Serial.write(0xAA); // Синхробайт 2
  
  // Отправляем данные
  for (int i = 0; i < 400; i++) {
    Serial.write(analogBuffer[i]);
  }
  
  // Отправляем конец кадра
  Serial.write(0x55); 
  Serial.write(0xEE);
}

void sendSettings() {
  Serial.write('S'); // Заголовок настроек
  Serial.write(adcPrescaler);
  Serial.write((byte)(samplingRates[adcPrescaler])); // Целая часть
  Serial.write((byte)((samplingRates[adcPrescaler] - (int)samplingRates[adcPrescaler]) * 100)); // Дробная
}


void startSampling() {
  samplingPaused = false;
  bufferIndex = 0; // Сбрасываем индекс буфера
  
  // Перезапускаем АЦП если он был остановлен
  ADCSRA |= (1 << ADEN) | (1 << ADATE) | (1 << ADIE);
  ADCSRA |= (1 << ADSC); // Запускаем преобразование
  
  Serial.println("Sampling STARTED");
}

void stopSampling() {
  samplingPaused = true;
  
  // Останавливаем АЦП (но не выключаем полностью)
  ADCSRA &= ~(1 << ADATE); // Останавливаем авто-триггер
  
  Serial.println("Sampling STOPPED");
}

void sendSamplingStatus() {
  Serial.write('X'); // Заголовок статуса
  Serial.write(samplingActive ? 0x01 : 0x00);
  Serial.write(samplingPaused ? 0x01 : 0x00);
}

void toggleSampling() {
  samplingActive = !samplingActive;
  
  if (samplingActive) {
    startSampling();
  } else {
    stopSampling();
  }
  
  // Отправляем статус на дисплейную плату
  sendSamplingStatus();
}

void handleSerialCommands() {
  if (Serial.available()) {
    byte command = Serial.read();
    
    switch(command) {
      case 'P': // Установить предделитель
        while (!Serial.available());
        byte prescaler = Serial.read();
        setADCPrescaler(prescaler);
        sendSettings(); // Отправляем текущие настройки
        break;
        
      case 'S': // Старт/стоп sampling
        toggleSampling();
        break;
        
      case 'R': // Запрос настроек
        sendSettings();
        break;
    }
  }
}

void loop() {
  // Обработка команд
  handleSerialCommands();
  
  // Отправка данных когда буфер заполнен
  if (bufferIndex >= 400 && samplingActive) {
    sendDataFrame();
    bufferIndex = 0;
    
    // Если режим с триггером - ждем триггер
    if (settings.triggerMode) {
      waitForTrigger();
    }
  }
}




void setTriggerMode(boolean enabled, byte level) {
  settings.triggerMode = enabled;
  settings.triggerLevel = level;
}



ISR(ADC_vect) {
  if (samplingActive && bufferIndex < 400) {
    analogBuffer[bufferIndex++] = ADCH;
  }
}
