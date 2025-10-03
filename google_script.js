function doGet(e) {
  // Get the active spreadsheet and the data sheet
  var spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = spreadsheet.getSheetByName("rawdata");
  
  // Create the sheet if it doesn't exist
  if (!sheet) {
    sheet = spreadsheet.insertSheet("rawdata");
    sheet.appendRow([ "Timestamp (UTC epoch)", 
                      "Voltage 1 (V)",
                      "Voltage 2 (V)",
                      "ADC value 1",
                      "ADC value 2",
                      "User",
                      "Temperature (°C)",
                      "Humidity (%)",
                      "Current 1 (mA)",
                      "Current 2 (mA)"
                      ]);
    var cell = sheet.getRange("I2");
    cell.setFormula('=ARRAYFORMULA(IF(B2:B<>"", B2:B*1000/params!$A$2, ))');
    var cell = sheet.getRange("J2");
    cell.setFormula('=ARRAYFORMULA(IF(C2:C<>"", C2:C*1000/params!$A$2, ))');
}
  
  // Get parameters from the request
  var timestamp = e.parameter.timestamp || new Date().getTime();
  var voltage0 = e.parameter.voltage0 || "No Value";
  var voltage1 = e.parameter.voltage1 || "No Value";
  var rawValue0 = e.parameter.raw_value0 || "No Value";
  var rawValue1 = e.parameter.raw_value1 || "No Value";
  var user = e.parameter.user || "No Value";
  var temperature = e.parameter.temperature || "No Value";
  var humidity = e.parameter.humidity || "No Value";
  
  // Log the incoming data
  Logger.log("Data received - Timestamp: " + timestamp + ", Voltage 0: " + voltage0 + ", Voltage 1: " + voltage1 + ", Raw Value 0: " + rawValue0 + ", Raw Value 1: " + rawValue1 + ", User: " + user + ", Temperature: " + temperature + ", Humidity: " + humidity);
  
  try {
    // Append the data to the sheet
    sheet.appendRow([timestamp, voltage0, voltage1 , rawValue0, rawValue1, user, temperature, humidity]);
    
    // Return success response
    return ContentService.createTextOutput("Success: Data added to Google Sheet")
      .setMimeType(ContentService.MimeType.TEXT);
      
  } catch (error) {
    // Return error response
    return ContentService.createTextOutput("Error: " + error.message)
      .setMimeType(ContentService.MimeType.TEXT);
  }
}
