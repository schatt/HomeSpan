
#include <DNSServer.h>

#include "Network.h"
#include "Utils.h"
#include "HAP.h"

using namespace Utils;

///////////////////////////////

void Network::scan(){

  int n=WiFi.scanNetworks();

  free(ssidList);
  ssidList=(char **)calloc(n,sizeof(char *));
  numSSID=0;

  for(int i=0;i<n;i++){
    boolean found=false;
    for(int j=0;j<numSSID;j++){
      if(!strcmp(WiFi.SSID(i).c_str(),ssidList[j]))
        found=true;
    }
    if(!found){
      ssidList[numSSID]=(char *)calloc(WiFi.SSID(i).length()+1,sizeof(char));
      sprintf(ssidList[numSSID],"%s",WiFi.SSID(i).c_str());
      numSSID++;
    }
  }

}

///////////////////////////////

boolean Network::serialConfigure(){

  sprintf(ssid,"");
  sprintf(pwd,"");
 
  while(!strlen(ssid)){
    Serial.print(">>> WiFi SSID: ");
    readSerial(ssid,MAX_SSID);
    if(atoi(ssid)>0 && atoi(ssid)<=numSSID){
      strcpy(ssid,ssidList[atoi(ssid)-1]);
    }
    Serial.print(ssid);
    Serial.print("\n");
  }
  
  while(!strlen(pwd)){
    Serial.print(">>> WiFi PASS: ");
    readSerial(pwd,MAX_PWD);    
    Serial.print(mask(pwd,2));
    Serial.print("\n");
  }

  homeSpan.statusLED.start(1000);   // slowly blink Status LED

  while(WiFi.status()!=WL_CONNECTED){
    Serial.print("\nConnecting to: ");
    Serial.print(ssid);
    Serial.print("... ");

    if(WiFi.begin(ssid,pwd)!=WL_CONNECTED){
      char buf[8]="";
      Serial.print("Can't connect. Re-trying in 5 seconds (or type 'X <return>' to cancel)...");
      long sTime=millis();
      while(millis()-sTime<5000){
        if(Serial.available()){
          readSerial(buf,1);
          if(buf[0]=='X'){
            Serial.print("Canceled!\n\n");
            return(0);
          }
        }
      }
    }
  } // WiFi not yet connected

  Serial.print("Success!  IP:  ");
  Serial.print(WiFi.localIP());
  Serial.print("\n\n");

  homeSpan.statusLED.start(250);   // rapidly blink Status LED
  
  boolean okay=false;
  Serial.print("Specify new 8-digit Setup Code or leave blank to retain existing code...\n\n");
  
  while(!okay){
    Serial.print("Setup Code: ");
    sprintf(setupCode,"");
    readSerial(setupCode,8);
        
    if(strlen(setupCode)==0){
      okay=true;
      Serial.print("(skipping)\n\n");
    } else { 
      Serial.print(setupCode);

      int n=0;
      while(setupCode[n]!='\0' && setupCode[n]>='0' && setupCode[n]<='9')
        n++;

      if(n<8)
        Serial.print("\n** Invalid format!\n\n");
      else if(!allowedCode(setupCode))
        Serial.print("\n** Disallowed code!\n\n");
      else {
        Serial.print("  Accepted!\n\n");
        okay=true; 
      }
             
    } // if Setup Code not blank
  } // while !okay

  char k[2]="";
  while(k[0]!='y' && k[0]!='n'){
    Serial.print("Confirm settings (y/n)? ");
    readSerial(k,1);
    Serial.print(k);
    Serial.print("\n");
  }

  Serial.print("\n");
    
  return(k[0]=='y');
}

///////////////////////////////

boolean Network::allowedCode(char *s){
  return(
    strcmp(s,"00000000") && strcmp(s,"11111111") && strcmp(s,"22222222") && strcmp(s,"33333333") && 
    strcmp(s,"44444444") && strcmp(s,"55555555") && strcmp(s,"66666666") && strcmp(s,"77777777") &&
    strcmp(s,"88888888") && strcmp(s,"99999999") && strcmp(s,"12345678") && strcmp(s,"87654321"));
}

///////////////////////////////

