#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <base64.h>

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// Google Cloud Speech-to-Text API endpoint
const char* speechAPIUrl = "https://speech.googleapis.com/v1/speech:recognize?key=YOUR_GOOGLE_API_KEY";

// Google Text-to-Speech API endpoint
const char* ttsAPIUrl = "https://texttospeech.googleapis.com/v1/text:synthesize?key=YOUR_GOOGLE_API_KEY";

// ChatGPT API endpoint
const char* chatGPTAPIUrl = "https://api.openai.com/v1/completions";
const char* chatGPTAPIKey = "YOUR_CHATGPT_API_KEY";

// I2S pins for the MEMS microphone
#define I2S_WS 25  // Word Select (LRCL)
#define I2S_SD 26  // Serial Data (DOUT)
#define I2S_SCK 27 // Serial Clock (BCLK)

// Buffer for audio data
#define BUFFER_SIZE 16000

void setupI2SMic() {
    i2s_config_t i2s_config = {
        mode: i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
        sample_rate: 16000,
        bits_per_sample: I2S_BITS_PER_SAMPLE_16BIT,
        channel_format: I2S_CHANNEL_FMT_ONLY_LEFT,
        communication_format: i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        intr_alloc_flags: 0,
        dma_buf_count: 8,
        dma_buf_len: 64,
        use_apll: false
    };

    i2s_pin_config_t pin_config = {
        bck_io_num: I2S_SCK,
        ws_io_num: I2S_WS,
        data_out_num: I2S_PIN_NO_CHANGE,
        data_in_num: I2S_SD
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
}

String recognizeSpeech() {
    int16_t audioData[BUFFER_SIZE]; // Buffer for 1 second of audio at 16kHz

    // Read data from I2S
    size_t bytesRead;
    i2s_read(I2S_NUM_0, audioData, BUFFER_SIZE * sizeof(int16_t), &bytesRead, portMAX_DELAY);

    // Convert audio data to base64 (if needed by the API)
    String base64AudioData = base64::encode((uint8_t*)audioData, bytesRead);

    // Make HTTP POST request to Google Cloud Speech-to-Text API
    HTTPClient http;
    http.begin(speechAPIUrl);
    http.addHeader("Content-Type", "application/json");

    // Create JSON payload
    StaticJsonDocument<1024> jsonDoc;
    jsonDoc["config"]["encoding"] = "LINEAR16";
    jsonDoc["config"]["sampleRateHertz"] = 16000;
    jsonDoc["config"]["languageCode"] = "pl-PL";
    jsonDoc["audio"]["content"] = base64AudioData;
    String requestBody;
    serializeJson(jsonDoc, requestBody);

    int httpResponseCode = http.POST(requestBody);

    String response;
    if (httpResponseCode > 0) {
        response = http.getString();
    } else {
        response = "Error in HTTP request";
    }
    http.end();

    // Parse the response to get the recognized text
    StaticJsonDocument<1024> responseDoc;
    deserializeJson(responseDoc, response);
    const char* recognizedText = responseDoc["results"][0]["alternatives"][0]["transcript"];
    return String(recognizedText);
}

String generateResponse(String prompt) {
    HTTPClient http;
    http.begin(chatGPTAPIUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + chatGPTAPIKey);

    // Create JSON payload
    StaticJsonDocument<512> jsonDoc;
    jsonDoc["model"] = "text-davinci-003";
    jsonDoc["prompt"] = prompt;
    jsonDoc["max_tokens"] = 150;  // Increased tokens for longer responses
    jsonDoc["temperature"] = 0.7;
    String requestBody;
    serializeJson(jsonDoc, requestBody);

    int httpResponseCode = http.POST(requestBody);

    String response;
    if (httpResponseCode > 0) {
        response = http.getString();
    } else {
        response = "Error in HTTP request";
    }
    http.end();

    // Parse the response to get the generated text
    StaticJsonDocument<1024> responseDoc;
    deserializeJson(responseDoc, response);
    const char* chatGPTResponse = responseDoc["choices"][0]["text"];
    return String(chatGPTResponse);
}

void playResponse(String response) {
    // Convert text to speech using Google Text-to-Speech API
    HTTPClient http;
    http.begin(ttsAPIUrl);
    http.addHeader("Content-Type", "application/json");

    // Create JSON payload
    StaticJsonDocument<512> jsonDoc;
    jsonDoc["input"]["text"] = response;
    jsonDoc["voice"]["languageCode"] = "pl-PL";
    jsonDoc["voice"]["name"] = "pl-PL-Wavenet-A";  // Adjust voice as needed
    jsonDoc["audioConfig"]["audioEncoding"] = "LINEAR16";
    String requestBody;
    serializeJson(jsonDoc, requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
        String response = http.getString();

        // Parse the JSON response
        StaticJsonDocument<1024> responseDoc;
        deserializeJson(responseDoc, response);
        const char* audioContent = responseDoc["audioContent"];
        String audioData = String(audioContent);

        // Decode base64 audio data
        int audioLength = base64::decodedLength(audioData.c_str(), audioData.length());
        uint8_t* audioBuffer = new uint8_t[audioLength];
        base64::decode(audioBuffer, audioData.c_str(), audioData.length());

        // Play audio using DAC and PAM8403
        for (int i = 0; i < audioLength; i++) {
            dacWrite(25, audioBuffer[i]);
            delayMicroseconds(100); // Adjust delay to match audio sample rate
        }
        delete[] audioBuffer;
    } else {
        Serial.println("Error in HTTP request");
    }
    http.end();
}

void setup() {
    Serial.begin(115200);
    setupI2SMic();
    connectToWiFi();
}

void loop() {
    String recognizedText = recognizeSpeech();
    Serial.println("Recognized Text: " + recognizedText);

    String response = generateResponse(recognizedText);
    Serial.println("ChatGPT Response: " + response);

    playResponse(response);
}
