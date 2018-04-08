package at.d7gr.weatherWebService;

import org.apache.http.client.methods.CloseableHttpResponse;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.entity.StringEntity;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClients;
import org.json.simple.JSONArray;
import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;
import org.thethingsnetwork.data.common.messages.DownlinkMessage;
import org.thethingsnetwork.data.mqtt.Client;

import javax.ws.rs.*;
import javax.ws.rs.core.Application;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;


@ApplicationPath("REST")
@Path("/")
public class RestAPI extends Application {

    boolean debug = false;

    @POST
    @Produces("application/json")
    @Path("/getWeather")
    public String getWeather(String jsonCoordinates) {

        //Convert the json object to a java object
        try {

            JSONParser parser = new JSONParser();
            JSONObject jsonCoordinatesObject = (JSONObject) parser.parse(jsonCoordinates);

            float lat = Float.parseFloat(jsonCoordinatesObject.get("lat").toString());
            float lon = Float.parseFloat(jsonCoordinatesObject.get("lon").toString());

            String weatherForecastJSON="";

            if(!debug) {
                URL url = new URL("https://api.weatherbit.io/v2.0/forecast/daily?lat=" + lat + "&lon=" + lon + "&units=m&days=1&key=MY-API-KEY");

                //Generate the request
                HttpURLConnection con = (HttpURLConnection) url.openConnection();
                con.setRequestMethod("GET");

                //Make the request
                int status = con.getResponseCode();

                //Read response
                BufferedReader in = new BufferedReader(new InputStreamReader(con.getInputStream()));
                String inputLine;
                StringBuffer content = new StringBuffer();
                while ((inputLine = in.readLine()) != null) {
                    content.append(inputLine);
                }
                in.close();

                //Disconnect
                con.disconnect();

                weatherForecastJSON = content.toString();
            } else{
                weatherForecastJSON = "{\"city_name\":\"Duivendrecht\",\"data\":[{\"wind_cdir\":\"NE\",\"rh\":39,\"wind_spd\":6,\"pop\":0,\"wind_cdir_full\":\"northeast\",\"slp\":1030.8,\"app_max_temp\":-3.8,\"pres\":1030.9,\"dewpt\":-13.2,\"snow\":0,\"uv\":3,\"ts\":1519560000,\"wind_dir\":45,\"weather\":{\"icon\":\"c01d\",\"code\":\"800\",\"description\":\"Clear sky\"},\"app_min_temp\":-9.9,\"max_temp\":1.3,\"snow_depth\":0,\"precip\":0,\"max_dhi\":740.4,\"datetime\":\"2018-02-25\",\"temp\":-1,\"min_temp\":-3.5,\"clouds\":1,\"vis\":10}],\"timezone\":\"Europe/Amsterdam\",\"lon\":4.92,\"lat\":52.35,\"country_code\":\"NL\",\"state_code\":\"07\"}";
            }

            JSONObject weatherForecastJSONObject = (JSONObject) parser.parse(weatherForecastJSON);
            //Get the data array of the json object
            JSONArray dataArray = (JSONArray) weatherForecastJSONObject.get("data");

            //From the array get the object with the weather data and read temp, min_temp, max_temp
            JSONObject weatherData = (JSONObject) dataArray.get(0);
            double temp = Double.parseDouble(weatherData.get("temp").toString());
            double minTemp = Double.parseDouble(weatherData.get("min_temp").toString());
            double maxTemp = Double.parseDouble(weatherData.get("max_temp").toString());

            //Also get the weather description from the weather object
            JSONObject weather = (JSONObject) weatherData.get("weather");
            String weatherDescription = weather.get("description").toString();
            String weatherCode = weather.get("code").toString();

            //Create a new object containing the information
            JSONObject weatherArduino = new JSONObject();
            weatherArduino.put("temp",temp);
            weatherArduino.put("minTemp",minTemp);
            weatherArduino.put("maxTemp",maxTemp);
            weatherArduino.put("weatherDescription",weatherDescription);
            weatherArduino.put("weatherCode",weatherCode);

            //Return the json string
            return weatherArduino.toJSONString();

        }catch (Exception ex){
            return "\"errorMsg\":\"failed to request weather forecast\"";
        }
    }

    private int getUTCOffset(float lat , float lon){
        //http://api.timezonedb.com/v2/get-time-zone?key=MY-API-KEY&format=json&by=position&lat=40.35&lng=3.92
        try {
            JSONParser parser = new JSONParser();
            URL url = new URL("http://api.timezonedb.com/v2/get-time-zone?key=MY-API-KEY&format=json&by=position&lat=" + lat + "&lng=" + lon);

            //Generate the request
            HttpURLConnection con = (HttpURLConnection) url.openConnection();
            con.setRequestMethod("GET");

            //Make the request
            int status = con.getResponseCode();

            //Read response
            BufferedReader in = new BufferedReader(new InputStreamReader(con.getInputStream()));
            String inputLine;
            StringBuffer content = new StringBuffer();
            while ((inputLine = in.readLine()) != null) {
                content.append(inputLine);
            }
            in.close();

            //Disconnect
            con.disconnect();

            String timeInformationJSON = content.toString();
            JSONObject timeInformationJSONObject = (JSONObject) parser.parse(timeInformationJSON);

            //Element: gmtOffset
            int offsetSeconds = Integer.parseInt(timeInformationJSONObject.get("gmtOffset").toString());
            int offsetHour = offsetSeconds / 60 / 60;

            return offsetHour;
        }catch (Exception ex){
            return 0;
        }
    }