boolean Network::apConfigure(char *apName){

  Serial.print("Starting Access Point: ");
  Serial.print(apName);
  Serial.print(" / ");
  Serial.print(apPassword);
  Serial.print("\n");

  homeSpan.statusLED.start(100,0.5,2,500);     // fast double blink

  WiFiServer apServer(80);

  TempBuffer <uint8_t> tempBuffer(MAX_HTTP+1);
  uint8_t *httpBuf=tempBuffer.buf;
  
  const byte DNS_PORT = 53;
  DNSServer dnsServer;
  IPAddress apIP(192, 168, 4, 1);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName,apPassword);             // start access point
  dnsServer.start(DNS_PORT, "*", apIP);       // start DNS server that resolves every request to the address of this device
  apServer.begin();

  int status=0;                              // initialize status
  alarmTimeOut=millis()+lifetime;            // Access Point will shut down when alarmTimeOut is reached

  while(status==0){

    if(millis()>alarmTimeOut){
      Serial.print("\n*** WARNING:  Access Point timed Out (");
      Serial.print(lifetime/1000);
      Serial.print(" seconds)\n\n");
      ESP.restart();
    }

    dnsServer.processNextRequest();

    if(client=apServer.available()){                    // found a new HTTP client
      LOG2("=======================================\n");
      LOG1("** Access Point Client Connected: (");
      LOG1(millis()/1000);
      LOG1(" sec) ");
      LOG1(client.remoteIP());
      LOG1("\n");
      LOG2("\n");
    }
    
    if(client && client.available()){                   // if connection exists and data is available

      LOG2("<<<<<<<<< ");
      LOG2(client.remoteIP());
      LOG2(" <<<<<<<<<\n");
    
      int nBytes=client.read(httpBuf,MAX_HTTP+1);       // read all available bytes up to maximum allowed+1
       
      if(nBytes>MAX_HTTP){                              // exceeded maximum number of bytes allowed
        badRequestError();
        Serial.print("\n*** ERROR:  Exceeded maximum HTTP message length\n\n");
        continue;
      }

      httpBuf[nBytes]='\0';                             // add null character to enable string functions    
      char *body=(char *)httpBuf;                       // char pointer to start of HTTP Body
      char *p;                                          // char pointer used for searches
      
      if(!(p=strstr((char *)httpBuf,"\r\n\r\n"))){
        badRequestError();
        Serial.print("\n*** ERROR:  Malformed HTTP request (can't find blank line indicating end of BODY)\n\n");
        continue;      
      }

      *p='\0';                                          // null-terminate end of HTTP Body to faciliate additional string processing
      uint8_t *content=(uint8_t *)p+4;                  // byte pointer to start of optional HTTP Content
      int cLen=0;                                       // length of optional HTTP Content

      if((p=strstr(body,"Content-Length: ")))           // Content-Length is specified
        cLen=atoi(p+16);
      if(nBytes!=strlen(body)+4+cLen){
        badRequestError();
        Serial.print("\n*** ERROR:  Malformed HTTP request (Content-Length plus Body Length does not equal total number of bytes read)\n\n");
        continue;        
      }

      LOG2(body);
      LOG2("\n------------ END BODY! ------------\n");

      content[cLen]='\0';                               // add a trailing null on end of any contents, which should always be text-based

      status=processRequest(body, (char *)content);     // process request; returns 0=continue, 1=exit and save settings, -1=cancel changes and restart
      
      LOG2("\n");

    } // process HAP Client

  } // while status==0

  delay(2000);

  if(status==-1)
    ESP.restart();
  
  return(1);
}

///////////////////////////////

