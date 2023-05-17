/*
An EPS32 program that displays a list of Wi-Fi networks in the terminal.

Can use with android Serial USB terminal such UsbTerminal
115200 8 N 1

1. Flash to any esp32 board with a hardware serial
2. Open terminal or connect to the mobile phone using OTA cable

VT-100 support must be autodetected

Released into the public domain.

Created by Denis, 2023. Thanks for https://github.com/LieBtrau
*/

#include <Arduino.h>
#include "WiFi.h"

//-----------------------------------------------------------------------------------
//Xterm
typedef enum{
        NORMAL=0,
        BOLD=1,
        UNDERLINED=4,
        BLINK=5,
        INVERSE=7,
    }CHARACTERTYPE;
typedef enum{
    BLACK=0,
    RED=1,
    GREEN=2,
    YELLOW=3,
    BLUE=4,
    MAGENTA=5,
    CYAN=6,
    WHITE=7,
    DEF=9
}COLOR;

class Xterm
{
public:
    Xterm(HardwareSerial * stream);
    bool init();
    void print(char);
    void print(uint8_t);
    void print(int);
    void print(unsigned int);
    void print(long);
    void print(unsigned long);
    template <typename... Args> size_t printf(const int row, const int col, const CHARACTERTYPE m,const char * format, Args... args){
        setCursorType(m);
        setCursorPos(row, col);
        return _stream->printf(format, args...);
    }
    void setCursorPos(int row, int col);
    void setCursorType(CHARACTERTYPE m);
    void setForegroundColor(COLOR c);
    void setBackgroundColor(COLOR c);
    bool getTerminalType(byte& terminalType);
    template<typename T> void print(int row, int col, T &t, CHARACTERTYPE m)
    {
        setCursorType(m);
        setCursorPos(row, col);
        _stream->print(t);          // text
    }
    void clear();
private:
    void setCharacterAttributes(byte m);
    HardwareSerial* _stream;
};

Xterm::Xterm(HardwareSerial* stream): _stream(stream){
}
bool Xterm::init()
{
    byte type;
    if(!getTerminalType(type))
    {
        return false;
    }
    _stream->print("\eSP F");  	// tell to use 7-bit control codes (will be echoed back)
    _stream->print("\e[?25l"); 	// hide cursor
    _stream->print("\e[?12l");	// disable cursor highlighting
    clear(); 	// clear entire screen
    return true;
}

//\e[?62;3c    //VT220 with ReGIS graphics (response from GTKTerm)
//\e[?1;2c     //VT100 with Advanced Video Option (response from minicom)
bool Xterm::getTerminalType(byte& terminalType)
{
    while(_stream->available()>0)_stream->read();       //clear input buffer
    _stream->print("\e[c");                             // request attributes from terminal
     String response=_stream->readStringUntil('c');
    if(response.length()==0 || response.charAt(0)!='\e' ||
            response.charAt(1)!='[' || response.charAt(2)!='?')
    {
        return false;
    }
    byte semicolonPos=response.indexOf(';');
    if(semicolonPos<0){
        return false;
    }
    response=response.substring(3,semicolonPos);
    terminalType=response.toInt();
    return true;
}

void Xterm::clear()
{
    _stream->print("\e[2J"); // clear screen
}

#include <cstdarg>

void Xterm::setCursorPos(int row, int col){
    _stream->print("\e[");  	// CSI (control sequence initializer)
    _stream->print(row);
    _stream->print(";");
    _stream->print(col);
    _stream->print("f");
}

void Xterm::setForegroundColor(COLOR c)
{
    if(c>=BLACK && c!=DEF){
        setCharacterAttributes(c+30);
    }
}

void Xterm::setBackgroundColor(COLOR c)
{
    if(c>=BLACK && c!=DEF){
        setCharacterAttributes(c+40);
    }

}

void Xterm::setCursorType(CHARACTERTYPE m){
    switch (m) // m = mode
    {
    case 1: // Bold
    case 4: // Underlined
    case 5: // Blink
    case 7: // Inverse
        setCharacterAttributes(m);
        break;
    default: // Normal
        setCharacterAttributes(0);

    }
}

void Xterm::setCharacterAttributes(byte m){
    String cmd="\e[";
    cmd+= String(m);
    cmd+='m';
    _stream->print(cmd);
}

Xterm xterm=Xterm(&Serial);
//----------------------------------------------------------------------------------
bool writeScreen();
void writeMid(int row);
void writeBot(int row);

bool useXterm = false;