    @POST
    @Produces("application/json")
    @Path("/getWeatherArduino")
    public String getWeatherArduino(String jsonObject) {
        try {
            JSONParser parser = new JSONParser();
            JSONObject postData = (JSONObject) parser.parse(jsonObject);

            //Get the payload object with the cooridnates
            JSONObject devicePosition = (JSONObject)postData.get("payload_fields");

            //Check if there is JSON payload and exit if not
            if(devicePosition == null || (devicePosition != null && devicePosition.isEmpty())){
                return "{message:no payload}";
            }

            //Get weather information
            String weatherInformation = getWeather(devicePosition.toJSONString());

            //Time offset
            float lat = Float.parseFloat(devicePosition.get("lat").toString());
            float lon = Float.parseFloat(devicePosition.get("lon").toString());
            int offset = getUTCOffset(lat,lon);

            //Merge offset with weather data
            JSONObject payloadObject = (JSONObject) parser.parse(weatherInformation);
            payloadObject.put("offset",offset);

            //Get the downlink url
            String downlinkURL = postData.get("downlink_url").toString();
            //Get device id
            String deviceId = postData.get("dev_id").toString();
            //Get port
            int port = Integer.parseInt(postData.get("port").toString());
            //Confirmation needed
            boolean confirmation = false;

            //Create the downlink object
            JSONObject downlinkObject = new JSONObject();
            downlinkObject.put("dev_id",deviceId);
            downlinkObject.put("port",port);
            downlinkObject.put("confirmed",confirmation);
            downlinkObject.put("payload_fields",payloadObject);

            //Connect to Things Network Application
            String region = "MY-REGION";
            String appId = "MY-APPID";
            String accessKey = "MY-ACCESSKEY";

            Client client = new Client(region, appId, accessKey);

            client.start();
            client.send(deviceId, new DownlinkMessage(port, payloadObject));
            client.end();

            return "{message:downlink sent}";
        } catch (Exception e) {
            e.printStackTrace();
            return "{}";
        }
    }

    /*Format for downlink json
    {
      "dev_id": "my-dev-id", // The device ID
      "port": 1,             // LoRaWAN FPort
      "confirmed": false,    // Whether the downlink should be confirmed by the device
      "payload_fields": {    // The JSON object to be encoded by your encoder payload function
        "on": true,
        "color": "blue"
      }
    }


     */

    /* Format of the of the post data
    {
      "app_id": "my-app-id",              // Same as in the topic
      "dev_id": "my-dev-id",              // Same as in the topic
      "hardware_serial": "0102030405060708", // In case of LoRaWAN: the DevEUI
      "port": 1,                          // LoRaWAN FPort
      "counter": 2,                       // LoRaWAN frame counter
      "is_retry": false,                  // Is set to true if this message is a retry (you could also detect this from the counter)
      "confirmed": false,                 // Is set to true if this message was a confirmed message
      "payload_raw": "AQIDBA==",          // Base64 encoded payload: [0x01, 0x02, 0x03, 0x04]
      "payload_fields": {},               // Object containing the results from the payload functions - left out when empty
      "metadata": {
        "time": "1970-01-01T00:00:00Z",   // Time when the server received the message
        "frequency": 868.1,               // Frequency at which the message was sent
        "modulation": "LORA",             // Modulation that was used - LORA or FSK
        "data_rate": "SF7BW125",          // Data rate that was used - if LORA modulation
        "bit_rate": 50000,                // Bit rate that was used - if FSK modulation
        "coding_rate": "4/5",             // Coding rate that was used
        "gateways": [
          {
            "gtw_id": "ttn-herengracht-ams", // EUI of the gateway
            "timestamp": 12345,              // Timestamp when the gateway received the message
            "time": "1970-01-01T00:00:00Z",  // Time when the gateway received the message - left out when gateway does not have synchronized time
            "channel": 0,                    // Channel where the gateway received the message
            "rssi": -25,                     // Signal strength of the received message
            "snr": 5,                        // Signal to noise ratio of the received message
            "rf_chain": 0,                   // RF chain where the gateway received the message
            "latitude": 52.1234,             // Latitude of the gateway reported in its status updates
            "longitude": 6.1234,             // Longitude of the gateway
            "altitude": 6                    // Altitude of the gateway
          },
          //...more if received by more gateways...
        ],
        "latitude": 52.2345,              // Latitude of the device
        "longitude": 6.2345,              // Longitude of the device
        "altitude": 2                     // Altitude of the device
      },
      "downlink_url": "https://integrations.thethingsnetwork.org/ttn-eu/api/v2/down/my-app-id/my-process-id?key=ttn-account-v2.secret"
    }
    */
}