int Network::processRequest(char *body, char *formData){

  int returnCode=0;
  
  String responseHead="HTTP/1.1 200 OK\r\nContent-type: text/html\r\n";
  
  String responseBody="<html><head><style>"
                        "p{font-size:300%; margin:1em}"
                        "label{font-size:300%; margin:1em}"
                        "input{font-size:250%; margin:1em}"
                        "button{font-size:250%; margin:1em}"
                      "</style></head>"
                      "<body style=\"background-color:lightyellow;\">"
                      "<center><p><b>HomeSpan Setup</b></p></center>";

  if(!strncmp(body,"POST /configure ",16) &&                              // POST CONFIGURE
     strstr(body,"Content-Type: application/x-www-form-urlencoded")){     // check that content is from a form

    LOG2(formData);                                                       // print form data
    LOG2("\n------------ END DATA! ------------\n");
               
    LOG1("In Post Configure...\n");

    getFormValue(formData,"network",ssid,MAX_SSID);
    getFormValue(formData,"pwd",pwd,MAX_PWD);
    
    timer=millis();
    homeSpan.statusLED.start(1000);

    responseBody+="<meta http-equiv = \"refresh\" content = \"2; url = /wifi-status\" />"
                  "<p>Initiating WiFi connection to:</p><p><b>" + String(ssid) + "</p>";
  
  } else

  if(!strncmp(body,"POST /confirm-restart ",21)){                         // GET CONFIRM-RESTART 
    responseBody+="<p><b>Settings saved!</b></p><p>Restarting HomeSpan in 2 seconds...</p>";
    getFormValue(formData,"code",setupCode,8);
    // insert code to check whether setup Code allowed or not
    returnCode=1;    
  } else

  if(!strncmp(body,"GET /cancel-restart ",20)){                           // GET CANCEL-RESTART 
    responseBody+="<p><b>Update canceled!</b></p><p>Restarting HomeSpan in 2 seconds...</p>";
    returnCode=-1;    
  } else

  if(!strncmp(body,"GET /wifi-status ",17)){                              // GET WIFI-STATUS

    LOG1("In Get WiFi Status...\n");

    if(WiFi.status()!=WL_CONNECTED && WiFi.begin(ssid,pwd)!=WL_CONNECTED){
      responseHead+="Refresh: 5\r\n";     
      
      responseBody+="<p>Re-trying connection to:</p><p><b>" + String(ssid) + "</p>";
      responseBody+="<p>Timeout in " + String((alarmTimeOut-millis())/1000) + " seconds.</p>";
      responseBody+="<center><button onclick=\"document.location='/landing-page'\">Cancel</button></center>";
    } else {
      
      homeSpan.statusLED.start(500,0.3,2,1000);   // slow double-blink
      
      responseBody+="<p>SUCCESS! Connected to:</p><p><b>" + String(ssid) + "</b></p>";
      responseBody+="<p>You may enter new 8-digit Setup Code below, or leave blank to retain existing code.</p>";

      responseBody+="<form action=\"/confirm-restart\" method=\"post\">"
                    "<label for=\"code\">Setup Code:</label>"
                    "<center><input size=\"32\" type=\"tel\" id=\"code\" name=\"code\" placeholder=\"12345678\" pattern=\"[0-9]{8}\" maxlength=8></center>"
                    "<center><input style=\"font-size:300%\" type=\"submit\" value=\"SAVE Settings\"></center>"
                    "</form>";
                    
      responseBody+="<center><button style=\"font-size:300%\" onclick=\"document.location='/cancel-restart'\">CANCEL Configuration</button></center>";
    }
  
  } else                                                                

  if(!strncmp(body,"GET /landing-page ",18)){                             // GET LANDING-PAGE

    LOG1("In Landing Page...\n");

    landingPage=true;

    homeSpan.statusLED.start(500,0.3,2,1000);   // slow double-blink

    responseBody+="<p>Welcome to HomeSpan! This page allows you to configure the above HomeSpan device to connect to your WiFi network.</p>"
                  "<p>The LED on this device should be <em>double-blinking</em> during this configuration.</p>"
                  "<form action=\"/configure\" method=\"post\">"
                  "<label for=\"ssid\">WiFi Network:</label>"
                  "<center><input size=\"32\" list=\"network\" name=\"network\" placeholder=\"Choose or Type\" required maxlength=" + String(MAX_SSID) + "></center>"
                  "<datalist id=\"network\">";

    for(int i=0;i<numSSID;i++)
        responseBody+="<option value=\"" + String(ssidList[i]) + "\">" + String(ssidList[i]) + "</option>";  
    
    responseBody+="</datalist><br><br>"
                  "<label for=\"pwd\">WiFi Password:</label>"
                  "<center><input size=\"32\" type=\"password\" id=\"pwd\" name=\"pwd\" required maxlength=" + String(MAX_PWD) + "></center>"
                  "<br><br>";
                  
    responseBody+="<center><input style=\"font-size:300%\" type=\"submit\" value=\"SUBMIT\"></center>"
                  "</form>";
  } else {

    if(!landingPage)
      responseHead="HTTP/1.1 307 Temporary Redirect\r\nLocation: /landing-page\r\n";
  }

  responseHead+="\r\n";               // add blank line between reponse header and body
  responseBody+="</body></html>";     // close out body and html tags

  LOG2("\n>>>>>>>>>> ");
  LOG2(client.remoteIP());
  LOG2(" >>>>>>>>>>\n");
  LOG2(responseHead);
  LOG2(responseBody);
  LOG2("\n");
  client.print(responseHead);
  client.print(responseBody);
  LOG2("------------ SENT! --------------\n");

  return(returnCode);
    
} // processRequest

//////////////////////////////////////

int Network::getFormValue(char *formData, char *tag, char *value, int maxSize){

  char *s=strstr(formData,tag);     // find start of tag
  
  if(!s)                            // if not found, return -1
    return(-1);

  char *v=index(s,'=');             // find '='

  if(!v)                            // if not found, return -1 (this should not happen)
    return(-1);

  v++;                              // point to begining of value 
  int len=0;                        // track length of value
  
  while(*v!='\0' && *v!='&' && len<maxSize){      // copy the value until null, '&', or maxSize is reached
    len++;
    *value++=*v++;
  }

  *value='\0';                      // add terminating null
  return(len);
  
}

//////////////////////////////////////

int Network::badRequestError(){

  char s[]="HTTP/1.1 400 Bad Request\r\n\r\n";
  LOG2("\n>>>>>>>>>> ");
  LOG2(client.remoteIP());
  LOG2(" >>>>>>>>>>\n");
  LOG2(s);
  client.print(s);
  LOG2("------------ SENT! --------------\n");
  
  delay(1);
  client.stop();

  return(-1);
}

//////////////////////////////////////