void setup() {
  Serial.begin(115200);
  delay(5);//5ms


  byte type;
  if(!xterm.getTerminalType(type) || (type<1)) {
    Serial.printf("WARNING: unknown terminal type. xterm functions disabled\n",type);
  } else {
    xterm.init();
    useXterm = true;
    xterm.print(1,1,"Starting...",NORMAL); 
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
}

struct datatype {
    String name;
    unsigned long first;
    unsigned long last;
    int count;
    int lastRSSI;
    int64_t sumRSSI;
    int channel;
};

#include <vector>

void loop() {
  std::vector<datatype> data;
  int rc=0;

  if (useXterm) writeScreen();

  do {
    int n = WiFi.scanNetworks();
    for (int i=0; i<n; i++) {
        bool found = false;
        for (auto &d : data) {// access by reference to avoid copying
            if (d.name == WiFi.SSID(i)) {
                found = true;
                d.last = millis();
                d.lastRSSI = WiFi.RSSI(i);
                d.sumRSSI += d.lastRSSI;
                d.count++;
                break;
            }
        }

        if (!found) data.push_back(datatype{
          name:WiFi.SSID(i).c_str(),  
          first:millis(),
          last:millis(),
          count:1,
          lastRSSI:WiFi.RSSI(i),
          sumRSSI:WiFi.RSSI(i),
          channel:WiFi.channel(i)
        });
    }


    if (useXterm) {

        std::sort(data.begin(),data.end(),[](datatype &a, datatype &b){ return a.sumRSSI/a.count>b.sumRSSI/b.count; });

        if (rc!=data.size()) writeBot(data.size()+4);
        while (rc<data.size())writeMid(rc++ + 4);
            
        int i=1;
        for (datatype &d : data) {
            //30 chars
            char name[31] = "                              ";
            memcpy(name,d.name.c_str(),d.name.length()>30?30:d.name.length());
            for (int j = 0; j<strlen(name); j++ ) if (name[j]>127) name[j] = '?'; //replace non-ascii chars
            
            xterm.printf(i+3,3,NORMAL,"%d ",i);
            //xterm.print(i+3,7,"                                ",NORMAL);
            xterm.printf(i+3,8,NORMAL,"%s",name);
            xterm.printf(i+3,41,NORMAL,"%d  ",d.lastRSSI);
            xterm.printf(i+3,49,NORMAL,"%d  ",d.sumRSSI/d.count);
            xterm.printf(i+3,57,NORMAL,"%d  ",(millis()-d.last)/1000);
        
            i++;
        }

        xterm.printf(rc+5,1,NORMAL,"found %d networks; uptime %d seconds      ",n,millis()/1000);   
    } else {
        //Non-xterm mode. order oposite, show last records only
        std::sort(data.begin(),data.end(),[](datatype &a, datatype &b){ return a.sumRSSI/a.count<b.sumRSSI/b.count; });
        Serial.printf("========%d sec; %d networks=====\n",millis()/1000,n);
        Serial.printf("RSSI | Avg | Delay | Name\n",millis()/1000,n);
        int i=1;
        
        for (datatype &d : data) if ((i+16)>(data.size())) {
            
            char name[31] = {0};
            memcpy(name,d.name.c_str(),d.name.length()>30?30:d.name.length());
            for (int j = 0; j<strlen(name); j++ ) if (name[j]>127) name[j] = '?'; //replace non-ascii chars
            name[d.name.length()>30?30:d.name.length()] = 0;

            char del[16];
            sprintf(del,"%d     ",(millis()-d.last)/1000);
            del[5] = 0;

            Serial.printf(" %03d | %03d | %s | %s\n",d.lastRSSI,(int)(d.sumRSSI/d.count),del,name);

            i++;
        } else i++;

    }
    
    delay(1000);
  } while (true);
}


void writeMid(int row) {
    xterm.print(row,1,"║    ║                                ║       ║       ║           ║",NORMAL); 
}
void writeBot(int row) {
    xterm.print(row,1,"╚════╩════════════════════════════════╩═══════╩═══════╩═══════════╝",NORMAL); 
}
bool writeScreen() {
  xterm.clear();

                 //00000000011111111112222222222333333333344444444445555555555666
                 //12345678901234567890123456789012345678901234567890123456789012
  xterm.print(1,1,"╔════╦════════════════════════════════╦═══════╦═══════╦═══════════╗",NORMAL); 
  xterm.print(2,1,"║ ## ║ Network name                   ║ RSSI  ║ Avg   ║ Delay     ║",NORMAL); 
  xterm.print(3,1,"╠════╬════════════════════════════════╬═══════╬═══════╬═══════════╣",NORMAL); 
  xterm.print(4,1,"╚════╩════════════════════════════════╩═══════╩═══════╩═══════════╝",NORMAL); 
  return true;
}